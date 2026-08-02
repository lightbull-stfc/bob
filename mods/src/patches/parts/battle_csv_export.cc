#include "battle_csv_export.h"

#include "config.h"
#include "errormsg.h"
#include "file.h"
#include "str_utils.h"

#include <prime/BattleResultsManager.h>

#include <spdlog/spdlog.h>
#include <spud/detour.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BattleCsvExport
{
namespace
{
using namespace std::chrono_literals;

constexpr std::array<std::chrono::milliseconds, 5> ReportRetryDelays{250ms, 1s, 3s, 10s, 30s};
constexpr std::array<std::chrono::milliseconds, 3> JsonRetryDelays{500ms, 2s, 5s};

enum class ExportStage {
  Queued,
  Fetching,
  Writing,
};

struct PendingExport {
  uint64_t                              battle_id = 0;
  ExportStage                           stage = ExportStage::Queued;
  size_t                                fetch_attempts = 0;
  std::chrono::steady_clock::time_point next_attempt{};
};

struct WriteTask {
  uint64_t              battle_id = 0;
  std::filesystem::path csv_path;
  std::filesystem::path json_path;
  std::string           csv;
  bool                  write_csv = true;
  bool                  write_json = true;
};

struct WriteResult {
  uint64_t              battle_id = 0;
  std::filesystem::path csv_path;
  std::filesystem::path json_path;
  bool                  csv_success = false;
  bool                  json_success = false;
  std::string           csv_error;
  std::string           json_error;
};

BattleResultsManager* manager = nullptr;
RawJsonFetcher        raw_json_fetcher;

std::mutex                   pending_mutex;
std::deque<PendingExport>    pending_exports;
std::unordered_set<uint64_t> observed_battle_ids;
bool                         baseline_initialized = false;

std::mutex              write_mutex;
std::condition_variable write_cv;
std::deque<WriteTask>   write_queue;

std::mutex              result_mutex;
std::deque<WriteResult> write_results;
std::once_flag          writer_start_flag;

std::filesystem::path ConfigDirectory()
{
  namespace fs = std::filesystem;

#if _WIN32
  fs::path config_path{std::string(File::MakePath(File::Config()))};
#else
  fs::path config_path{File::MakePath(File::Config())};
#endif

  auto base = config_path.parent_path();
  if (!base.empty()) {
    return base;
  }

  std::error_code ec;
  base = fs::current_path(ec);
  return ec ? fs::path{"."} : base;
}

std::filesystem::path ResolveOutputDirectory()
{
  namespace fs = std::filesystem;

  const auto& configured = Config::Get().battle_csv_export_directory;
  if (configured.empty()) {
    return (ConfigDirectory() / "battle-logs").lexically_normal();
  }

  fs::path directory{configured};
  if (directory.is_relative()) {
    directory = ConfigDirectory() / directory;
  }
  return directory.lexically_normal();
}

std::filesystem::path CsvOutputPath(uint64_t battle_id)
{
  return ResolveOutputDirectory() / ("battle-" + std::to_string(battle_id) + ".csv");
}

std::filesystem::path JsonOutputPath(uint64_t battle_id)
{
  return ResolveOutputDirectory() / ("battle-" + std::to_string(battle_id) + ".json");
}

bool WriteFileAtomically(const std::filesystem::path& path, const std::string& data, std::string& error)
{
  namespace fs = std::filesystem;

  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "failed to create output directory: " + ec.message();
    return false;
  }

  if (fs::exists(path, ec)) {
    return true;
  }

  auto temporary = path;
  temporary += ".tmp";
  fs::remove(temporary, ec);
  ec.clear();

  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      error = "failed to open temporary output file";
      return false;
    }

    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    output.close();
    if (!output) {
      fs::remove(temporary, ec);
      error = "failed while writing output data";
      return false;
    }
  }

  fs::rename(temporary, path, ec);
  if (ec) {
    std::error_code exists_ec;
    if (fs::exists(path, exists_ec)) {
      fs::remove(temporary, ec);
      return true;
    }

    fs::remove(temporary, ec);
    error = "failed to finalize output file: " + ec.message();
    return false;
  }

  return true;
}

std::string FetchRawJson(uint64_t battle_id)
{
  if (!raw_json_fetcher) {
    return {};
  }

  for (size_t attempt = 0; attempt < JsonRetryDelays.size(); ++attempt) {
    auto json = raw_json_fetcher(battle_id);
    if (!json.empty()) {
      return json;
    }

    if (attempt + 1 < JsonRetryDelays.size()) {
      std::this_thread::sleep_for(JsonRetryDelays[attempt]);
    }
  }

  return {};
}

WriteResult WriteFiles(WriteTask&& task)
{
  WriteResult result{
      .battle_id=task.battle_id,
      .csv_path=task.csv_path,
      .json_path=task.json_path,
      .csv_success=!task.write_csv,
      .json_success=!task.write_json,
  };

  if (task.write_csv) {
    result.csv_success = WriteFileAtomically(task.csv_path, task.csv, result.csv_error);
  }

  if (task.write_json) {
    auto raw_json = FetchRawJson(task.battle_id);
    if (raw_json.empty()) {
      result.json_error = "game server returned no raw journal JSON after retries";
    } else {
      result.json_success = WriteFileAtomically(task.json_path, raw_json, result.json_error);
    }
  }

  return result;
}

void WriterThread()
{
  for (;;) {
    WriteTask task;
    {
      std::unique_lock lock(write_mutex);
      write_cv.wait(lock, [] { return !write_queue.empty(); });
      task = std::move(write_queue.front());
      write_queue.pop_front();
    }

    auto result = WriteFiles(std::move(task));
    {
      std::scoped_lock lock(result_mutex);
      write_results.push_back(std::move(result));
    }
  }
}

void StartWriter()
{
  std::call_once(writer_start_flag, [] { std::thread(WriterThread).detach(); });
}

void RemovePending(uint64_t battle_id, bool forget_observed)
{
  std::scoped_lock lock(pending_mutex);

  for (auto it = pending_exports.begin(); it != pending_exports.end(); ++it) {
    if (it->battle_id == battle_id) {
      pending_exports.erase(it);
      break;
    }
  }

  if (forget_observed) {
    observed_battle_ids.erase(battle_id);
  }
}

void HandleWriteResults()
{
  std::deque<WriteResult> results;
  {
    std::scoped_lock lock(result_mutex);
    results.swap(write_results);
  }

  for (auto& result : results) {
    if (result.csv_success && result.json_success) {
      spdlog::info("Exported battle {} to {} and {}", result.battle_id, result.csv_path.string(),
                   result.json_path.string());
      RemovePending(result.battle_id, false);
      continue;
    }

    if (!result.csv_success) {
      spdlog::error("Failed to export battle {} CSV to {}: {}", result.battle_id, result.csv_path.string(),
                    result.csv_error);
    }
    if (!result.json_success) {
      spdlog::error("Failed to export battle {} raw JSON to {}: {}", result.battle_id, result.json_path.string(),
                    result.json_error);
    }

    RemovePending(result.battle_id, true);
  }
}

void QueueWrite(WriteTask&& task)
{
  {
    std::scoped_lock lock(write_mutex);
    write_queue.push_back(std::move(task));
  }
  write_cv.notify_one();
}

void Tick()
{
  HandleWriteResults();

  if (!Config::Get().battle_csv_export_enabled || manager == nullptr) {
    return;
  }

  PendingExport pending;
  {
    std::scoped_lock lock(pending_mutex);
    if (pending_exports.empty() || pending_exports.front().stage == ExportStage::Writing) {
      return;
    }
    pending = pending_exports.front();
  }

  const auto csv_path = CsvOutputPath(pending.battle_id);
  const auto json_path = JsonOutputPath(pending.battle_id);
  std::error_code ec;
  const bool csv_exists = std::filesystem::exists(csv_path, ec);
  ec.clear();
  const bool json_exists = std::filesystem::exists(json_path, ec);

  if (csv_exists && json_exists) {
    spdlog::debug("Battle {} is already exported at {} and {}", pending.battle_id, csv_path.string(),
                  json_path.string());
    RemovePending(pending.battle_id, false);
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (pending.stage == ExportStage::Queued) {
    manager->FetchBattleReport(static_cast<int64_t>(pending.battle_id));

    std::scoped_lock lock(pending_mutex);
    if (!pending_exports.empty() && pending_exports.front().battle_id == pending.battle_id) {
      pending_exports.front().stage = ExportStage::Fetching;
      pending_exports.front().fetch_attempts = 1;
      pending_exports.front().next_attempt = now + ReportRetryDelays.front();
    }
    return;
  }

  if (now < pending.next_attempt) {
    return;
  }

  BattleResultData* battle_report = nullptr;
  if (!manager->TryGetBattleReport(static_cast<int64_t>(pending.battle_id), battle_report)
      || battle_report == nullptr) {
    bool remove = false;
    {
      std::scoped_lock lock(pending_mutex);
      if (pending_exports.empty() || pending_exports.front().battle_id != pending.battle_id) {
        return;
      }

      auto& current = pending_exports.front();
      if (current.fetch_attempts >= ReportRetryDelays.size()) {
        remove = true;
      } else {
        manager->FetchBattleReport(static_cast<int64_t>(current.battle_id));
        current.next_attempt = now + ReportRetryDelays[current.fetch_attempts];
        ++current.fetch_attempts;
      }
    }

    if (remove) {
      spdlog::error("Battle {} was not available after {} fetch attempts; it will be retried when observed again",
                    pending.battle_id, ReportRetryDelays.size());
      RemovePending(pending.battle_id, true);
    }
    return;
  }

  auto* battle_target = manager->RetrieveBattleTargetDataFromBattleResult(battle_report);
  auto* utility = manager->ExportUtility();
  if (battle_target == nullptr || utility == nullptr) {
    spdlog::error("Unable to build CSV data for battle {}: battle target or export utility is unavailable",
                  pending.battle_id);
    RemovePending(pending.battle_id, true);
    return;
  }

  auto* csv_string = utility->ExportToCSV(battle_target);
  if (csv_string == nullptr) {
    spdlog::error("The game CSV exporter returned no data for battle {}", pending.battle_id);
    RemovePending(pending.battle_id, true);
    return;
  }

  auto csv = to_string(csv_string);
  {
    std::scoped_lock lock(pending_mutex);
    if (pending_exports.empty() || pending_exports.front().battle_id != pending.battle_id) {
      return;
    }
    pending_exports.front().stage = ExportStage::Writing;
  }

  QueueWrite(WriteTask{
      .battle_id=pending.battle_id,
      .csv_path=csv_path,
      .json_path=json_path,
      .csv=std::move(csv),
      .write_csv=!csv_exists,
      .write_json=!json_exists,
  });
}

void BattleResultsManager_Initialize_Hook(auto original, BattleResultsManager* self)
{
  original(self);
  manager = self;
}

void ScreenManager_LateUpdate_Hook(auto original, void* self)
{
  original(self);
  Tick();
}
} // namespace

void SetRawJsonFetcher(RawJsonFetcher fetcher)
{
  raw_json_fetcher = std::move(fetcher);
}

void ObserveBattleHeaders(const std::vector<uint64_t>& battle_ids)
{
  if (!Config::Get().battle_csv_export_enabled) {
    return;
  }

  size_t queued = 0;
  {
    std::scoped_lock lock(pending_mutex);

    if (!baseline_initialized) {
      baseline_initialized = true;

      if (!Config::Get().battle_csv_export_existing) {
        size_t repairs = 0;
        for (auto it = battle_ids.rbegin(); it != battle_ids.rend(); ++it) {
          observed_battle_ids.insert(*it);

          std::error_code ec;
          const bool csv_exists = std::filesystem::exists(CsvOutputPath(*it), ec);
          ec.clear();
          const bool json_exists = std::filesystem::exists(JsonOutputPath(*it), ec);
          if (csv_exists != json_exists) {
            pending_exports.push_back(PendingExport{.battle_id=*it});
            ++repairs;
          }
        }

        spdlog::info("Battle report exporter initialized with {} existing battle headers; queued {} incomplete "
                     "CSV/JSON pair(s) for repair",
                     battle_ids.size(), repairs);
        return;
      }
    }

    for (auto it = battle_ids.rbegin(); it != battle_ids.rend(); ++it) {
      if (observed_battle_ids.insert(*it).second) {
        pending_exports.push_back(PendingExport{.battle_id=*it});
        ++queued;
      }
    }
  }

  if (queued > 0) {
    spdlog::info("Queued {} battle report(s) for CSV and raw JSON export", queued);
  }
}

void InstallHooks()
{
  const auto output_directory = ResolveOutputDirectory();
  std::error_code ec;
  std::filesystem::create_directories(output_directory, ec);
  if (ec) {
    spdlog::error("Battle CSV/JSON exporter could not create output directory {}: {}", output_directory.string(),
                  ec.message());
  } else {
    spdlog::info("Battle CSV/JSON exporter enabled; output directory: {}", output_directory.string());
  }

  StartWriter();

  auto& manager_helper = BattleResultsManager::get_class_helper();
  if (!manager_helper.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Prime.BattleResults", "BattleResultsManager");
  } else if (auto* ptr = manager_helper.GetMethod("Initialize", 0); ptr == nullptr) {
    ErrorMsg::MissingMethod("BattleResultsManager", "Initialize");
  } else {
    SPUD_STATIC_DETOUR(ptr, BattleResultsManager_Initialize_Hook);
  }

  auto screen_manager = il2cpp_get_class_helper("Assembly-CSharp", "Digit.Client.UI", "ScreenManager");
  if (!screen_manager.isValidHelper()) {
    ErrorMsg::MissingHelper("Digit.Client.UI", "ScreenManager");
  } else if (auto* ptr = screen_manager.GetMethod("LateUpdate", 0); ptr == nullptr) {
    ErrorMsg::MissingMethod("ScreenManager", "LateUpdate");
  } else {
    SPUD_STATIC_DETOUR(ptr, ScreenManager_LateUpdate_Hook);
  }
}
} // namespace BattleCsvExport

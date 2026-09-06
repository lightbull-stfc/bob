#pragma once

#include <cstdint>

#include <il2cpp/il2cpp_helper.h>

#include "BattleReportCSVUtility.h"

struct BattleResultData;
struct BattleTargetData;

struct BattleResultsManager {
public:
  void FetchBattleReport(int64_t battle_id)
  {
    static auto method =
        get_class_helper().GetMethod<void(BattleResultsManager*, int64_t, Il2CppObject*)>("FetchBattleReport", 2);
    if (method) {
      method(this, battle_id, nullptr);
    }
  }

  bool TryGetBattleReport(int64_t battle_id, BattleResultData*& battle_report)
  {
    static auto method = get_class_helper().GetMethod<bool(BattleResultsManager*, int64_t, BattleResultData**)>(
        "TryGetBattleReport", 2);
    battle_report = nullptr;
    return method && method(this, battle_id, &battle_report);
  }

  BattleTargetData* RetrieveBattleTargetDataFromBattleResult(BattleResultData* battle_report)
  {
    static auto method = get_class_helper().GetMethod<BattleTargetData*(BattleResultsManager*, BattleResultData*)>(
        "RetrieveBattleTargetDataFromBattleResult", 1);
    return method ? method(this, battle_report) : nullptr;
  }

  BattleReportCSVUtility* ExportUtility()
  {
    static auto field = get_class_helper().GetField("_exportUtility");
    if (!field.isValidHelper()) {
      return nullptr;
    }

    return *reinterpret_cast<BattleReportCSVUtility**>(reinterpret_cast<char*>(this) + field.offset());
  }

  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.BattleResults", "BattleResultsManager");
    return class_helper;
  }
};

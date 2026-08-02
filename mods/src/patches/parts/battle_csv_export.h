#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace BattleCsvExport
{
using RawJsonFetcher = std::function<std::string(uint64_t)>;

void SetRawJsonFetcher(RawJsonFetcher fetcher);
void ObserveBattleHeaders(const std::vector<uint64_t>& battle_ids);
void InstallHooks();
} // namespace BattleCsvExport

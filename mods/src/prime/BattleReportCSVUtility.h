#pragma once

#include <il2cpp/il2cpp_helper.h>

struct BattleTargetData;

struct BattleReportCSVUtility {
public:
  Il2CppString* ExportToCSV(BattleTargetData* battle_target_data)
  {
    static auto method = get_class_helper().GetMethod<Il2CppString*(BattleReportCSVUtility*, BattleTargetData*)>(
        "ExportToCSV", 1);
    return method ? method(this, battle_target_data) : nullptr;
  }

  static IL2CppClassHelper& get_class_helper()
  {
    static auto class_helper =
        il2cpp_get_class_helper("Assembly-CSharp", "Digit.Prime.Combat", "BattleReportCSVUtility");
    return class_helper;
  }
};

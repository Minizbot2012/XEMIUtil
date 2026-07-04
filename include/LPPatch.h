#pragma once
#include <filesystem>
namespace MPL::LPPatch
{
  // A single expanded user rule: any Light Placer light whose base LIGH editor id matches
  // `light` gets its `externalEmittance` rewritten to `externalEmittance`.
  struct PatchRule
  {
    std::string light;
    std::string externalEmittance;
  };

  static const std::filesystem::path RULES_DIR = "Data/SKSE/XEMIUtil/LightPlacer";
  static const std::filesystem::path LP_CONFIG_DIR = R"(Data\LightPlacer)";
  // Installs the Light Placer emittance-patch feature: on the first in-game load of a
  // session it rewrites the `externalEmittance` of matching Light Placer light configs,
  // fires Light Placer's own `ReloadLP` console command so the change takes effect live,
  // then restores the original config files on disk (clean uninstall).
  void Install();
}  // namespace MPL::LPPatch

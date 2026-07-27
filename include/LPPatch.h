#pragma once

#include <RE/Skyrim.h>
#include <XEMI_API.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace MPL::LPPatch
{
    struct SourcePlacement
    {
        RE::FormID reference = 0;
        RE::FormID base = 0;
        RE::FormID cell = 0;
        std::string model;
        std::vector<RE::FormID> filterIDs;
    };

    struct PatchRule
    {
        std::unordered_set<std::string> lights;
        std::string externalEmittance;
        std::vector<SourcePlacement> placements;
        bool detailedLogging = false;
    };

    std::string NormalizeModelPath(std::string_view a_path);
    std::string StableFormKey(RE::FormID a_formID);

    // Runs once from Window Sync's startup index. Matching Light Placer entries are
    // partitioned by cell, reloaded, and restored on disk before gameplay begins.
    void QueueStartupPatch(std::vector<PatchRule> a_rules);
    bool RegisterTransformer(
        const XEMIAPI::LightPlacerTransformer* a_transformer);
    bool RequestReload();
}  // namespace MPL::LPPatch

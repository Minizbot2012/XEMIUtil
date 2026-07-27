#pragma once

#include <RE/Skyrim.h>
#include <cstddef>
#include <unordered_map>

namespace MPL::PluginIndex
{
    struct Placement
    {
        RE::FormID reference = 0;
        RE::FormID base = 0;
        RE::FormID cell = 0;
        bool deleted = false;
    };

    struct Result
    {
        std::unordered_map<RE::FormID, Placement> placements;
        std::size_t pluginsDiscovered = 0;
        std::size_t pluginsParsed = 0;
        std::size_t referencesRead = 0;
        std::size_t compressedReferences = 0;
        bool complete = false;
    };

    Result Build();
}  // namespace MPL::PluginIndex

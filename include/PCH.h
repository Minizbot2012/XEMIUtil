#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
// clang-format off
// #define DEBUG
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include <REX/REX.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/fmt/bin_to_hex.h>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <windows.h>
// clang-format on
const std::unordered_map<std::string, std::string> string_replacement_table = {
    { "legacyofthedragonbornv5esm" , "legacyofthedragonbornesm" }
};
#ifdef SKYRIM_AE
#    define OFFSET(se, ae) ae
#    define OFFSET_3(se, ae, vr) ae
#elif SKYRIM_VR
#    define OFFSET(se, ae) se
#    define OFFSET_3(se, ae, vr) vr
#else
#    define OFFSET(se, ae) se
#    define OFFSET_3(se, ae, vr) se
#endif
#define DLLEXPORT __declspec(dllexport)
namespace logger = SKSE::log;
using namespace std::literals;

#include "Hooking.h"
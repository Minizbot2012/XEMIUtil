#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
// clang-format off
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <rfl.hpp>
#include <rfl/json.hpp>
#include <REX/REX.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/fmt/bin_to_hex.h>
#include <windows.h>
// clang-format on
#define DLLEXPORT __declspec(dllexport)
namespace logger = SKSE::log;
using namespace std::literals;
#include <Hooking.h>

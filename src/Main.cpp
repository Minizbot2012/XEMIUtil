#include "SKSE/Interfaces.h"
#include <Config.h>
#include <DetailedLogging.h>
#include <Hooks.h>
#include <Plugin.h>
#include <REL/Version.h>

SKSEPluginInfo(
    .Version = REL::Version{ MPL::Plugin::MAJOR, MPL::Plugin::MINOR, MPL::Plugin::PATCH, 0 },
    .Name = MPL::Plugin::PROJECT,
    .Author = "Mini"sv,
    .SupportEmail = ""sv,
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary
);

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    SKSE::Init(a_skse);
    logger::info("Game version : {}", a_skse->RuntimeVersion().string());
    MPL::DetailedLogging::Initialize();
    MPL::Hooks::Install();
    return true;
}

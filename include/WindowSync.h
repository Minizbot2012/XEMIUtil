#pragma once

namespace RE
{
    class TESObjectCELL;
    class TESObjectREFR;
}  // namespace RE

namespace MPL::WindowSync
{
    void Install();
    void ProcessReference(RE::TESObjectREFR*);
    void ProcessCell(RE::TESObjectCELL*);
    void ResetRuntimeState();
}  // namespace MPL::WindowSync

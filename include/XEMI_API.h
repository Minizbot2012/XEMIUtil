#pragma once

#include <cstddef>
#include <cstdint>

namespace RE
{
    class TESObjectCELL;
    class TESObjectREFR;
}

namespace MPL::XEMIAPI
{
    inline constexpr std::uint32_t kVersion = 5;

    using LightPlacerOutput =
        bool (*)(void*, const char*, std::size_t);
    using LightPlacerTransform =
        bool (*)(
            const char*,
            const char*,
            std::size_t,
            LightPlacerOutput,
            void*);

    struct LightPlacerTransformer
    {
        // The ID is copied during registration.
        const char* id = nullptr;
        // Input and output pointers are valid only during this synchronous call.
        // A successful transform must invoke the output callback exactly once.
        LightPlacerTransform TransformJson = nullptr;
        void (*OnReloadComplete)() = nullptr;
    };

    enum class CellStatus : std::uint8_t
    {
        kUnknown,
        kNoMatch,
        kMatched,
    };

    enum class CellResultFlag : std::uint32_t
    {
        kProfilesTruncated = 1U << 0,
    };

    struct CellResult
    {
        CellStatus status = CellStatus::kUnknown;
        std::uint32_t flags = 0;
        std::uint32_t profileCount = 0;
        // Strings and the pointer array are borrowed for the duration of a
        // callback, or until the next GetCellResult call on the same thread.
        const char* const* profiles = nullptr;
    };

    struct ClientCallbacks
    {
        // The ID is copied during registration.
        const char* id = nullptr;
        void (*OnCellClassified)(RE::TESObjectCELL*, const CellResult*) = nullptr;
    };

    struct ReferenceCallbacks
    {
        // The ID is copied during registration.
        const char* id = nullptr;
        void (*OnReferenceEmittanceChanged)(RE::TESObjectREFR*) = nullptr;
    };

    struct Interface
    {
        std::uint32_t version = kVersion;
        bool (*RegisterClient)(const ClientCallbacks*) = nullptr;
        bool (*HasWindowProfiles)() = nullptr;
        CellResult (*GetCellResult)(RE::TESObjectCELL*) = nullptr;
        bool (*RegisterReferenceClient)(const ReferenceCallbacks*) = nullptr;
        bool (*RegisterLightPlacerTransformer)(
            const LightPlacerTransformer*) = nullptr;
        bool (*RequestLightPlacerReload)() = nullptr;
    };

    constexpr bool HasFlag(
        const CellResult& a_result,
        const CellResultFlag a_flag)
    {
        return (a_result.flags & static_cast<std::uint32_t>(a_flag)) != 0;
    }

    using RequestInterface = const Interface* (*)(std::uint32_t);
}  // namespace MPL::XEMIAPI

#include <DetailedLogging.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace MPL::DetailedLogging
{
    namespace
    {
        constexpr std::string_view kSettingsPath =
            "Data/SKSE/Plugins/XEMIUtilSettings.json";

        struct Settings
        {
            bool detailedLogging = false;
        };

        std::atomic_bool enabled{ false };
    }  // namespace

    void Initialize()
    {
        std::ifstream file(
            std::filesystem::path(kSettingsPath),
            std::ios::binary);
        if (!file)
        {
            enabled.store(false, std::memory_order_relaxed);
            return;
        }
        const std::string text{
            std::istreambuf_iterator<char>{ file },
            std::istreambuf_iterator<char>{} };
        const auto parsed =
            rfl::json::read<Settings, rfl::DefaultIfMissing>(text);
        enabled.store(
            parsed && parsed.value().detailedLogging,
            std::memory_order_relaxed);
    }

    bool IsEnabled()
    {
        return enabled.load(std::memory_order_relaxed);
    }
}  // namespace MPL::DetailedLogging

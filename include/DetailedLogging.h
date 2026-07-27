#pragma once

#include <spdlog/spdlog.h>
#include <utility>

namespace MPL::DetailedLogging
{
    void Initialize();
    bool IsEnabled();

    template <class... Args>
    void Info(spdlog::format_string_t<Args...> a_format, Args&&... a_args)
    {
        if (IsEnabled())
        {
            logger::info(a_format, std::forward<Args>(a_args)...);
        }
    }
}  // namespace MPL::DetailedLogging

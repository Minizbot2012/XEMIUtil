#pragma once

#include <RE/Skyrim.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string_view>

namespace MPL::FormResolver
{
    inline bool IsHexString(std::string_view a_value)
    {
        if (a_value.starts_with("0x") || a_value.starts_with("0X"))
        {
            a_value.remove_prefix(2);
        }
        return !a_value.empty() &&
               std::ranges::all_of(a_value, [](const unsigned char a_character) {
                   return std::isxdigit(a_character) != 0;
               });
    }

    inline std::optional<RE::FormID> ParseHex(std::string_view a_value)
    {
        if (a_value.starts_with("0x") || a_value.starts_with("0X"))
        {
            a_value.remove_prefix(2);
        }
        RE::FormID value = 0;
        const auto [end, error] = std::from_chars(
            a_value.data(),
            a_value.data() + a_value.size(),
            value,
            16);
        if (error != std::errc{} ||
            end != a_value.data() + a_value.size())
        {
            return std::nullopt;
        }
        return value;
    }

    inline RE::FormID Resolve(const std::string_view a_selector)
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler || a_selector.empty())
        {
            return 0;
        }

        const auto separator = a_selector.find_first_of("~:");
        if (separator != std::string_view::npos)
        {
            const auto local = ParseHex(a_selector.substr(0, separator));
            const auto plugin = a_selector.substr(separator + 1);
            return local && !plugin.empty() ?
                       dataHandler->LookupFormID(*local, plugin) :
                       0;
        }
        if (IsHexString(a_selector))
        {
            return ParseHex(a_selector).value_or(0);
        }
        if (const auto* form = RE::TESForm::LookupByEditorID(a_selector))
        {
            return form->GetFormID();
        }
        return 0;
    }
}  // namespace MPL::FormResolver

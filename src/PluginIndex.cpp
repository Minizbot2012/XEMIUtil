#include <PluginIndex.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
#include <zlib.h>

namespace MPL::PluginIndex
{
    namespace
    {
        constexpr std::uint32_t Signature(
            const char a_first,
            const char a_second,
            const char a_third,
            const char a_fourth)
        {
            return static_cast<std::uint8_t>(a_first) |
                   static_cast<std::uint32_t>(
                       static_cast<std::uint8_t>(a_second))
                       << 8 |
                   static_cast<std::uint32_t>(
                       static_cast<std::uint8_t>(a_third))
                       << 16 |
                   static_cast<std::uint32_t>(
                       static_cast<std::uint8_t>(a_fourth))
                       << 24;
        }

        constexpr auto kGroup = Signature('G', 'R', 'U', 'P');
        constexpr auto kReference = Signature('R', 'E', 'F', 'R');
        constexpr auto kName = Signature('N', 'A', 'M', 'E');
        constexpr auto kExtendedSize = Signature('X', 'X', 'X', 'X');
        constexpr std::uint32_t kDeleted = 0x00000020;
        constexpr std::uint32_t kCompressed = 0x00040000;
        constexpr std::int32_t kCellChildren = 6;
        constexpr std::size_t kMaxStoredRecordSize =
            64ULL * 1024ULL * 1024ULL;
        constexpr std::size_t kMaxDecompressedRecordSize =
            256ULL * 1024ULL * 1024ULL;
        constexpr std::size_t kMaxPlacementsPerPlugin =
            2'000'000;
        constexpr std::size_t kMaxTotalPlacements =
            4'000'000;

#pragma pack(push, 1)
        struct RecordHeader
        {
            std::uint32_t signature;
            std::uint32_t dataSize;
            std::uint32_t flags;
            std::uint32_t formID;
            std::uint32_t revision;
            std::uint16_t version;
            std::uint16_t unknown;
        };

        struct GroupHeader
        {
            std::uint32_t signature;
            std::uint32_t size;
            std::uint32_t label;
            std::int32_t type;
            std::uint32_t stamp;
            std::uint32_t unknown;
        };
#pragma pack(pop)

        static_assert(sizeof(RecordHeader) == 24);
        static_assert(sizeof(GroupHeader) == 24);

        template <class T>
        bool Read(std::ifstream& a_stream, T& a_value)
        {
            return static_cast<bool>(
                a_stream.read(
                    reinterpret_cast<char*>(std::addressof(a_value)),
                    sizeof(T)));
        }

        template <class T>
        std::optional<T> ReadValue(
            const std::span<const std::byte> a_data,
            const std::size_t a_offset)
        {
            if (a_offset > a_data.size() ||
                sizeof(T) > a_data.size() - a_offset)
            {
                return std::nullopt;
            }
            T value{};
            std::memcpy(
                std::addressof(value),
                a_data.data() + a_offset,
                sizeof(T));
            return value;
        }

        std::optional<RE::FormID> FindBaseForm(
            const std::span<const std::byte> a_data)
        {
            std::size_t offset = 0;
            std::optional<std::uint32_t> extendedSize;
            while (offset + sizeof(std::uint32_t) + sizeof(std::uint16_t) <=
                   a_data.size())
            {
                const auto signature =
                    ReadValue<std::uint32_t>(a_data, offset);
                const auto smallSize =
                    ReadValue<std::uint16_t>(
                        a_data,
                        offset + sizeof(std::uint32_t));
                if (!signature || !smallSize)
                {
                    return std::nullopt;
                }
                offset += sizeof(std::uint32_t) + sizeof(std::uint16_t);

                std::size_t size = extendedSize ?
                                       *extendedSize :
                                       *smallSize;
                extendedSize.reset();
                if (size > a_data.size() - offset)
                {
                    return std::nullopt;
                }
                if (*signature == kExtendedSize)
                {
                    if (size != sizeof(std::uint32_t))
                    {
                        return std::nullopt;
                    }
                    extendedSize =
                        ReadValue<std::uint32_t>(a_data, offset);
                }
                else if (*signature == kName &&
                         size >= sizeof(RE::FormID))
                {
                    return ReadValue<RE::FormID>(a_data, offset);
                }
                offset += size;
            }
            return std::nullopt;
        }

        std::optional<std::vector<std::byte>> ReadRecordData(
            std::ifstream& a_stream,
            const RecordHeader& a_header,
            std::size_t& a_compressedCount)
        {
            if (a_header.dataSize > kMaxStoredRecordSize)
            {
                logger::warn(
                    "[Window Sync] Plugin index rejected a {} byte record because it exceeds the {} byte stored-data limit",
                    a_header.dataSize,
                    kMaxStoredRecordSize);
                return std::nullopt;
            }
            std::vector<std::byte> stored(a_header.dataSize);
            if (!stored.empty() &&
                !a_stream.read(
                    reinterpret_cast<char*>(stored.data()),
                    static_cast<std::streamsize>(stored.size())))
            {
                return std::nullopt;
            }
            if ((a_header.flags & kCompressed) == 0)
            {
                return stored;
            }

            ++a_compressedCount;
            if (stored.size() < sizeof(std::uint32_t))
            {
                return std::nullopt;
            }
            std::uint32_t decompressedSize = 0;
            std::memcpy(
                std::addressof(decompressedSize),
                stored.data(),
                sizeof(decompressedSize));
            if (decompressedSize == 0 ||
                decompressedSize >
                    kMaxDecompressedRecordSize)
            {
                logger::warn(
                    "[Window Sync] Plugin index rejected a {} byte decompressed record because it exceeds the supported allocation range",
                    decompressedSize);
                return std::nullopt;
            }
            std::vector<std::byte> result(decompressedSize);
            uLongf actualSize = decompressedSize;
            const auto status = uncompress(
                reinterpret_cast<Bytef*>(result.data()),
                std::addressof(actualSize),
                reinterpret_cast<const Bytef*>(
                    stored.data() + sizeof(decompressedSize)),
                static_cast<uLong>(
                    stored.size() - sizeof(decompressedSize)));
            if (status != Z_OK || actualSize != decompressedSize)
            {
                return std::nullopt;
            }
            return result;
        }

        class Parser
        {
        public:
            Parser(
                const RE::TESFile& a_file,
                Result& a_result) :
                file(a_file),
                result(a_result)
            {}

            bool Parse(const std::filesystem::path& a_path)
            {
                stream.open(a_path, std::ios::binary);
                if (!stream)
                {
                    return false;
                }
                stream.seekg(0, std::ios::end);
                const auto length = stream.tellg();
                if (length < 0)
                {
                    return false;
                }
                stream.seekg(0, std::ios::beg);
                return ParseRange(
                    static_cast<std::uint64_t>(length),
                    std::nullopt);
            }

        private:
            bool ParseRange(
                const std::uint64_t a_end,
                const std::optional<RE::FormID> a_cell)
            {
                while (true)
                {
                    const auto position = stream.tellg();
                    if (position < 0)
                    {
                        return false;
                    }
                    const auto start =
                        static_cast<std::uint64_t>(position);
                    if (start == a_end)
                    {
                        return true;
                    }
                    if (start > a_end || a_end - start < sizeof(std::uint32_t))
                    {
                        return false;
                    }

                    std::uint32_t signature = 0;
                    if (!Read(stream, signature))
                    {
                        return false;
                    }
                    stream.seekg(
                        static_cast<std::streamoff>(start),
                        std::ios::beg);

                    if (signature == kGroup)
                    {
                        if (a_end - start < sizeof(GroupHeader))
                        {
                            return false;
                        }
                        GroupHeader group{};
                        if (!Read(stream, group) ||
                            group.size < sizeof(GroupHeader) ||
                            group.size > a_end - start)
                        {
                            return false;
                        }
                        auto cell = a_cell;
                        if (group.type == kCellChildren)
                        {
                            cell = group.label;
                        }
                        const auto groupEnd = start + group.size;
                        if (!ParseRange(groupEnd, cell))
                        {
                            return false;
                        }
                        stream.seekg(
                            static_cast<std::streamoff>(groupEnd),
                            std::ios::beg);
                        continue;
                    }

                    if (a_end - start < sizeof(RecordHeader))
                    {
                        return false;
                    }
                    RecordHeader header{};
                    if (!Read(stream, header) ||
                        header.dataSize > a_end - start - sizeof(header))
                    {
                        return false;
                    }
                    const auto recordEnd =
                        start + sizeof(header) + header.dataSize;
                    if (header.signature == kReference && a_cell)
                    {
                        ++result.referencesRead;
                        const auto data = ReadRecordData(
                            stream,
                            header,
                            result.compressedReferences);
                        if (!data)
                        {
                            return false;
                        }
                        const auto reference =
                            file.GetRuntimeFormID(header.formID);
                        const auto cell =
                            file.GetRuntimeFormID(*a_cell);
                        if (reference && cell)
                        {
                            if (!result.placements.contains(reference) &&
                                result.placements.size() >=
                                    kMaxPlacementsPerPlugin)
                            {
                                logger::warn(
                                    "[Window Sync] Plugin index stopped after {} unique placements in '{}'",
                                    kMaxPlacementsPerPlugin,
                                    file.GetFilename());
                                return false;
                            }
                            auto& placement =
                                result.placements[reference];
                            placement.reference = reference;
                            placement.cell = cell;
                            placement.deleted =
                                (header.flags & kDeleted) != 0;
                            if (const auto base = FindBaseForm(*data);
                                base && *base)
                            {
                                placement.base =
                                    file.GetRuntimeFormID(*base);
                            }
                        }
                    }
                    stream.seekg(
                        static_cast<std::streamoff>(recordEnd),
                        std::ios::beg);
                }
            }

            const RE::TESFile& file;
            Result& result;
            std::ifstream stream;
        };

        std::filesystem::path PluginPath(const RE::TESFile& a_file)
        {
            const auto filename = std::string(a_file.GetFilename());
            const std::array candidates{
                std::filesystem::path("Data") / filename,
                std::filesystem::path(filename),
                std::filesystem::path(a_file.path) / filename,
            };
            for (const auto& candidate : candidates)
            {
                std::error_code error;
                if (std::filesystem::is_regular_file(candidate, error))
                {
                    return candidate;
                }
            }
            return candidates.front();
        }

        std::vector<const RE::TESFile*> LoadedPlugins()
        {
            std::vector<const RE::TESFile*> plugins;
            const auto* dataHandler =
                RE::TESDataHandler::GetSingleton();
            if (!dataHandler)
            {
                return plugins;
            }
            std::unordered_set<const RE::TESFile*> seen;
            const auto append = [&](const RE::TESFile* const* a_files,
                                    const std::size_t a_count) {
                if (!a_files)
                {
                    return;
                }
                for (std::size_t index = 0; index < a_count; ++index)
                {
                    auto* file = a_files[index];
                    if (file && seen.insert(file).second)
                    {
                        plugins.push_back(file);
                    }
                }
            };
            append(
                dataHandler->GetLoadedMods(),
                dataHandler->GetLoadedModCount());
            append(
                dataHandler->GetLoadedLightMods(),
                dataHandler->GetLoadedLightModCount());
            return plugins;
        }
    }  // namespace

    Result Build()
    {
        Result result;
        const auto plugins = LoadedPlugins();
        result.pluginsDiscovered = plugins.size();
        for (const auto* plugin : plugins)
        {
            if (!plugin)
            {
                continue;
            }
            const auto path = PluginPath(*plugin);
            try
            {
                Result parsed;
                Parser parser(*plugin, parsed);
                if (!parser.Parse(path))
                {
                    logger::warn(
                        "[Window Sync] Plugin index could not parse '{}' from '{}'; "
                        "runtime fallback remains enabled",
                        plugin->GetFilename(),
                        path.string());
                    continue;
                }
                bool totalLimitReached = false;
                for (auto& [formID, placement] :
                     parsed.placements)
                {
                    if (!result.placements.contains(formID) &&
                        result.placements.size() >=
                            kMaxTotalPlacements)
                    {
                        totalLimitReached = true;
                        break;
                    }
                    result.placements.insert_or_assign(
                        formID,
                        std::move(placement));
                }
                if (totalLimitReached)
                {
                    logger::error(
                        "[Window Sync] Plugin index reached the {} placement session limit while merging '{}'; runtime fallback remains enabled",
                        kMaxTotalPlacements,
                        plugin->GetFilename());
                    break;
                }
                result.referencesRead +=
                    parsed.referencesRead;
                result.compressedReferences +=
                    parsed.compressedReferences;
                ++result.pluginsParsed;
            }
            catch (const std::bad_alloc&)
            {
                logger::error(
                    "[Window Sync] Plugin index exhausted its allocation budget while parsing '{}'; runtime fallback remains enabled",
                    plugin->GetFilename());
            }
            catch (const std::length_error& error)
            {
                logger::error(
                    "[Window Sync] Plugin index rejected an oversized allocation while parsing '{}': {}",
                    plugin->GetFilename(),
                    error.what());
            }
        }
        result.complete =
            result.pluginsParsed == result.pluginsDiscovered;
        return result;
    }
}  // namespace MPL::PluginIndex

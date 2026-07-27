#include <Config.h>
#include <DetailedLogging.h>
#include <LPPatch.h>
#include <PluginIndex.h>
#include <WindowSync.h>
#include <XEMI_API.h>
#include <algorithm>
#include <atomic>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MPL::WindowSync
{
    namespace
    {
        constexpr std::size_t kMaxReportedProfiles = 4096;

        struct CellResultData
        {
            XEMIAPI::CellStatus status =
                XEMIAPI::CellStatus::kUnknown;
            std::vector<std::string> profiles;
            bool profilesTruncated = false;
        };

        struct CellResultSnapshot
        {
            std::vector<std::string> profiles;
            std::vector<const char*> profilePointers;
            XEMIAPI::CellResult view;

            void Assign(const CellResultData& a_result)
            {
                profiles = a_result.profiles;
                profilePointers.clear();
                profilePointers.reserve(profiles.size());
                for (const auto& profile : profiles)
                {
                    profilePointers.push_back(profile.c_str());
                }
                view = {
                    .status = a_result.status,
                    .flags = a_result.profilesTruncated ?
                                 static_cast<std::uint32_t>(
                                     XEMIAPI::CellResultFlag::
                                         kProfilesTruncated) :
                                 0,
                    .profileCount =
                        static_cast<std::uint32_t>(
                            profilePointers.size()),
                    .profiles = profilePointers.empty() ?
                                    nullptr :
                                    profilePointers.data(),
                };
            }
        };

        struct RegisteredClient
        {
            std::string id;
            void (*OnCellClassified)(
                RE::TESObjectCELL*,
                const XEMIAPI::CellResult*) = nullptr;
        };

        struct CellState
        {
            std::unordered_set<std::size_t> matchedEntries;
            CellResultData result;
            bool complete = false;
        };

        std::mutex stateLock;
        std::unordered_map<RE::FormID, CellState> cells;
        std::unordered_map<RE::FormID, CellState> startupCells;
        std::unordered_map<RE::FormID, std::size_t> referencePlans;
        std::vector<RE::FormID> indexedReferences;
        std::vector<RegisteredClient> clients;
        std::atomic_bool startupInitialized{ false };
        std::atomic_bool pluginIndexComplete{ false };

        bool IsSupportedReference(const RE::TESObjectREFR* a_reference)
        {
            const auto* object = a_reference ? a_reference->GetObjectReference() : nullptr;
            return object &&
                   (object->Is(RE::FormType::MovableStatic) ||
                       object->Is(RE::FormType::Static) ||
                       object->Is(RE::FormType::Light));
        }

        bool IsSupportedBase(const RE::FormID a_formID)
        {
            const auto* object =
                a_formID ? RE::TESForm::LookupByID(a_formID) : nullptr;
            return object &&
                   (object->Is(RE::FormType::MovableStatic) ||
                       object->Is(RE::FormType::Static) ||
                       object->Is(RE::FormType::Light));
        }

        bool MatchesForms(
            const RE::FormID a_reference,
            const RE::FormID a_base,
            const std::unordered_set<Config::LiteForm>& a_forms,
            const bool a_formsAreBase)
        {
            if (a_forms.contains(Config::LiteForm::FromID(a_reference)))
            {
                return true;
            }
            return a_formsAreBase && a_base &&
                   a_forms.contains(Config::LiteForm::FromID(a_base));
        }

        bool MatchesForms(
            const RE::TESObjectREFR* a_reference,
            const std::unordered_set<Config::LiteForm>& a_forms,
            const bool a_formsAreBase)
        {
            if (!a_reference)
            {
                return false;
            }
            if (a_forms.contains(Config::LiteForm::FromID(a_reference->GetFormID())))
            {
                return true;
            }
            if (!a_formsAreBase)
            {
                return false;
            }
            const auto* base = a_reference->GetBaseObject();
            return base && a_forms.contains(Config::LiteForm::FromID(base->GetFormID()));
        }

        const Config::CellContainsSettings* CellContainsForEntry(
            const Config::ConfigEntry& a_entry)
        {
            if (a_entry.cellContains)
            {
                return std::addressof(*a_entry.cellContains);
            }
            return a_entry.windows ? std::addressof(*a_entry.windows) : nullptr;
        }

        bool CellExcluded(
            const Config::ConfigEntry& a_entry,
            const RE::TESObjectCELL* a_cell)
        {
            return a_cell && a_entry.excluded_cells &&
                   a_entry.excluded_cells->contains(
                       Config::LiteForm::FromID(a_cell->GetFormID()));
        }

        std::vector<RE::TESObjectREFR*> CellReferences(RE::TESObjectCELL* a_cell)
        {
            std::vector<RE::TESObjectREFR*> references;
            if (!a_cell)
            {
                return references;
            }
            const auto& source = a_cell->GetRuntimeData().references;
            references.reserve(source.size());
            for (const auto& reference : source)
            {
                if (reference)
                {
                    references.push_back(reference.get());
                }
            }
            return references;
        }

        std::unordered_set<std::size_t> FindCellContainsEntries(
            RE::TESObjectCELL* a_cell,
            const std::vector<RE::TESObjectREFR*>& a_references)
        {
            std::unordered_set<std::size_t> matches;
            const auto& entries = Config::StatData::GetSingleton()->entries;
            for (std::size_t index = 0; index < entries.size(); ++index)
            {
                const auto& entry = entries[index];
                const auto* cellContains = CellContainsForEntry(entry);
                if (!cellContains || cellContains->forms.empty() ||
                    CellExcluded(entry, a_cell))
                {
                    continue;
                }
                if (std::ranges::any_of(a_references, [&](const auto& a_reference) {
                        return IsSupportedReference(a_reference) &&
                               MatchesForms(
                            a_reference,
                            cellContains->forms,
                            cellContains->forms_are_base.value_or(true));
                    }))
                {
                    matches.insert(index);
                }
            }
            return matches;
        }

        std::unordered_set<std::size_t> FindCellContainsEntries(
            RE::TESObjectCELL* a_cell,
            const std::vector<const PluginIndex::Placement*>& a_references)
        {
            std::unordered_set<std::size_t> matches;
            const auto& entries = Config::StatData::GetSingleton()->entries;
            for (std::size_t index = 0; index < entries.size(); ++index)
            {
                const auto& entry = entries[index];
                const auto* cellContains = CellContainsForEntry(entry);
                if (!cellContains || cellContains->forms.empty() ||
                    CellExcluded(entry, a_cell))
                {
                    continue;
                }
                if (std::ranges::any_of(a_references, [&](const auto* a_reference) {
                        return a_reference &&
                               MatchesForms(
                                   a_reference->reference,
                                   a_reference->base,
                                   cellContains->forms,
                                   cellContains->forms_are_base.value_or(true));
                    }))
                {
                    matches.insert(index);
                }
            }
            return matches;
        }

        std::vector<std::string_view> ProfileNames(
            const Config::CellContainsSettings& a_settings)
        {
            std::vector<std::string_view> profiles;
            if (a_settings.profile && !a_settings.profile->empty())
            {
                profiles.push_back(*a_settings.profile);
            }
            if (a_settings.profiles)
            {
                for (const auto& profile : *a_settings.profiles)
                {
                    if (!profile.empty())
                    {
                        profiles.push_back(profile);
                    }
                }
            }
            return profiles;
        }

        CellResultData MakeResult(
            const XEMIAPI::CellStatus a_status,
            const std::vector<std::string>& a_profiles = {})
        {
            CellResultData result;
            result.status = a_status;
            for (const auto& profile : a_profiles)
            {
                if (profile.empty())
                {
                    continue;
                }
                if (result.profiles.size() >=
                    kMaxReportedProfiles)
                {
                    result.profilesTruncated = true;
                    continue;
                }
                result.profiles.push_back(profile);
            }
            if (result.profilesTruncated)
            {
                logger::warn(
                    "[Window Sync] Cell classification profile list exceeded {} entries and was truncated",
                    kMaxReportedProfiles);
            }
            return result;
        }

        bool SameResult(
            const CellResultData& a_left,
            const CellResultData& a_right)
        {
            if (a_left.status != a_right.status ||
                a_left.profilesTruncated !=
                    a_right.profilesTruncated)
            {
                return false;
            }
            return a_left.profiles == a_right.profiles;
        }

        CellResultData ResultForMatches(
            const std::unordered_set<std::size_t>& a_matches)
        {
            const auto& entries = Config::StatData::GetSingleton()->entries;
            std::vector<std::string> profiles;
            for (const auto entryIndex : a_matches)
            {
                if (entryIndex >= entries.size())
                {
                    continue;
                }
                const auto* cellContains = CellContainsForEntry(entries[entryIndex]);
                if (!cellContains)
                {
                    continue;
                }
                for (const auto profile : ProfileNames(*cellContains))
                {
                    profiles.emplace_back(profile);
                }
            }
            std::ranges::sort(profiles);
            profiles.erase(
                std::ranges::unique(profiles).begin(),
                profiles.end());
            if (!profiles.empty())
            {
                return MakeResult(XEMIAPI::CellStatus::kMatched, profiles);
            }
            return MakeResult(XEMIAPI::CellStatus::kNoMatch);
        }

        bool DetailedLoggingEnabled()
        {
            return DetailedLogging::IsEnabled();
        }

        void NotifyClient(
            const RegisteredClient& a_callback,
            RE::TESObjectCELL* a_cell,
            const CellResultData& a_result)
        {
            try
            {
                if (a_callback.OnCellClassified)
                {
                    CellResultSnapshot snapshot;
                    snapshot.Assign(a_result);
                    a_callback.OnCellClassified(
                        a_cell,
                        std::addressof(snapshot.view));
                }
            }
            catch (const std::exception& error)
            {
                logger::error(
                    "[Window Sync] XEMI API client '{}' raised an exception during OnCellClassified: {}",
                    a_callback.id,
                    error.what());
            }
            catch (...)
            {
                logger::error(
                    "[Window Sync] XEMI API client '{}' raised an unknown exception during OnCellClassified",
                    a_callback.id);
            }
        }

        void NotifyClients(
            RE::TESObjectCELL* a_cell,
            const CellResultData& a_result)
        {
            std::vector<RegisteredClient> callbacks;
            try
            {
                std::scoped_lock lock(stateLock);
                callbacks = clients;
            }
            catch (const std::exception& error)
            {
                logger::error(
                    "[Window Sync] Could not copy XEMI API clients for notification: {}",
                    error.what());
                return;
            }
            catch (...)
            {
                logger::error(
                    "[Window Sync] Could not copy XEMI API clients for notification because an unknown exception occurred");
                return;
            }
            for (const auto& callback : callbacks)
            {
                NotifyClient(callback, a_cell, a_result);
            }
        }

        bool UpdateCellState(
            RE::TESObjectCELL* a_cell,
            const std::unordered_set<std::size_t>& a_matches,
            const CellResultData& a_result)
        {
            std::scoped_lock lock(stateLock);
            auto& state = cells[a_cell->GetFormID()];
            const bool changed = !state.complete || !SameResult(state.result, a_result);
            state.matchedEntries = a_matches;
            state.result = a_result;
            state.complete = true;
            return changed;
        }

        bool CellAllowsEntry(
            const Config::ConfigEntry& a_entry,
            const std::size_t a_entryIndex,
            RE::TESObjectCELL* a_cell,
            const std::unordered_set<std::size_t>& a_cellContainsMatches)
        {
            if (CellExcluded(a_entry, a_cell))
            {
                return false;
            }
            const bool hasCellFilter = a_entry.allowed_cells.has_value();
            const bool hasContainsFilter = CellContainsForEntry(a_entry) != nullptr;
            if (!hasCellFilter && !hasContainsFilter)
            {
                return true;
            }

            const bool explicitlyAllowed =
                hasCellFilter && a_cell &&
                a_entry.allowed_cells->contains(Config::LiteForm::FromID(a_cell->GetFormID()));
            const bool containsAllowed =
                hasContainsFilter && a_cellContainsMatches.contains(a_entryIndex);
            return explicitlyAllowed || containsAllowed;
        }

        bool MatchesEntryTarget(
            RE::TESObjectREFR* a_reference,
            const Config::ConfigEntry& a_entry)
        {
            if (MatchesForms(
                    a_reference,
                    a_entry.forms,
                    a_entry.forms_are_base.value_or(false)))
            {
                return true;
            }
            const auto* cellContains = CellContainsForEntry(a_entry);
            return cellContains && cellContains->apply_xemi.value_or(false) &&
                   MatchesForms(
                       a_reference,
                       cellContains->forms,
                       cellContains->forms_are_base.value_or(true));
        }

        bool MatchesEntryTarget(
            const PluginIndex::Placement& a_reference,
            const Config::ConfigEntry& a_entry)
        {
            if (MatchesForms(
                    a_reference.reference,
                    a_reference.base,
                    a_entry.forms,
                    a_entry.forms_are_base.value_or(false)))
            {
                return true;
            }
            const auto* cellContains = CellContainsForEntry(a_entry);
            return cellContains && cellContains->apply_xemi.value_or(false) &&
                   MatchesForms(
                       a_reference.reference,
                       a_reference.base,
                       cellContains->forms,
                       cellContains->forms_are_base.value_or(true));
        }

        const Config::ConfigEntry* MatchingEntry(
            RE::TESObjectREFR* a_reference,
            RE::TESObjectCELL* a_cell,
            const std::unordered_set<std::size_t>& a_cellContainsMatches)
        {
            const auto& entries = Config::StatData::GetSingleton()->entries;
            for (std::size_t index = entries.size(); index > 0; --index)
            {
                const auto entryIndex = index - 1;
                const auto& entry = entries[entryIndex];
                if (!CellAllowsEntry(
                        entry,
                        entryIndex,
                        a_cell,
                        a_cellContainsMatches))
                {
                    continue;
                }
                if (entry.only_interior.value_or(false) &&
                    (!a_cell || !a_cell->IsInteriorCell()))
                {
                    continue;
                }
                if (MatchesEntryTarget(a_reference, entry))
                {
                    return std::addressof(entry);
                }
            }
            return nullptr;
        }

        std::optional<std::size_t> MatchingEntry(
            const PluginIndex::Placement& a_reference,
            RE::TESObjectCELL* a_cell,
            const std::unordered_set<std::size_t>& a_cellContainsMatches)
        {
            const auto& entries = Config::StatData::GetSingleton()->entries;
            for (std::size_t index = entries.size(); index > 0; --index)
            {
                const auto entryIndex = index - 1;
                const auto& entry = entries[entryIndex];
                if (!CellAllowsEntry(
                        entry,
                        entryIndex,
                        a_cell,
                        a_cellContainsMatches))
                {
                    continue;
                }
                if (entry.only_interior.value_or(false) &&
                    (!a_cell || !a_cell->IsInteriorCell()))
                {
                    continue;
                }
                if (MatchesEntryTarget(a_reference, entry))
                {
                    return entryIndex;
                }
            }
            return std::nullopt;
        }

        bool ApplyEntry(RE::TESObjectREFR* a_reference, const Config::ConfigEntry& a_entry)
        {
            if (a_entry.xemi.formID)
            {
                auto* source = a_entry.xemi.Get<RE::TESForm>();
                if (!source)
                {
                    return false;
                }
                if (auto* existing = a_reference->extraList.GetByType<RE::ExtraEmittanceSource>())
                {
                    if (existing->source == source)
                    {
                        return false;
                    }
                    existing->source = source;
                }
                else
                {
                    auto* extra = RE::BSExtraData::Create<RE::ExtraEmittanceSource>();
                    extra->source = source;
                    a_reference->extraList.Add(extra);
                }
                DetailedLogging::Info(
                    "[Window Sync] Applied XEMI {:08X} to reference {:08X}",
                    source->GetFormID(),
                    a_reference->GetFormID());
                return true;
            }
            else if (a_entry.remove.value_or(false) &&
                     a_reference->extraList.HasType<RE::ExtraEmittanceSource>())
            {
                a_reference->extraList.RemoveByType(RE::ExtraDataType::kEmittanceSource);
                return true;
            }
            return false;
        }

        bool ApplyReference(
            RE::TESObjectREFR* a_reference,
            const std::unordered_set<std::size_t>& a_cellContainsMatches)
        {
            if (!IsSupportedReference(a_reference))
            {
                return false;
            }
            auto* cell = a_reference->GetParentCell();
            if (const auto* entry =
                    MatchingEntry(a_reference, cell, a_cellContainsMatches))
            {
                if (entry->only_interior.value_or(false) &&
                    (!cell || !cell->IsInteriorCell()))
                {
                    return false;
                }
                return ApplyEntry(a_reference, *entry);
            }
            return false;
        }

        std::unordered_set<std::size_t> CellContainsMatchesForCell(
            RE::TESObjectCELL* a_cell)
        {
            if (!a_cell)
            {
                return {};
            }
            std::scoped_lock lock(stateLock);
            if (const auto found = cells.find(a_cell->GetFormID()); found != cells.end())
            {
                return found->second.matchedEntries;
            }
            return {};
        }

        bool RecordReferenceMatches(RE::TESObjectREFR* a_reference)
        {
            auto* cell = a_reference ? a_reference->GetParentCell() : nullptr;
            if (!cell || !cell->IsInteriorCell())
            {
                return false;
            }

            bool changed = false;
            const auto& entries = Config::StatData::GetSingleton()->entries;
            std::scoped_lock lock(stateLock);
            auto& cellState = cells[cell->GetFormID()];
            for (std::size_t index = 0; index < entries.size(); ++index)
            {
                const auto& entry = entries[index];
                const auto* cellContains = CellContainsForEntry(entry);
                if (cellContains && !CellExcluded(entry, cell) &&
                    MatchesForms(
                        a_reference,
                        cellContains->forms,
                        cellContains->forms_are_base.value_or(true)))
                {
                    changed |= cellState.matchedEntries.insert(index).second;
                }
            }
            return changed;
        }

        bool HasRuntimeCellMatches(RE::TESObjectCELL* a_cell)
        {
            if (!a_cell)
            {
                return false;
            }

            std::scoped_lock lock(stateLock);
            const auto current = cells.find(a_cell->GetFormID());
            if (current == cells.end())
            {
                return false;
            }
            const auto startup = startupCells.find(a_cell->GetFormID());
            return startup == startupCells.end() ||
                   current->second.matchedEntries != startup->second.matchedEntries;
        }

        std::vector<LPPatch::PatchRule> BuildLightPlacerRules(
            const std::unordered_map<
                RE::TESObjectCELL*,
                std::vector<const PluginIndex::Placement*>>& a_placementsByCell,
            const std::unordered_map<RE::FormID, CellState>& a_cellStates)
        {
            std::vector<LPPatch::PatchRule> rules;
            const auto& entries = Config::StatData::GetSingleton()->entries;
            for (std::size_t entryIndex = 0;
                 entryIndex < entries.size();
                 ++entryIndex)
            {
                const auto& entry = entries[entryIndex];
                const auto* cellContains = CellContainsForEntry(entry);
                if (!entry.lpLight || entry.lpLight->empty() || !entry.xemi.formID ||
                    entry.xemi.selector.empty() || !cellContains ||
                    cellContains->forms.empty())
                {
                    continue;
                }

                LPPatch::PatchRule rule{
                    .lights = *entry.lpLight,
                    .externalEmittance = entry.xemi.selector,
                    .detailedLogging = DetailedLogging::IsEnabled(),
                };
                for (const auto& [cell, placements] : a_placementsByCell)
                {
                    if (!cell || !cell->IsInteriorCell() ||
                        CellExcluded(entry, cell))
                    {
                        continue;
                    }
                    const auto state =
                        a_cellStates.find(cell->GetFormID());
                    if (state == a_cellStates.end() ||
                        !state->second.matchedEntries.contains(entryIndex))
                    {
                        continue;
                    }

                    for (const auto* placement : placements)
                    {
                        if (!placement || placement->deleted || !placement->base)
                        {
                            continue;
                        }

                        const auto* base =
                            RE::TESForm::LookupByID<RE::TESBoundObject>(
                                placement->base);
                        const auto* model =
                            base ? base->As<RE::TESModel>() : nullptr;
                        const auto path = model ?
                                              LPPatch::NormalizeModelPath(
                                                  model->GetModel()) :
                                              std::string{};

                        LPPatch::SourcePlacement source{
                            .reference = placement->reference,
                            .base = placement->base,
                            .cell = cell->GetFormID(),
                            .model = path,
                            .filterIDs = {
                                cell->GetFormID(),
                                placement->reference,
                                placement->base,
                            },
                        };
                        for (auto* location = cell->GetLocation();
                             location;
                             location = location->parentLoc)
                        {
                            source.filterIDs.push_back(location->GetFormID());
                        }
                        rule.placements.push_back(std::move(source));
                    }
                }
                if (!rule.externalEmittance.empty() &&
                    !rule.placements.empty())
                {
                    rules.push_back(std::move(rule));
                }
            }
            return rules;
        }

        void RunStartupPatch()
        {
            auto* data = Config::StatData::GetSingleton();
            data->LoadConfig();
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!dataHandler)
            {
                logger::error(
                    "[Window Sync] Startup patching failed because TESDataHandler is unavailable");
                return;
            }

            auto index = PluginIndex::Build();
            std::unordered_map<
                RE::TESObjectCELL*,
                std::vector<const PluginIndex::Placement*>>
                placementsByCell;
            std::vector<RE::FormID> knownReferences;
            knownReferences.reserve(index.placements.size());

            std::unordered_map<RE::FormID, CellState> preclassifiedCells;
            const auto& allCells =
                dataHandler->GetFormArray<RE::TESObjectCELL>();
            for (auto* cell : allCells)
            {
                if (cell && cell->IsInteriorCell())
                {
                    preclassifiedCells.emplace(
                        cell->GetFormID(),
                        CellState{
                            .result = MakeResult(XEMIAPI::CellStatus::kNoMatch),
                            .complete = true,
                        });
                }
            }

            std::size_t runtimeReferences = 0;
            std::size_t runtimeBaseOverrides = 0;
            for (auto& [formID, placement] : index.placements)
            {
                if (!placement.deleted)
                {
                    if (auto* reference =
                            RE::TESForm::LookupByID<RE::TESObjectREFR>(formID))
                    {
                        if (const auto* base = reference->GetBaseObject())
                        {
                            ++runtimeReferences;
                            const auto runtimeBase = base->GetFormID();
                            if (runtimeBase && runtimeBase != placement.base)
                            {
                                DetailedLogging::Info(
                                    "[Window Sync] Reconciled runtime base for reference "
                                    "{:08X} in cell {:08X}: {:08X} -> {:08X}",
                                    formID,
                                    placement.cell,
                                    placement.base,
                                    runtimeBase);
                                placement.base = runtimeBase;
                                ++runtimeBaseOverrides;
                            }
                        }
                    }
                }
                if (placement.deleted || !placement.base ||
                    !IsSupportedBase(placement.base))
                {
                    continue;
                }
                auto* cell =
                    RE::TESForm::LookupByID<RE::TESObjectCELL>(placement.cell);
                if (!cell)
                {
                    continue;
                }
                knownReferences.push_back(formID);
                placementsByCell[cell].push_back(
                    std::addressof(placement));
            }

            std::size_t matchedCells = 0;
            std::vector<std::pair<RE::TESObjectCELL*, CellResultData>>
                notifications;
            for (const auto& [cell, placements] : placementsByCell)
            {
                if (!cell->IsInteriorCell())
                {
                    continue;
                }
                auto matches =
                    FindCellContainsEntries(cell, placements);
                const auto result = ResultForMatches(matches);
                auto& state = preclassifiedCells[cell->GetFormID()];
                state.matchedEntries = std::move(matches);
                state.result = result;
                state.complete = true;
                if (result.status == XEMIAPI::CellStatus::kMatched)
                {
                    ++matchedCells;
                    notifications.emplace_back(cell, result);
                }
            }

            std::unordered_map<RE::FormID, std::size_t> plans;
            std::size_t changedReferences = 0;
            static const std::unordered_set<std::size_t> noMatches;
            for (const auto& [cell, placements] : placementsByCell)
            {
                const auto state =
                    preclassifiedCells.find(cell->GetFormID());
                const auto& matches =
                    state != preclassifiedCells.end() ?
                        state->second.matchedEntries :
                        noMatches;
                for (const auto* placement : placements)
                {
                    if (!placement)
                    {
                        continue;
                    }
                    const auto entry = MatchingEntry(
                        *placement,
                        cell,
                        matches);
                    if (!entry)
                    {
                        continue;
                    }
                    plans.insert_or_assign(
                        placement->reference,
                        *entry);
                    if (auto* reference =
                            RE::TESForm::LookupByID<RE::TESObjectREFR>(
                                placement->reference))
                    {
                        changedReferences +=
                            ApplyEntry(reference, data->entries[*entry]) ?
                                1 :
                                0;
                    }
                }
            }

            std::ranges::sort(knownReferences);
            knownReferences.erase(
                std::ranges::unique(knownReferences).begin(),
                knownReferences.end());

            {
                std::scoped_lock lock(stateLock);
                cells = std::move(preclassifiedCells);
                startupCells = cells;
                referencePlans = std::move(plans);
                indexedReferences = std::move(knownReferences);
            }
            pluginIndexComplete.store(
                index.complete,
                std::memory_order_release);
            startupInitialized.store(true, std::memory_order_release);

            logger::info(
                "[Window Sync] Startup plugin index completed: plugins={}/{}, "
                "REFR records={}, compressed REFR records={}, winning placements={}, "
                "runtime references={}, runtime base overrides={}, "
                "supported references={}, interior cells classified={}, "
                "cellContains matches={}, direct XEMI plans={}, initialized references "
                "changed={}, index={}",
                index.pluginsParsed,
                index.pluginsDiscovered,
                index.referencesRead,
                index.compressedReferences,
                index.placements.size(),
                runtimeReferences,
                runtimeBaseOverrides,
                indexedReferences.size(),
                startupCells.size(),
                matchedCells,
                referencePlans.size(),
                changedReferences,
                index.complete ? "complete" : "partial");

            for (const auto& [cell, result] : notifications)
            {
                NotifyClients(cell, result);
            }
            LPPatch::QueueStartupPatch(
                BuildLightPlacerRules(placementsByCell, startupCells));
        }

        bool RegisterClient(const XEMIAPI::ClientCallbacks* a_callbacks)
        {
            if (!a_callbacks || !a_callbacks->id ||
                !*a_callbacks->id ||
                !a_callbacks->OnCellClassified)
            {
                return false;
            }
            std::vector<std::pair<RE::TESObjectCELL*, CellResultData>>
                replay;
            RegisteredClient client;
            try
            {
                client = {
                    .id = a_callbacks->id,
                    .OnCellClassified =
                        a_callbacks->OnCellClassified,
                };
                std::scoped_lock lock(stateLock);
                const auto existing = std::ranges::find_if(
                    clients,
                    [&](const RegisteredClient& a_registered)
                    {
                        return a_registered.id == client.id;
                    });
                if (existing != clients.end())
                {
                    return existing->OnCellClassified ==
                               client.OnCellClassified;
                }
                clients.push_back(client);
                replay.reserve(cells.size());
                for (const auto& [formID, state] : cells)
                {
                    if (state.complete &&
                        state.result.status ==
                            XEMIAPI::CellStatus::kMatched)
                    {
                        if (auto* cell =
                                RE::TESForm::LookupByID<
                                    RE::TESObjectCELL>(
                                    formID))
                        {
                            replay.emplace_back(
                                cell,
                                state.result);
                        }
                    }
                }
            }
            catch (const std::exception& error)
            {
                logger::error(
                    "[Window Sync] Could not register XEMI API client '{}': {}",
                    a_callbacks->id,
                    error.what());
                return false;
            }
            catch (...)
            {
                logger::error(
                    "[Window Sync] Could not register XEMI API client '{}' because an unknown exception occurred",
                    a_callbacks->id);
                return false;
            }
            for (const auto& [cell, result] : replay)
            {
                NotifyClient(client, cell, result);
            }
            return true;
        }

        bool HasWindowProfiles()
        {
            try
            {
                auto* data = Config::StatData::GetSingleton();
                data->LoadConfig();
                return std::ranges::any_of(
                    data->entries,
                    [](const Config::ConfigEntry& a_entry)
                    {
                        const auto* cellContains =
                            CellContainsForEntry(a_entry);
                        if (!cellContains ||
                            cellContains->forms.empty())
                        {
                            return false;
                        }
                        return std::ranges::any_of(
                            ProfileNames(*cellContains),
                            [](const std::string_view a_profile)
                            {
                                return !a_profile.empty();
                            });
                    });
            }
            catch (const std::exception& error)
            {
                logger::error(
                    "[Window Sync] HasWindowProfiles failed: {}",
                    error.what());
            }
            catch (...)
            {
                logger::error(
                    "[Window Sync] HasWindowProfiles failed with an unknown exception");
            }
            return false;
        }

        CellResultData GetCellResultData(
            RE::TESObjectCELL* a_cell)
        {
            if (!a_cell || !a_cell->IsInteriorCell())
            {
                return MakeResult(XEMIAPI::CellStatus::kNoMatch);
            }
            {
                std::scoped_lock lock(stateLock);
                if (const auto found = cells.find(a_cell->GetFormID());
                    found != cells.end() && found->second.complete)
                {
                    return found->second.result;
                }
            }
            if (a_cell->IsAttached())
            {
                ProcessCell(a_cell);
                std::scoped_lock lock(stateLock);
                if (const auto found = cells.find(a_cell->GetFormID()); found != cells.end())
                {
                    return found->second.result;
                }
            }
            return MakeResult(XEMIAPI::CellStatus::kUnknown);
        }

        XEMIAPI::CellResult GetCellResult(
            RE::TESObjectCELL* a_cell)
        {
            thread_local CellResultSnapshot snapshot;
            try
            {
                snapshot.Assign(GetCellResultData(a_cell));
                return snapshot.view;
            }
            catch (const std::exception& error)
            {
                logger::error(
                    "[Window Sync] GetCellResult failed: {}",
                    error.what());
            }
            catch (...)
            {
                logger::error(
                    "[Window Sync] GetCellResult failed with an unknown exception");
            }
            snapshot.Assign(
                MakeResult(XEMIAPI::CellStatus::kUnknown));
            return snapshot.view;
        }

        const XEMIAPI::Interface api{
            .version = XEMIAPI::kVersion,
            .RegisterClient = RegisterClient,
            .HasWindowProfiles = HasWindowProfiles,
            .GetCellResult = GetCellResult,
            .RegisterLightPlacerTransformer =
                LPPatch::RegisterTransformer,
            .RequestLightPlacerReload = LPPatch::RequestReload,
        };

        void OnMessage(SKSE::MessagingInterface::Message* a_message)
        {
            if (!a_message)
            {
                return;
            }
            switch (a_message->type)
            {
            case SKSE::MessagingInterface::kDataLoaded:
                RunStartupPatch();
                logger::info(
                    "[Window Sync] Data-loaded initialization: cellContains profiles={}",
                    HasWindowProfiles() ? "available" : "none");
                break;
            case SKSE::MessagingInterface::kPreLoadGame:
            case SKSE::MessagingInterface::kNewGame:
                ResetRuntimeState();
                break;
            default:
                break;
            }
        }
    }  // namespace

    void Install()
    {
        if (auto* messaging = SKSE::GetMessagingInterface())
        {
            messaging->RegisterListener(OnMessage);
        }
    }

    void ProcessReference(RE::TESObjectREFR* a_reference)
    {
        if (!IsSupportedReference(a_reference))
        {
            return;
        }
        if (!startupInitialized.load(std::memory_order_acquire))
        {
            return;
        }
        auto* data = Config::StatData::GetSingleton();
        data->LoadConfig();

        const auto formID = a_reference->GetFormID();
        const auto plan = referencePlans.find(formID);
        const bool hasPlan =
            plan != referencePlans.end() && plan->second < data->entries.size();
        const bool indexed =
            std::ranges::binary_search(indexedReferences, formID);

        auto* cell = a_reference->GetParentCell();
        const bool newWindowMatch = RecordReferenceMatches(a_reference);
        const auto matches = CellContainsMatchesForCell(cell);
        if (newWindowMatch || HasRuntimeCellMatches(cell) || !indexed)
        {
            ApplyReference(a_reference, matches);
        }
        else if (hasPlan)
        {
            ApplyEntry(a_reference, data->entries[plan->second]);
        }

        if (newWindowMatch && cell)
        {
            const auto references = CellReferences(cell);
            for (auto* reference : references)
            {
                if (reference != a_reference)
                {
                    ApplyReference(reference, matches);
                }
            }
            const auto result = ResultForMatches(matches);
            const bool notify = UpdateCellState(cell, matches, result);
            if (DetailedLoggingEnabled())
            {
                logger::info(
                    "[Window Sync] Dynamically initialized cellContains reference {:08X} "
                    "enabled XEMI mapping for {} reference(s) in cell {:08X}",
                    a_reference->GetFormID(),
                    references.size(),
                    cell->GetFormID());
            }
            if (notify)
            {
                NotifyClients(cell, result);
            }
        }
        else if (!pluginIndexComplete.load(std::memory_order_acquire) &&
                 cell && DetailedLoggingEnabled())
        {
            logger::info(
                "[Window Sync] Partial plugin index used runtime fallback for "
                "reference {:08X} in cell {:08X}",
                formID,
                cell->GetFormID());
        }
    }

    void ProcessCell(RE::TESObjectCELL* a_cell)
    {
        if (!a_cell || !a_cell->IsInteriorCell())
        {
            return;
        }
        {
            std::scoped_lock lock(stateLock);
            if (const auto found = cells.find(a_cell->GetFormID());
                found != cells.end() && found->second.complete)
            {
                return;
            }
        }
        auto* data = Config::StatData::GetSingleton();
        data->LoadConfig();
        if (!HasWindowProfiles())
        {
            return;
        }

        const auto references = CellReferences(a_cell);
        const auto matches = FindCellContainsEntries(a_cell, references);
        if (!matches.empty())
        {
            for (auto* reference : references)
            {
                ApplyReference(reference, matches);
            }
        }

        const auto result = ResultForMatches(matches);
        const bool notify = UpdateCellState(a_cell, matches, result);

        if (result.status == XEMIAPI::CellStatus::kMatched)
        {
            std::string profiles;
            for (const auto& profile : result.profiles)
            {
                if (!profiles.empty())
                {
                    profiles.append(", ");
                }
                profiles.append(profile);
            }
            logger::warn(
                "[Window Sync] Dynamic fallback classified interior cell {:08X} as profile(s) "
                "'{}' through cellContains from {} initialized reference(s)",
                a_cell->GetFormID(),
                profiles,
                references.size());
        }
        if (notify)
        {
            NotifyClients(a_cell, result);
        }
    }

    void ResetRuntimeState()
    {
        std::scoped_lock lock(stateLock);
        cells = startupCells;
    }
}  // namespace MPL::WindowSync

extern "C" __declspec(dllexport) const MPL::XEMIAPI::Interface* XEMIUtil_RequestAPI(
    const std::uint32_t a_version)
{
    return a_version == MPL::XEMIAPI::kVersion ?
               std::addressof(MPL::WindowSync::api) :
               nullptr;
}

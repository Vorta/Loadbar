#include "model/settings.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace loadbar {
bool disk_hidden(const HiddenDiskIds &ids, std::wstring_view id) noexcept {
    return std::ranges::binary_search(ids, id);
}
bool hide_disk(HiddenDiskIds &ids, std::wstring id) {
    const auto at = std::ranges::lower_bound(ids, id);
    if (at != ids.end() && *at == id) {
        return false;
    }
    ids.insert(at, std::move(id));
    return true;
}
void show_disk(HiddenDiskIds &ids, std::wstring_view id) {
    const auto at = std::ranges::lower_bound(ids, id);
    if (at != ids.end() && *at == id) {
        ids.erase(at);
    }
}

bool collection_changed(const Settings &before, const Settings &after) {
    return before.gpu_id != after.gpu_id || before.network_id != after.network_id ||
           before.interval_ms != after.interval_ms || before.gpu_visible != after.gpu_visible ||
           before.network_visible != after.network_visible ||
           before.hidden_disks != after.hidden_disks;
}
bool valid_settings(const Settings &settings) {
    const auto valid_id = [](const std::wstring &id) {
        return id.size() <= 1024 && std::ranges::none_of(id, [](wchar_t ch) { return ch < 32; });
    };
    return settings.edge <= Edge::bottom && std::isfinite(settings.thickness) &&
           settings.thickness >= kMinimumThicknessDips &&
           settings.thickness <= kMaximumThicknessDips &&
           std::floor(settings.thickness) == settings.thickness &&
           (!settings.legacy_percent ||
            (std::isfinite(*settings.legacy_percent) && *settings.legacy_percent >= 1 &&
             *settings.legacy_percent <= 25)) &&
           settings.interval_ms >= 250 && settings.interval_ms <= 5000 &&
           settings.alignment <= Alignment::end && valid_id(settings.monitor_id) &&
           valid_id(settings.gpu_id) && valid_id(settings.network_id) &&
           settings.hidden_disks.size() <= kMaximumHiddenDisks &&
           std::ranges::is_sorted(settings.hidden_disks) &&
           std::ranges::adjacent_find(settings.hidden_disks) == settings.hidden_disks.end() &&
           std::ranges::all_of(settings.hidden_disks,
                               [&](const auto &id) { return !id.empty() && valid_id(id); });
}
std::wstring encode_settings(const Settings &settings) {
    std::wostringstream stream;
    stream.imbue(std::locale::classic());
    // v9 persists only explicit disk exclusions; unknown identities remain visible.
    stream << L"Loadbar 9 " << static_cast<unsigned>(settings.edge) << L' ' << std::setprecision(17)
           << settings.thickness << L' ' << settings.interval_ms << L' '
           << std::quoted(settings.monitor_id) << L' ' << std::quoted(settings.gpu_id) << L' '
           << std::quoted(settings.network_id) << L' ' << static_cast<unsigned>(settings.alignment)
           << L' ' << settings.gpu_visible << L' ' << settings.network_visible << L' '
           << settings.legacy_percent.value_or(0);
    stream << L' ' << settings.hidden_disks.size();
    for (const auto &id : settings.hidden_disks) {
        stream << L' ' << std::quoted(id);
    }
    return stream.str();
}
std::optional<Settings> decode_settings(std::wstring_view text) {
    if (text.size() > kMaximumSettingsCharacters) {
        return std::nullopt;
    }
    std::wistringstream stream{std::wstring(text)};
    stream.imbue(std::locale::classic());
    Settings result;
    std::wstring name;
    unsigned version{}, edge{}, mode{}, scale{};
    stream >> name >> version >> edge;
    if (!stream || name != L"Loadbar" || version < 1 || version > 9 || edge > 4) {
        return std::nullopt;
    }
    if (version < 3) {
        stream >> mode;
    }
    stream >> result.thickness >> result.interval_ms;
    std::array<double, 4> old_ceilings{};
    if (version < 4) {
        for (double &ceiling : old_ceilings) {
            stream >> ceiling;
        }
    }
    stream >> std::quoted(result.monitor_id);
    if (version < 3) {
        std::wstring old_disk;
        stream >> std::quoted(old_disk);
        if (old_disk.size() > 1024 ||
            std::ranges::any_of(old_disk, [](wchar_t c) { return c < 32; })) {
            return std::nullopt;
        }
        // All physical disks are now shown separately; the former single choice is retired.
    }
    stream >> std::quoted(result.gpu_id) >> std::quoted(result.network_id);
    if (!stream || mode > 1) {
        return std::nullopt;
    }
    if (version >= 2) {
        unsigned alignment{}, numbers{}, heat{}, split{};
        stream >> alignment;
        if (version < 4) {
            stream >> scale;
        }
        if (version < 5) {
            stream >> numbers >> heat >> split;
        }
        if (!stream || alignment > 2 || scale > 1 || numbers > 1 || heat > 1 || split > 1) {
            return std::nullopt;
        }
        result.alignment = static_cast<Alignment>(alignment);
        // Validated old appearance preferences are deliberately retired.
    }
    if (version >= 8) {
        unsigned gpu_visible{}, network_visible{};
        double legacy_percent{};
        stream >> gpu_visible >> network_visible >> legacy_percent;
        if (!stream || gpu_visible > 1 || network_visible > 1 || !std::isfinite(legacy_percent) ||
            (legacy_percent != 0 && (legacy_percent < 1 || legacy_percent > 25))) {
            return std::nullopt;
        }
        result.gpu_visible = gpu_visible != 0;
        result.network_visible = network_visible != 0;
        if (legacy_percent != 0) {
            result.legacy_percent = legacy_percent;
        }
    }
    if (version >= 9) {
        std::size_t count{};
        stream >> count;
        if (!stream || count > kMaximumHiddenDisks) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < count; ++i) {
            std::wstring id;
            stream >> std::quoted(id);
            if (!stream || id.empty() || id.size() > 1024 ||
                !hide_disk(result.hidden_disks, std::move(id))) {
                return std::nullopt;
            }
        }
    }
    // Validate old records before retiring ceilings/log mode. No old peak is inferred.
    if (version < 4 && !std::ranges::all_of(
                           old_ceilings,
                           [scale](double ceiling) {
                               return std::isfinite(ceiling) && ceiling >= 1 && ceiling <= 1e15 &&
                                      (scale == 0 || ceiling > 1000);
                           })) {
        return std::nullopt;
    }
    stream >> std::ws;
    result.edge = static_cast<Edge>(edge);
    if (version < 3 && mode == 0) {
        result.legacy_percent = result.thickness;
        result.thickness = Settings{}.thickness;
    }
    if (version < 6) {
        // Old DIP records allowed 1–480. Validate before migration so malformed records
        // do not become valid merely because clamping would put them in the new range.
        if (!std::isfinite(result.thickness) || result.thickness < 1 || result.thickness > 480) {
            return std::nullopt;
        }
        result.thickness =
            std::clamp(result.thickness, kMinimumThicknessDips, kMaximumThicknessDips);
    }
    if (version < 7) {
        // v6 allowed fractional DIPs in 40–640. Validate its bounds before rounding;
        // older records have already been validated and clamped above.
        if (!std::isfinite(result.thickness) || result.thickness < kMinimumThicknessDips ||
            result.thickness > kMaximumThicknessDips) {
            return std::nullopt;
        }
        result.thickness = std::round(result.thickness);
    }
    return stream.eof() && valid_settings(result) ? std::optional(result) : std::nullopt;
}
} // namespace loadbar

#include "core/tweak_service.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace gno {

namespace {

std::string valueText(const TweakSpec& s, const TweakValue& v) {
    if (!v.existed) return "-";
    return s.type == TweakType::Dword ? std::to_string(v.dword) : v.str;
}

bool differsFromRecommended(const TweakSpec& s, const TweakValue& v) {
    if (!v.existed) return true;
    if (s.type == TweakType::Dword) return v.dword != s.dword_value;
    return v.str != s.sz_value;
}

} // namespace

std::vector<TweakView> TweakService::listViews(const std::string& category) const {
    std::vector<TweakView> out;
    for (const auto& spec : allTweaks(external_dir_)) {
        if (!category.empty() && category != spec.category) continue;
        TweakView view;
        view.spec = &spec;
        view.current = access_.read(spec);
        view.differs = differsFromRecommended(spec, view.current);
        view.current_text = valueText(spec, view.current);
        out.push_back(std::move(view));
    }
    return out;
}

std::string TweakService::snapshotPath() const {
    return (std::filesystem::path(rollback_dir_) / "last_batch.json").string();
}

bool TweakService::hasRollbackSnapshot() const {
    std::error_code ec;
    return std::filesystem::exists(snapshotPath(), ec);
}

std::string TweakService::applyCategory(const std::string& category) {
    // 1. select
    std::vector<const TweakSpec*> selected;
    for (const auto& spec : allTweaks(external_dir_))
        if (category.empty() || spec.category == category)
            selected.push_back(&spec);

    int applied = 0, failed = 0;
    last_applied_needs_reboot_.clear();

    // 2. capture snapshot
    std::ostringstream snap;
    snap << "{\n  \"entries\": [\n";
    for (std::size_t i = 0; i < selected.size(); ++i) {
        const auto* spec = selected[i];
        const auto old_v = access_.read(*spec);
        snap << "    {\"id\": \"" << spec->id << "\", \"root\": "
             << static_cast<int>(spec->root) << ", \"subkey\": \"";
        for (char c : std::string(spec->subkey))
            snap << (c == '\\' ? "\\\\" : std::string(1, c));
        snap << "\", \"value_name\": \"" << spec->value_name
             << "\", \"type\": " << static_cast<int>(spec->type)
             << ",\n     \"existed\": " << (old_v.existed ? "true" : "false")
             << ", \"dword\": " << old_v.dword << ", \"str\": \"";
        for (char c : old_v.str)
            snap << (c == '\\' ? "\\\\" : c == '"' ? "\\\"" : std::string(1, c));
        snap << "\"}" << (i + 1 < selected.size() ? "," : "") << "\n";
    }
    snap << "  ]\n}";

    if (!gno::persistence::atomicWriteText(snapshotPath(), snap.str()))
        return "\xd0\x9d\xd0\xb5 \xd1\x83\xd0\xb4\xd0\xb0\xd0\xbb\xd0\xbe\xd1\x81\xd1\x8c "
               "\xd1\x81\xd0\xbe\xd1\x85\xd1\x80\xd0\xb0\xd0\xbd\xd0\xb8\xd1\x82\xd1\x8c "
               "\xd1\x81\xd0\xbd\xd0\xb0\xd0\xbf\xd1\x88\xd0\xbe\xd1\x82";

    // 3. write recommended values
    for (const auto* spec : selected) {
        TweakValue rec;
        rec.existed = true;
        rec.dword = spec->dword_value;
        rec.str = spec->sz_value;
        try {
            access_.write(*spec, rec);
            ++applied;
            if (spec->needs_reboot)
                last_applied_needs_reboot_.push_back(spec->id);
        }
        catch (...) { ++failed; }
    }

    bool reboot = false;
    for (const auto* s : selected)
        if (s->needs_reboot) { reboot = true; break; }

    std::ostringstream res;
    res << applied << "/" << selected.size();
    if (failed) res << ", errors: " << failed;
    if (reboot) res << "\nReboot required.";
    return res.str();
}

std::vector<std::string> TweakService::appliedNeedsReboot() const {
    return last_applied_needs_reboot_;
}

std::string TweakService::rollbackLast() {
    std::ifstream file(snapshotPath());
    if (!file) return "no snapshot";

    std::ostringstream buf;
    buf << file.rdbuf();
    const std::string json = buf.str();

    int restored = 0, failed = 0;
    std::size_t pos = 0;
    while ((pos = json.find("\"id\": \"", pos)) != std::string::npos) {
        pos += 8;
        const auto end_q = json.find('"', pos);
        if (end_q == std::string::npos) break;
        const std::string id = json.substr(pos, end_q - pos);
        pos = end_q;

        for (const auto& spec : allTweaks(external_dir_)) {
            if (id != spec.id) continue;

            const std::size_t exi = json.find("\"existed\": ", pos);
            const bool existed = exi <= pos + 400 && json.compare(exi, 4, "true") == 0;

            if (!existed) {
                try { access_.write(spec, TweakValue{}); ++restored; } catch (...) { ++failed; }
            } else {
                TweakValue v;
                v.existed = true;
                const std::size_t dw = json.find("\"dword\":", pos);
                if (dw != std::string::npos && dw < pos + 400)
                    try { v.dword = static_cast<std::uint32_t>(std::stoul(json.substr(dw + 8))); } catch (...) {}
                const std::size_t sk = json.find("\"str\": \"", pos);
                if (sk != std::string::npos && sk < pos + 400) {
                    std::size_t p = sk + 8, q = p;
                    while (q < json.size() && json[q] != '"') { if (json[q] == '\\') ++q; ++q; }
                    v.str = json.substr(p, q - p);
                }
                try { access_.write(spec, v); ++restored; } catch (...) { ++failed; }
            }
            break;
        }
    }

    std::error_code ec;
    std::filesystem::remove(snapshotPath(), ec);

    return std::to_string(restored) + " restored, " + std::to_string(failed) + " failed";
}

} // namespace gno

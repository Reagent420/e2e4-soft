#include "problem_db.h"
#include "launch_diagnostics.h"

#include <algorithm>
#include <cctype>

namespace gno {

namespace {

std::string lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

ProblemEntry make(const std::string& game, const std::string& title, const std::string& symptoms,
                  const std::string& cause, const std::string& solution,
                  const std::string& fix, const std::string& difficulty) {
    ProblemEntry e;
    e.game = game;
    e.title = title;
    e.symptoms = symptoms;
    e.cause = cause;
    e.solution = solution;
    e.fix_action = fix;
    e.difficulty = difficulty;
    return e;
}

} // namespace

std::vector<ProblemEntry> ProblemDb::getAll() {
    std::vector<ProblemEntry> out;

    // v2.0: CS2 matchmaking ping filter (routed through safe engine)
    out.push_back(make("CS2",
        "Matches on 100+ ping",
        "Matchmaking sends you to far regions because the client ping filter is unlimited by default",
        "Set mm_dedicated_search_maxping=60 so matchmaking only offers nearby servers",
        "Enable mm_dedicated_search_maxping via GNO autofix (60 ms, with backup and rollback)",
        "cs2_maxping", "easy"));
    // --- CS2 / Counter-Strike 2 ---
    out.push_back(make("CS2",
        "������� ���� � ���������",
        "���� ������, ���������� �����������������, ��������� �� �������������.",
        "������������ ������� �� �������� Valve; ������ ������� �� ��������� ����.",
        "1. �������� ����������� TCP � MTU (������� �������������).\n2. ��������� ������������� ����� ���/����� �� �������.\n3. ������� DNS �� 1.1.1.1.\n4. ��� ���������� ��������� ���� �������� ���������������� �����.",
        "tcp_mtu", "medium"));
    out.push_back(make("CS2",
        "������� � �������� FPS",
        "FPS ������ � ������������, �������� ��������.",
        "Game DVR ����� ����� � ����; CPU �������� �� ���������� �������.",
        "1. �������� ���������� FPS� � ��������� �������� Game DVR � ���������� ���� �������.\n2. ��������� ���-������� CS2 (autoexec.cfg) �� ������� ����������.",
        "fps_boost", "easy"));
    out.push_back(make("CS2",
        "������ VCRUNTIME140.dll / ����������� DLL",
        "���� �� �����������, ���������� ���� � ������� � DLL.",
        "�� ���������� Visual C++ Redistributable 2015-2022.",
        "���������� Microsoft Visual C++ Redistributable (x64) � ������������ �����. ��������� �������, ��� ���� ���������� ���, � ����������� �������.",
        "", "easy"));

    // --- Valorant ---
    out.push_back(make("Valorant",
        "���� �� ������� 80+ �� � ����������",
        "������� ���� ���� ��� ������� ���������, ����� �����������.",
        "������� �� �������� Riot �����������; DNS-��������� ���������.",
        "1. ������� DNS �� 1.1.1.1 (������� �������������).\n2. �������� TCP-�����������.\n3. ��� ��������� ���� � ��������� ������� ��������.",
        "dns_tcp", "medium"));
    out.push_back(make("Valorant",
        "FPS ���� 144 �� ������ ��",
        "������� ������ �� ���������� �� 144, �������� �������.",
        "Game DVR � ������� �������� �������� �������.",
        "1. ��������� Game DVR � �������� ���� �������� �������������������.\n2. ��������� ���-������� Valorant (GameUserSettings.ini) ��� ����������� FPS.",
        "fps_boost", "easy"));
    out.push_back(make("Valorant",
        "������� Vanguard �� ����������� / ������",
        "���� ������ ������������� ��������� ��� ����� ������ Vanguard.",
        "������ Vanguard �� ����������� ��� ������������� �����������.",
        "������������� ��������� ����� ���������. ���������, ��� ������ vgc ��������. ��������� ���������, ��� ���������/���������� ����� ����������� ������ � �����������.",
        "", "medium"));

    // --- PUBG ---
    out.push_back(make("PUBG",
        "������ ������� � �������� � ������",
        "����� ���������, �������� �� ���������, ����� ����������.",
        "������������ �����; ������������ ������� ��-�� �������� MTU.",
        "1. ���������� MTU 1400 (������� �������������).\n2. �������� ����������� ������ (����� ����������� ��������� ����).",
        "mtu", "medium"));
    out.push_back(make("PUBG",
        "���� �������� � �������",
        "FPS ��������� � ��������� �������.",
        "��������� ������� � ���� �����������; ��� �������.",
        "1. ��������� ���-������� PUBG (GameUserSettings.ini) � ��������� ������� ��������� �����.\n2. �������� ������������ ����������� ������.",
        "fps_boost", "medium"));
    out.push_back(make("PUBG",
        "������ �Failed to create D3D device�",
        "���� �� �����������, ���� � ������� DirectX.",
        "���������� �������� ���������� ��� ����������� DirectX.",
        "�������� ������� ���������� � ���������� ��������� ������ DirectX. ��������� �������� ������������ ������� � �����������.",
        "", "medium"));

    // --- Fortnite ---
    out.push_back(make("Fortnite",
        "������� ���� � ������ ����������",
        "���� 100+, ���������� ���������� � ������.",
        "������� �� �������� Epic Games ���������� �������.",
        "1. ������� DNS �� 1.1.1.1.\n2. �������� TCP-����������� � MTU 1400.\n3. ������� ��������� ����������� �������.",
        "dns_tcp", "medium"));
    out.push_back(make("Fortnite",
        "���� ��� ����� � ���������� �����",
        "��� �������� �������� ����� �������� � ������.",
        "��������� DNS ��� �������-��������; ������ ��������� ��������.",
        "1. ��������� ������� Fortnite � ������� ����������� �������� (������� �������� ���).\n2. �������� ����������� ����.",
        "priority", "easy"));

    // --- Dota 2 ---
    out.push_back(make("Dota 2",
        "���� ������ ������ ��������� ������",
        "������������� ������� ����� �� 300 ��.",
        "������������ Wi-Fi ��� ������� ��������.",
        "1. ���������, ��� ��� ������� �������� (������� ����������).\n2. �������� TCP-�����������.\n3. ��� ��������� ���� � ����������� ������.",
        "tcp", "easy"));
    out.push_back(make("Dota 2",
        "���� �������� ��� ������ �����",
        "������ ����������� ��� ������.",
        "�������� ��� ����; �������� � overlay-�����������.",
        "��������� �������� Overwolf/Afterburner (������� ����������). ��������� ������� �� � ����������� �������. �������� ����� �����.",
        "", "medium"));

    // --- Apex Legends ---
    out.push_back(make("Apex Legends",
        "��� ������ Leaf / Net 100",
        "������ ���������� � ��������� EA, ������ Net.",
        "������ ������� �� �������� �� �������� EA.",
        "1. �������� MTU 1400 � TCP-�����������.\n2. ������� DNS �� 1.1.1.1.\n3. ��� ��������� ���� � ������������ ��������.",
        "mtu", "medium"));
    out.push_back(make("Apex Legends",
        "������� ���� �����",
        "���� ��������� ������� � �������� �����.",
        "���������� ������ ���������� � ���� ���.",
        "����������� ��������� ����������� ������ Wi-Fi. �������� ����������� TCP. ��������� ������� �������� ��������� � �������������.",
        "tcp", "easy"));

    // --- Warzone / Call of Duty ---
    out.push_back(make("Warzone",
        "Stuttering ��� ������������",
        "�������� ������������ ��� ����/��������.",
        "������������ ����������� ������; ����������� ������ �� ���������.",
        "1. �������� ������������ ����������� ������ (������� �������������).\n2. �������� ������� � ������� ���������.",
        "vm", "medium"));
    out.push_back(make("Warzone",
        "������ ����������� � �������",
        "���� �� ����� ����������� �� �������� Call of Duty.",
        "DNS-��������� ������������ ��� ����������.",
        "������� DNS �� 1.1.1.1 ����� ���������. ��������� ���������� � ��������� �������, ��� DNS �� ��������, � �����������.",
        "dns", "medium"));

    // --- Minecraft ---
    out.push_back(make("Minecraft",
        "���� �� ��������",
        "������� ���� �� ������������, �������� ���������������.",
        "������� �������� �� �������; UDP-������ ��������.",
        "1. �������� TCP-����������� � MTU 1400.\n2. ��� ��������� ���� � �������������.",
        "mtu", "easy"));
    out.push_back(make("Minecraft",
        "������� � ���������",
        "FPS ������ ��� ��������� ��������.",
        "Game DVR ����� �����; ���� ������� ������������ CPU.",
        "�������� ���������� FPS�: ���������� Game DVR � ���� �������� ������������������� ���� �� +20% FPS.",
        "fps_boost", "easy"));

    // --- GTA V ---
    out.push_back(make("GTA V",
        "���� �������� �� ��������",
        "����� � �������� ����� ����� ��������.",
        "�������� ��� Social Club; ������������ ����������� ������.",
        "1. �������� ������������ ����������� ������.\n2. ������� ������� overlay-��������� � ��������� ������� �� � �����������.",
        "vm", "medium"));
    out.push_back(make("GTA V",
        "���� � ������� (GTA Online)",
        "������ ���������������, ��������� ��������� �������.",
        "������ �������; ��������� ������� �� �������� Rockstar.",
        "�������� TCP-����������� � MTU 1400. ������� DNS �� 1.1.1.1.",
        "mtu", "medium"));

    // --- Rocket League ---
    out.push_back(make("Rocket League",
        "���� �������, ��� ������ �������",
        "������ ���� �������� ��-�� �����.",
        "������������ �����; ������� MTU ������������� ������.",
        "���������� MTU 1400 � �������� TCP-�����������. ��������� ������� ������ � �����������.",
        "mtu", "easy"));

    // --- League of Legends ---
    out.push_back(make("League of Legends",
        "�������� ����� (input lag)",
        "����/������ ����������� � ����������.",
        "��������� ������� �� �������� Riot; ������� ������� ������������� TCP.",
        "�������� TCP-����������� (���������� ACK). ������� DNS �� 1.1.1.1.",
        "tcp", "easy"));
    out.push_back(make("League of Legends",
        "����� ������������ ������",
        "������ ����������� � ����� ������.",
        "����������� ��� �������; �������� � �����������.",
        "������������� ������ ����� ��������� ����� �����������. ���������, ��� ��������� �� ��������� ������.",
        "", "medium"));

    return out;
}

std::vector<ProblemEntry> ProblemDb::getForGame(const std::string& game_name) {
    std::string g = lower(game_name);
    std::vector<ProblemEntry> out;
    for (const auto& e : getAll()) {
        if (lower(e.game) == g)
            out.push_back(e);
    }
    return out;
}

std::vector<std::string> ProblemDb::getKnownGames() {
    std::vector<std::string> games;
    for (const auto& e : getAll()) {
        bool seen = false;
        for (const auto& g : games)
            if (g == e.game) { seen = true; break; }
        if (!seen)
            games.push_back(e.game);
    }
    return games;
}

std::string ProblemDb::applyAutoFix(const ProblemEntry& entry) {
    if (entry.fix_action == "tcp_mtu") {
        std::string r = LaunchDiagnostics::applyFix("tcp");
        r += "\n" + LaunchDiagnostics::applyFix("mtu");
        return r;
    }
    if (entry.fix_action == "dns_tcp") {
        std::string r = LaunchDiagnostics::applyFix("dns");
        r += "\n" + LaunchDiagnostics::applyFix("tcp");
        return r;
    }
    if (entry.fix_action == "fps_boost") {
        return LaunchDiagnostics::applyFix("power_plan") + "\n" +
               LaunchDiagnostics::applyFix("game_dvr") + "\n" +
               LaunchDiagnostics::applyFix("fullscreen_opt");
    }
    return LaunchDiagnostics::applyFix(entry.fix_action);
}

} // namespace gno
namespace gno {

std::vector<ProblemEntry> ProblemDb::search(const std::string& game_name, const std::string& query) {
    auto entries = getForGame(game_name);
    if (query.empty()) return entries;
    const std::string q = lower(query);
    std::vector<ProblemEntry> hits;
    for (const auto& e : entries) {
        if (lower(e.title).find(q) != std::string::npos ||
            lower(e.symptoms).find(q) != std::string::npos ||
            lower(e.cause).find(q) != std::string::npos ||
            lower(e.solution).find(q) != std::string::npos) {
            hits.push_back(e);
        }
    }
    return hits;
}

} // namespace gno

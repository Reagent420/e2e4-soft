#include "problem_db.h"
#include "launch_diagnostics.h"

#include <algorithm>
#include <cctype>
#include <utility>

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
        "Матчи на высоком пинге",
        "Пинг растёт в матчмейкинге, подключение идёт к дальним серверам.",
        "Регионы подбирает инфраструктура Valve; маршрут до сервера не оптимизирован.",
        "1. Проверить TCP-соединение и MTU (диагностика запуска).\n2. Сравнить задержку до разных регионов в таблице маршрутов.\n3. Сменить DNS на 1.1.1.1.\n4. Если проблема в маршруте — обратиться к провайдеру.",
        "tcp_mtu", "medium"));
    out.push_back(make("CS2",
        "Просадки и низкий FPS",
        "FPS плавает в перестрелках, картинка дёргается.",
        "Game DVR пишет клипы в фоне; CPU нагружен фоновыми процессами.",
        "1. Измерить текущий FPS и отключить Game DVR через средство запуска.\n2. Применить про-пресет CS2 (autoexec.cfg) для стабильного FPS.",
        "fps_boost", "easy"));
    out.push_back(make("CS2",
        "Нет VCRUNTIME140.dll / отсутствуют DLL",
        "Игра не запускается, ругается на отсутствующую DLL.",
        "Не установлен Visual C++ Redistributable 2015-2022.",
        "Установите Microsoft Visual C++ Redistributable (x64) с официального сайта. Если библиотека есть, но не регистрируется — обратитесь в поддержку.",
        "", "easy"));

    // --- Valorant ---
    out.push_back(make("Valorant",
        "Пинг выше 80 мс и потеря пакетов",
        "Пинг скачет в перестрелках, модельки телепортируются.",
        "Маршрут до серверов Riot нестабилен; DNS-резолвинг подтормаживает.",
        "1. Сменить DNS на 1.1.1.1 (диагностика запуска).\n2. Проверить TCP-соединение.\n3. Если пинг до сервера выше 100 — переключиться на другой регион в настройках игры.",
        "dns_tcp", "medium"));
    out.push_back(make("Valorant",
        "FPS ниже 144 на мощном ПК",
        "Частота держится ниже 144, хотя железо справляется.",
        "Game DVR и фоновые процессы забирают часть кадров.",
        "1. Отключить Game DVR и полноэкранную оптимизацию для клиента.\n2. Применить про-пресет Valorant (GameUserSettings.ini) для разблокировки FPS.",
        "fps_boost", "easy"));
    out.push_back(make("Valorant",
        "Античит Vanguard не запускается / ошибки",
        "Игра не может подключиться к защищённому режиму из-за античита Vanguard.",
        "Служба Vanguard не установлена или заблокирована защитником системы.",
        "Переустановите античит с официального сайта. Проверьте, что служба vgc работает. Убедитесь, что защитник/антивирус не блокирует драйвер.",
        "", "medium"));

    // --- PUBG ---
    out.push_back(make("PUBG",
        "Высокий пинг и потери пакетов в начале матча",
        "Пинг растёт в начале, потом плавно нормализуется.",
        "Нестабильный маршрут; потери из-за слишком большого MTU.",
        "1. Установить MTU 1400 (диагностика запуска).\n2. Проверить стабильность линии (тест скорости и потерь).",
        "mtu", "medium"));
    out.push_back(make("PUBG",
        "Просадки FPS в городах",
        "FPS падает в крупных локациях и перестрелках.",
        "Высокие настройки графики; нагруженный CPU.",
        "1. Применить про-пресет PUBG (GameUserSettings.ini) для снижения нагрузки на CPU.\n2. Отключить полноэкранную оптимизацию.",
        "fps_boost", "medium"));
    out.push_back(make("PUBG",
        "Ошибка «Failed to create D3D device»",
        "Игра не запускается, ругается на видеокарту.",
        "Устаревшие драйверы или проблемы с инициализацией DirectX.",
        "Обновите драйверы видеокарты с официального сайта производителя. Если не помогает — переустановите библиотеки DirectX.",
        "", "medium"));

    // --- Fortnite ---
    out.push_back(make("Fortnite",
        "Случайный пинг и скачки задержки",
        "Пинг 100+, задержка прыгает в движении.",
        "Маршрут до серверов Epic Games нестабилен.",
        "1. Сменить DNS на 1.1.1.1.\n2. Проверить TCP-соединение и MTU 1400.\n3. Сменить регион матчмейкинга в настройках.",
        "dns_tcp", "medium"));
    out.push_back(make("Fortnite",
        "Фриз при смене зоны и рывки",
        "Картинка замирает при переходе между зонами.",
        "Низкий приоритет процесса; подгрузка ресурсов.",
        "1. Поднять приоритет процесса Fortnite при запуске (диагностика запуска).\n2. Проверить стабильность соединения.",
        "priority", "easy"));

    // --- Dota 2 ---
    out.push_back(make("Dota 2",
        "Пинг сильно вырастает к вечеру",
        "Задержка подскакивает до 300 мс.",
        "Загруженный Wi-Fi канал или перегруженный маршрут.",
        "1. Убедиться, что используется кабель (диагностика запуска).\n2. Проверить TCP-соединение.\n3. Если пинг до сервера высокий — сменить регион матча.",
        "tcp", "easy"));
    out.push_back(make("Dota 2",
        "Фриз при добивании крипов",
        "Короткие зависания в замесах.",
        "Нагруженный CPU; конфликт с overlay-программами.",
        "Закройте оверлеи Overwolf/Afterburner (диагностика процессов). Проверьте, что они не запускаются вместе с игрой. Снизьте настройки графики.",
        "", "medium"));

    // --- Apex Legends ---
    out.push_back(make("Apex Legends",
        "Код ошибки Leaf / Net 100",
        "Вылеты и разрывы связи с серверами EA, код Net.",
        "Проблема с соединением или маршрутом до серверов EA.",
        "1. Установить MTU 1400 и проверить TCP-соединение.\n2. Сменить DNS на 1.1.1.1.\n3. Если пинг не падает — смените регион сервера.",
        "mtu", "medium"));
    out.push_back(make("Apex Legends",
        "Случайные резкие скачки пинга",
        "Пинг подскакивает в случайные моменты.",
        "Нестабильный маршрут или перегруженный канал.",
        "Проверьте стабильность беспроводной сети. Включите TCP-соединение. Смените DNS на 1.1.1.1.",
        "tcp", "easy"));

    // --- Warzone / Call of Duty ---
    out.push_back(make("Warzone",
        "Stuttering при перестрелках",
        "Зависания в начале боя и при перестрелках.",
        "Нестабильный частотный диапазон; скачки напряжения по сети.",
        "1. Проверить стабильность сетевого соединения (диагностика запуска).\n2. Поднять приоритет процесса игры.",
        "vm", "medium"));
    out.push_back(make("Warzone",
        "Пакеты не доходят до сервера",
        "Пинг растёт на пакетах до серверов Call of Duty.",
        "DNS-резолвинг или нестабильный маршрут до серверов.",
        "Смените DNS на 1.1.1.1 и проверьте снова. Если пинг не изменился — обратитесь к провайдеру.",
        "dns", "medium"));

    // --- Minecraft ---
    out.push_back(make("Minecraft",
        "Пинг на всех серверах",
        "Задержка до любого сервера, пакеты теряются.",
        "Нестабильный маршрут; блокировка UDP-трафика.",
        "1. Проверить TCP-соединение и MTU 1400.\n2. Если пинг не падает — смените провайдера.",
        "mtu", "easy"));
    out.push_back(make("Minecraft",
        "Просадки и задержки кадров",
        "FPS падает при перемещении по миру.",
        "Game DVR пишет в фоне; Java нагружает CPU.",
        "Измерьте текущий FPS: отключение Game DVR даёт стабильные +20% FPS.",
        "fps_boost", "easy"));

    // --- GTA V ---
    out.push_back(make("GTA V",
        "Пинг скачет по регионам",
        "Задержка растёт в одной игровой сессии.",
        "Нагрузка на Social Club; нестабильный маршрут.",
        "1. Проверить стабильность сетевого соединения.\n2. Отключить overlay-программы и проверить, что они не запускаются вместе с игрой.",
        "vm", "medium"));
    out.push_back(make("GTA V",
        "Потеря пакетов в онлайне (GTA Online)",
        "Вылеты из сессий, модели телепортируются.",
        "Нестабильный маршрут; нагрузка на серверы Rockstar.",
        "Проверьте TCP-соединение и MTU 1400. Смените DNS на 1.1.1.1.",
        "mtu", "medium"));

    // --- Rocket League ---
    out.push_back(make("Rocket League",
        "Пинг скачет, хотя интернет стабильный",
        "Задержка плавает, хотя линия хорошая.",
        "Нестабильный маршрут; слишком большой MTU режет пакеты.",
        "Установите MTU 1400 и включите TCP-соединение. Проверьте стабильность линии ещё раз.",
        "mtu", "easy"));

    // --- League of Legends ---
    out.push_back(make("League of Legends",
        "Задержка отклика (input lag)",
        "Нажатия и мышь реагируют с задержкой.",
        "Нагрузка на серверы Riot; TCP-соединение не настроено.",
        "Включите TCP-соединение (отложенный ACK). Смените DNS на 1.1.1.1.",
        "tcp", "easy"));
    out.push_back(make("League of Legends",
        "Частые вылеты клиента",
        "Клиент вылетает в случайные моменты.",
        "Нестабильный маршрут; конфликты процессов.",
        "Закройте лишние программы перед запуском (диагностика процессов). Убедитесь, что антивирус не блокирует клиент.",
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

std::vector<ProblemEntry> ProblemDb::forDiagnostic(const DiagnosticReport& report) {
    // Недостаточно данных — советовать нечего: любой подбор был бы гаданием.
    if (report.outcome == DiagnosticOutcome::InsufficientData)
        return {};

    // Ключевые слова берём из того, что реально видно в отчёте: исход плюс
    // измеренные метрики. Так «проблема в локальной сети» ведёт к статьям про
    // роутер и потери, а не к случайной подборке.
    std::vector<std::string> keywords;
    switch (report.outcome) {
        case DiagnosticOutcome::LocalNetworkProblem:
            keywords = {"роутер", "локальн", "wifi", "wi-fi", "шлюз", "кабел", "потер", "сеть"};
            break;
        case DiagnosticOutcome::ImprovementLikely:
            keywords = {"маршрут", "пинг", "задержк", "dns", "mtu", "регион", "провайдер", "сервер"};
            break;
        case DiagnosticOutcome::NoImprovementFound:
            keywords = {"fps", "dvr", "оптимизац", "фон"};
            break;
        case DiagnosticOutcome::InsufficientData:
            return {};
    }

    if (report.direct.median_ms > 80.0 || report.gateway.median_ms > 80.0)
        keywords.push_back("задержк");
    if (report.direct.loss_percent > 2.0)
        keywords.push_back("потер");
    if (report.direct.jitter_ms > 15.0 || report.gateway.jitter_ms > 15.0)
        keywords.push_back("нестабильн");

    const std::string game = lower(report.game_id);

    std::vector<std::pair<int, ProblemEntry>> scored;
    for (const auto& e : getAll()) {
        const std::string haystack =
            lower(e.title + " " + e.symptoms + " " + e.cause + " " + e.solution);
        int score = 0;
        for (const auto& kw : keywords) {
            if (!kw.empty() && haystack.find(kw) != std::string::npos) ++score;
        }
        // Статья про ту же игру конкретнее — её решение применимо напрямую.
        if (!game.empty() && lower(e.game) == game) score += 2;
        if (score > 0) scored.emplace_back(score, e);
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<ProblemEntry> out;
    constexpr std::size_t kMaxArticles = 5;
    for (const auto& entry : scored) {
        if (out.size() >= kMaxArticles) break;
        out.push_back(entry.second);
    }
    return out;
}

} // namespace gno

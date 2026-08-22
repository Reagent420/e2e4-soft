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

    // --- CS2 / Counter-Strike 2 ---
    out.push_back(make("CS2",
        "Высокий пинг и телепорты",
        "Пинг скачет, противники «телепортируются», попадания не засчитываются.",
        "Нестабильный маршрут до серверов Valve; потери пакетов на последней миле.",
        "1. Включите оптимизацию TCP и MTU (вкладка «Оптимизация»).\n2. Выполните сравнительный замер «до/после» на главной.\n3. Смените DNS на 1.1.1.1.\n4. При включённой серверной сети включите мультимаршрутный режим.",
        "tcp_mtu", "medium"));
    out.push_back(make("CS2",
        "Тормоза и просадки FPS",
        "FPS падает в перестрелках, картинка дёргается.",
        "Game DVR пишет видео в фоне; CPU работает на пониженной частоте.",
        "1. Включите «Ускорение FPS» — программа отключит Game DVR и переключит план питания.\n2. Примените про-профиль CS2 (autoexec.cfg) на вкладке «Настройки».",
        "fps_boost", "easy"));
    out.push_back(make("CS2",
        "Ошибка VCRUNTIME140.dll / отсутствует DLL",
        "Игра не запускается, появляется окно с ошибкой о DLL.",
        "Не установлен Visual C++ Redistributable 2015-2022.",
        "Установите Microsoft Visual C++ Redistributable (x64) с официального сайта. Программа покажет, что этой библиотеки нет, в диагностике запуска.",
        "", "easy"));

    // --- Valorant ---
    out.push_back(make("Valorant",
        "Пинг до сервера 80+ мс и «лагспайки»",
        "Высокий пинг даже при хорошем интернете, рывки противников.",
        "Маршрут до серверов Riot неоптимален; DNS-резолвинг медленный.",
        "1. Смените DNS на 1.1.1.1 (вкладка «Оптимизация»).\n2. Включите TCP-оптимизацию.\n3. При серверной сети — автовыбор лучшего маршрута.",
        "dns_tcp", "medium"));
    out.push_back(make("Valorant",
        "FPS ниже 144 на слабом ПК",
        "Частота кадров не дотягивает до 144, картинка мылится.",
        "Game DVR и фоновые процессы отнимают ресурсы.",
        "1. Отключите Game DVR и включите план «Высокая производительность».\n2. Примените про-профиль Valorant (GameUserSettings.ini) для стабильного FPS.",
        "fps_boost", "easy"));
    out.push_back(make("Valorant",
        "Античит Vanguard не запускается / ошибка",
        "Игра просит перезагрузить компьютер или пишет ошибку Vanguard.",
        "Служба Vanguard не запустилась или заблокирована антивирусом.",
        "Перезагрузите компьютер после установки. Проверьте, что служба vgc запущена. Программа подскажет, что антивирус/брандмауэр может блокировать службу в диагностике.",
        "", "medium"));

    // --- PUBG ---
    out.push_back(make("PUBG",
        "Потери пакетов и «вылеты» с матчей",
        "Кадры «замирают», персонаж не двигается, затем дисконнект.",
        "Нестабильная линия; фрагментация пакетов из-за большого MTU.",
        "1. Установите MTU 1400 (вкладка «Оптимизация»).\n2. Включите компенсацию потерь (после подключения серверной сети).",
        "mtu", "medium"));
    out.push_back(make("PUBG",
        "Игра тормозит в зданиях",
        "FPS проседает в населённых пунктах.",
        "Настройки графики в игре конфликтуют; кэш системы.",
        "1. Примените про-профиль PUBG (GameUserSettings.ini) — программа сделает резервную копию.\n2. Включите «Оптимизацию виртуальной памяти».",
        "fps_boost", "medium"));
    out.push_back(make("PUBG",
        "Ошибка «Failed to create D3D device»",
        "Игра не запускается, окно с ошибкой DirectX.",
        "Устаревшие драйверы видеокарты или повреждённый DirectX.",
        "Обновите драйвер видеокарты и установите последнюю версию DirectX. Программа проверит конфигурацию графики в диагностике.",
        "", "medium"));

    // --- Fortnite ---
    out.push_back(make("Fortnite",
        "Высокий пинг и разрыв соединения",
        "Пинг 100+, постоянные реконнекты к матчам.",
        "Маршрут до серверов Epic Games нестабилен вечером.",
        "1. Смените DNS на 1.1.1.1.\n2. Включите TCP-оптимизацию и MTU 1400.\n3. Вечером включайте оптимизацию заранее.",
        "dns_tcp", "medium"));
    out.push_back(make("Fortnite",
        "Лаги при входе в креативные карты",
        "При загрузке креатива долго грузится и фризит.",
        "Медленный DNS для контент-серверов; низкий приоритет процесса.",
        "1. Примените профиль Fortnite с высоким приоритетом процесса (вкладка «Профили игр»).\n2. Включите оптимизацию сети.",
        "priority", "easy"));

    // --- Dota 2 ---
    out.push_back(make("Dota 2",
        "Пинг скачет каждые несколько секунд",
        "Периодические «спайки» пинга до 300 мс.",
        "Нестабильный Wi-Fi или фоновые загрузки.",
        "1. Проверьте, что нет фоновых загрузок (вкладка «Процессы»).\n2. Включите TCP-оптимизацию.\n3. При серверной сети — компенсацию потерь.",
        "tcp", "easy"));
    out.push_back(make("Dota 2",
        "Игра вылетает при выборе героя",
        "Клиент закрывается без ошибки.",
        "Повреждён кэш игры; конфликт с overlay-программами.",
        "Проверьте процессы Overwolf/Afterburner (вкладка «Процессы»). Программа покажет их в диагностике запуска. Закройте перед игрой.",
        "", "medium"));

    // --- Apex Legends ---
    out.push_back(make("Apex Legends",
        "Код ошибки Leaf / Net 100",
        "Разрыв соединения с серверами EA, ошибки Net.",
        "Потери пакетов на маршруте до серверов EA.",
        "1. Включите MTU 1400 и TCP-оптимизацию.\n2. Смените DNS на 1.1.1.1.\n3. При серверной сети — стабилизация маршрута.",
        "mtu", "medium"));
    out.push_back(make("Apex Legends",
        "Высокий пинг ночью",
        "Пинг стабильно высокий в вечернее время.",
        "Перегрузка канала провайдера в часы пик.",
        "Используйте проводное подключение вместо Wi-Fi. Включите оптимизацию TCP. Программа покажет вечернее ухудшение в рекомендациях.",
        "tcp", "easy"));

    // --- Warzone / Call of Duty ---
    out.push_back(make("Warzone",
        "Stuttering при передвижении",
        "Картинка подёргивается при беге/повороте.",
        "Недостаточно оперативной памяти; виртуальная память не настроена.",
        "1. Включите «Оптимизацию виртуальной памяти» (вкладка «Оптимизация»).\n2. Закройте браузер и фоновые программы.",
        "vm", "medium"));
    out.push_back(make("Warzone",
        "Ошибка подключения к онлайну",
        "Игра не может соединиться со службами Call of Duty.",
        "DNS-резолвинг заблокирован или брандмауэр.",
        "Смените DNS на 1.1.1.1 через программу. Проверьте брандмауэр — программа покажет, что DNS не отвечает, в диагностике.",
        "dns", "medium"));

    // --- Minecraft ---
    out.push_back(make("Minecraft",
        "Лаги на серверах",
        "Высокий пинг на мультиплеере, сущности телепортируются.",
        "Высокая нагрузка на маршрут; UDP-пакеты теряются.",
        "1. Включите TCP-оптимизацию и MTU 1400.\n2. При серверной сети — мультимаршрут.",
        "mtu", "easy"));
    out.push_back(make("Minecraft",
        "Тормоза с шейдерами",
        "FPS падает при включении шейдеров.",
        "Game DVR пишет видео; план питания ограничивает CPU.",
        "Включите «Ускорение FPS»: отключение Game DVR и план «Высокая производительность» дают до +20% FPS.",
        "fps_boost", "easy"));

    // --- GTA V ---
    out.push_back(make("GTA V",
        "Игра вылетает на загрузке",
        "Вылет к рабочему столу после заставки.",
        "Повреждён кэш Social Club; недостаточно виртуальной памяти.",
        "1. Включите «Оптимизацию виртуальной памяти».\n2. Уберите фоновые overlay-программы — программа покажет их в диагностике.",
        "vm", "medium"));
    out.push_back(make("GTA V",
        "Лаги в онлайне (GTA Online)",
        "Игроки телепортируются, транспорт двигается рывками.",
        "Потери пакетов; медленный маршрут до серверов Rockstar.",
        "Включите TCP-оптимизацию и MTU 1400. Смените DNS на 1.1.1.1.",
        "mtu", "medium"));

    // --- Rocket League ---
    out.push_back(make("Rocket League",
        "Пинг высокий, мяч «летит рывками»",
        "Физика мяча дёргается из-за пинга.",
        "Нестабильная линия; большой MTU фрагментирует пакеты.",
        "Установите MTU 1400 и включите TCP-оптимизацию. Программа покажет потери в мониторинге.",
        "mtu", "easy"));

    // --- League of Legends ---
    out.push_back(make("League of Legends",
        "Задержка ввода (input lag)",
        "Клик/умение срабатывает с опозданием.",
        "Медленный маршрут до серверов Riot; высокая частота подтверждений TCP.",
        "Включите TCP-оптимизацию (адаптивные ACK). Смените DNS на 1.1.1.1.",
        "tcp", "easy"));
    out.push_back(make("League of Legends",
        "Вылет «Неожиданная ошибка»",
        "Клиент закрывается с окном ошибки.",
        "Повреждённый кэш клиента; конфликт с антивирусом.",
        "Перезапустите клиент через программу после диагностики. Проверьте, что антивирус не блокирует клиент.",
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

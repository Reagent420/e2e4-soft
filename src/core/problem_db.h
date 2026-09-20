#pragma once

#include <string>
#include <vector>

#include "diagnostics/diagnostic_types.h"

namespace gno {

struct ProblemEntry {
    std::string game;            // game display name
    std::string title;           // problem title
    std::string symptoms;        // what the user sees
    std::string cause;           // why it happens
    std::string solution;        // how to solve via our program (steps)
    std::string fix_action;      // id of an auto-fix the program can run (empty = manual)
    std::string difficulty;      // "easy" | "medium" | "hard"
};

class ProblemDb {
public:
    // Returns known problems for a game (case-insensitive match on display name).
    static std::vector<ProblemEntry> getForGame(const std::string& game_name);

    // All problems across all games.
    static std::vector<ProblemEntry> getAll();

    // List of games that have a known-problems entry.
    static std::vector<std::string> getKnownGames();

    // Case-insensitive substring search over title/symptoms/cause/solution.
    static std::vector<ProblemEntry> search(const std::string& game_name, const std::string& query);

    // Подбор статей под свежий диагноз маршрута: что почитать, если виновата
    // локальная сеть или маршрут можно улучшить. Раньше база знаний была
    // оторвана от диагностики — пользователь видел «проблема в локальной сети»
    // и должен был сам догадаться, что искать в списке из сотни статей.
    // Сопоставление идёт по ключевым словам исхода и метрик (задержка, потери,
    // джиттер) в тексте статьи; статьи той же игры получают приоритет.
    // InsufficientData → пустой список: советовать нечего.
    static std::vector<ProblemEntry> forDiagnostic(const DiagnosticReport& report);

    // Applies the auto-fix for an entry; returns a human-readable result string.
    static std::string applyAutoFix(const ProblemEntry& entry);
};

} // namespace gno
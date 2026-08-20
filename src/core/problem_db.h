#pragma once

#include <string>
#include <vector>

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

    // Applies the auto-fix for an entry; returns a human-readable result string.
    static std::string applyAutoFix(const ProblemEntry& entry);
};

} // namespace gno
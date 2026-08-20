#pragma once

#include <string>

namespace gno {

enum class QualityGrade {
    A_Plus = 0,
    A = 1,
    B = 2,
    C = 3,
    D = 4,
    F = 5,
    Unknown = 6
};

struct ConnectionQuality {
    QualityGrade grade = QualityGrade::Unknown;
    int score = 0; // 0-100
    std::string grade_str;
    std::string description;
    std::string color; // hex color for UI
    
    // Component scores
    int ping_score = 0;
    int jitter_score = 0;
    int loss_score = 0;
};

class ConnectionGrader {
public:
    static ConnectionQuality evaluate(double ping_ms, double jitter_ms, double packet_loss_percent);
    
    static QualityGrade gradeFromScore(int score);
    static std::string gradeToString(QualityGrade grade);
    static std::string gradeColor(QualityGrade grade);
    static std::string gradeDescription(QualityGrade grade);

private:
    static int calculatePingScore(double ping_ms);
    static int calculateJitterScore(double jitter_ms);
    static int calculateLossScore(double loss_percent);
};

} // namespace gno
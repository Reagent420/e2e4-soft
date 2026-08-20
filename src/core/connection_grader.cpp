#include "connection_grader.h"
#include <algorithm>

namespace gno {

ConnectionQuality ConnectionGrader::evaluate(double ping_ms, double jitter_ms, double packet_loss_percent) {
    ConnectionQuality result;
    
    result.ping_score = calculatePingScore(ping_ms);
    result.jitter_score = calculateJitterScore(jitter_ms);
    result.loss_score = calculateLossScore(packet_loss_percent);
    
    // Weighted average: ping 40%, jitter 30%, loss 30%
    result.score = static_cast<int>(
        result.ping_score * 0.4 + 
        result.jitter_score * 0.3 + 
        result.loss_score * 0.3
    );
    
    result.grade = gradeFromScore(result.score);
    result.grade_str = gradeToString(result.grade);
    result.description = gradeDescription(result.grade);
    result.color = gradeColor(result.grade);
    
    return result;
}

int ConnectionGrader::calculatePingScore(double ping_ms) {
    if (ping_ms <= 20) return 100;
    if (ping_ms <= 40) return 90;
    if (ping_ms <= 60) return 80;
    if (ping_ms <= 80) return 70;
    if (ping_ms <= 100) return 60;
    if (ping_ms <= 150) return 40;
    if (ping_ms <= 200) return 20;
    return 0;
}

int ConnectionGrader::calculateJitterScore(double jitter_ms) {
    if (jitter_ms <= 5) return 100;
    if (jitter_ms <= 10) return 90;
    if (jitter_ms <= 20) return 75;
    if (jitter_ms <= 30) return 60;
    if (jitter_ms <= 50) return 40;
    if (jitter_ms <= 100) return 20;
    return 0;
}

int ConnectionGrader::calculateLossScore(double loss_percent) {
    if (loss_percent <= 0.1) return 100;
    if (loss_percent <= 0.5) return 90;
    if (loss_percent <= 1.0) return 80;
    if (loss_percent <= 2.0) return 60;
    if (loss_percent <= 5.0) return 40;
    if (loss_percent <= 10.0) return 20;
    return 0;
}

QualityGrade ConnectionGrader::gradeFromScore(int score) {
    if (score >= 95) return QualityGrade::A_Plus;
    if (score >= 85) return QualityGrade::A;
    if (score >= 70) return QualityGrade::B;
    if (score >= 55) return QualityGrade::C;
    if (score >= 40) return QualityGrade::D;
    return QualityGrade::F;
}

std::string ConnectionGrader::gradeToString(QualityGrade grade) {
    switch (grade) {
        case QualityGrade::A_Plus: return "A+";
        case QualityGrade::A:      return "A";
        case QualityGrade::B:      return "B";
        case QualityGrade::C:      return "C";
        case QualityGrade::D:      return "D";
        case QualityGrade::F:      return "F";
        default:                   return "?";
    }
}

std::string ConnectionGrader::gradeColor(QualityGrade grade) {
    switch (grade) {
        case QualityGrade::A_Plus: return "#22C55E"; // green
        case QualityGrade::A:      return "#4ADE80";
        case QualityGrade::B:      return "#84CC16"; // lime
        case QualityGrade::C:      return "#EAB308"; // yellow
        case QualityGrade::D:      return "#F97316"; // orange
        case QualityGrade::F:      return "#EF4444"; // red
        default:                   return "#64748B"; // gray
    }
}

std::string ConnectionGrader::gradeDescription(QualityGrade grade) {
    switch (grade) {
        case QualityGrade::A_Plus: return "Excellent - Competitive gaming ready";
        case QualityGrade::A:      return "Great - Smooth gaming experience";
        case QualityGrade::B:      return "Good - Minor issues possible";
        case QualityGrade::C:      return "Fair - Noticeable lag at times";
        case QualityGrade::D:      return "Poor - Frequent lag/stutter";
        case QualityGrade::F:      return "Critical - Unplayable";
        default:                   return "Unknown";
    }
}

} // namespace gno
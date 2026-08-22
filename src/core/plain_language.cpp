#include "core/plain_language.h"

#include "core/capability_matrix.h"

#include <cstdio>

namespace gno {

namespace {

std::string fmt(double v, int precision = 1) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%.*f", precision, v);
    return buffer;
}

} // namespace

std::vector<PlainSection> PlainLanguageReport::build(double avg_rtt_ms, double jitter_ms,
                                                     double packet_loss_percent, bool network_ok,
                                                     const GameDiagnostics* launch, bool elevated) {
    std::vector<std::string> what_is_wrong;
    std::vector<std::string> what_we_can;
    std::vector<std::string> what_you_should;

    if (!network_ok) {
        what_is_wrong.push_back("Не удаётся связаться с сервером игры. Возможно, проблема с DNS или интернетом.");
        what_we_can.push_back("Переключить DNS на быстрый (1.1.1.1) - вкладка «Оптимизация Windows».");
    } else {
        if (packet_loss_percent > 2.0)
            what_is_wrong.push_back("Теряется " + fmt(packet_loss_percent) +
                                    "% пакетов - игра будет «дёргаться».");
        if (avg_rtt_ms > 80.0)
            what_is_wrong.push_back("Средняя задержка " + fmt(avg_rtt_ms, 0) +
                                    " мс - выше комфортных 80 мс.");
        if (jitter_ms > 15.0)
            what_is_wrong.push_back("Джиттер " + fmt(jitter_ms) + " мс - задержка нестабильна, возможны рывки.");
        if (what_is_wrong.empty())
            what_is_wrong.push_back("Сеть в порядке: " + fmt(avg_rtt_ms) + " мс средняя задержка, джиттер " +
                                    fmt(jitter_ms) + " мс, потери " + fmt(packet_loss_percent) + "%.");

        if (packet_loss_percent > 2.0 || jitter_ms > 15.0)
            what_we_can.push_back("Переключить DNS на 1.1.1.1 и отключить Game DVR - вкладка «Оптимизация Windows».");
        if (avg_rtt_ms > 80.0)
            what_you_should.push_back("Подключитесь кабелем вместо Wi-Fi или смените канал Wi-Fi на 5 ГГц.");
        if (packet_loss_percent > 5.0)
            what_you_should.push_back("Проверьте кабель/роутер; потери выше 5% обычно аппаратные или со стороны провайдера.");
    }

    if (launch != nullptr) {
        for (const auto& check : launch->checks) {
            if (check.severity == 2) {
                what_is_wrong.push_back(check.name + ". " + check.recommendation);
                if (!check.fix_action.empty())
                    what_we_can.push_back("Автофикс \"" + check.fix_action + "\" доступен во вкладке «Оптимизация Windows».");
            } else if (check.severity == 1) {
                what_is_wrong.push_back(check.name + ": " + check.detail);
                what_you_should.push_back(check.recommendation);
            }
        }
    }

    if (!elevated)
        what_you_should.push_back("Запустите GNO от имени администратора, чтобы стали доступны все исправления.");

    if (what_we_can.empty())
        what_we_can.push_back("Автоисправления не требуются - либо всё в порядке, либо проблема вне зоны GNO (см. «Чего мы не делаем»).");

    std::vector<PlainSection> sections;
    sections.push_back({"Что не так", what_is_wrong});
    sections.push_back({"Что мы можем сделать за вас", what_we_can});
    sections.push_back({"Что вам стоит сделать самим",
                        what_you_should.empty() ? std::vector<std::string>{"Ничего - проблем не обнаружено."}
                                                : what_you_should});

    std::vector<std::string> cannot;
    for (const auto& c : CapabilityMatrix::cannotDo())
        cannot.push_back(c.title + ": " + c.detail);
    sections.push_back({"Чего мы не делаем", cannot});
    return sections;
}

} // namespace gno

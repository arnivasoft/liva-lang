#include "liva/Common/Diagnostics.h"
#include <iostream>

namespace liva {

DiagnosticsEngine::DiagInfo DiagnosticsEngine::getDiagInfo(DiagID id) {
    static const DiagInfo infos[] = {
#define DIAG(ID, Level, Message) {DiagLevel::Level, Message},
// Map lowercase level names to enum values
#define error Error
#define warning Warning
#define note Note
#include "liva/Common/DiagnosticKinds.def"
#undef error
#undef warning
#undef note
    };

    auto index = static_cast<uint16_t>(id);
    if (index < static_cast<uint16_t>(DiagID::NUM_DIAGNOSTICS)) {
        return infos[index];
    }
    return {DiagLevel::Error, "unknown diagnostic"};
}

std::string DiagnosticsEngine::formatMessage(const char *fmt) {
    return std::string(fmt);
}

void DiagnosticsEngine::formatMessageImpl(std::string &result, const char *fmt, int /*argIndex*/) {
    result += fmt;
}

std::string DiagnosticsEngine::formatDiagnostic(const Diagnostic &diag) const {
    std::string result;

    if (sourceManager_) {
        result += sourceManager_->formatLocation(diag.location);
        result += ": ";
    }

    switch (diag.level) {
    case DiagLevel::Error:
        result += "error: ";
        break;
    case DiagLevel::Warning:
        result += "warning: ";
        break;
    case DiagLevel::Note:
        result += "note: ";
        break;
    }

    result += diag.message;
    return result;
}

void DiagnosticsEngine::printToStderr(const Diagnostic &diag, const SourceManager *sm) {
    DiagnosticsEngine temp(sm);
    std::cerr << temp.formatDiagnostic(diag) << "\n";

    // Print source line and caret if we have a source manager
    if (sm && diag.location.isValid()) {
        auto line = sm->getLineContent(diag.location);
        if (!line.empty()) {
            std::cerr << "  " << line << "\n";
            std::cerr << "  ";
            for (uint32_t i = 1; i < diag.location.column; ++i) {
                std::cerr << ' ';
            }
            std::cerr << "^\n";
        }
    }
}

} // namespace liva

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
    case DiagLevel::Help:
        result += "help: ";
        break;
    }

    result += diag.message;
    return result;
}

void DiagnosticsEngine::printToStderr(const Diagnostic &diag, const SourceManager *sm,
                                      bool useColor) {
    // Color strings — empty if color disabled
    const char *sevColor = "";
    const char *blue     = "";
    const char *white    = "";
    const char *reset    = "";

    if (useColor) {
        white = color::BoldWhite;
        blue  = color::BoldBlue;
        reset = color::Reset;
        switch (diag.level) {
        case DiagLevel::Error:   sevColor = color::BoldRed;    break;
        case DiagLevel::Warning: sevColor = color::BoldYellow; break;
        case DiagLevel::Note:    sevColor = color::BoldCyan;   break;
        case DiagLevel::Help:    sevColor = color::BoldGreen;  break;
        }
    }

    // Severity label
    const char *label = "error";
    switch (diag.level) {
    case DiagLevel::Error:   label = "error";   break;
    case DiagLevel::Warning: label = "warning"; break;
    case DiagLevel::Note:    label = "note";    break;
    case DiagLevel::Help:    label = "help";    break;
    }

    // Line 1: severity: message
    std::cerr << sevColor << label << ": " << white << diag.message << reset << "\n";

    // Source snippet (Rust-style)
    if (sm && diag.location.isValid()) {
        std::string filename(sm->getFilename());
        std::string lineStr = std::to_string(diag.location.line);
        std::string colStr  = std::to_string(diag.location.column);
        size_t gutterWidth  = lineStr.size();

        // --> file:line:col (not for Help diagnostics)
        if (diag.level != DiagLevel::Help) {
            std::cerr << std::string(gutterWidth + 1, ' ')
                      << blue << "--> " << reset
                      << filename << ":" << lineStr << ":" << colStr << "\n";
        }

        // Empty gutter line
        std::cerr << std::string(gutterWidth + 1, ' ')
                  << blue << "|" << reset << "\n";

        auto sourceLine = sm->getLineContent(diag.location);

        if (diag.level == DiagLevel::Help && !diag.suggestion.empty()
            && !sourceLine.empty()) {
            // Help with suggestion: show modified source line with tildes
            std::string modifiedLine(sourceLine);
            uint32_t col0 = diag.location.column - 1;
            uint32_t replaceLen = diag.highlightLength > 0 ? diag.highlightLength : 1;
            if (col0 < modifiedLine.size()) {
                size_t actualReplace = (col0 + replaceLen <= modifiedLine.size())
                    ? replaceLen : modifiedLine.size() - col0;
                modifiedLine.replace(col0, actualReplace, diag.suggestion);
            }

            std::cerr << blue << lineStr << " | " << reset
                      << modifiedLine << "\n";

            // Tilde underline for suggestion
            std::cerr << std::string(gutterWidth + 1, ' ')
                      << blue << "| " << reset;
            for (uint32_t i = 1; i < diag.location.column; ++i)
                std::cerr << ' ';
            std::cerr << sevColor
                      << std::string(diag.suggestion.size(), '~')
                      << reset << "\n";
        } else if (!sourceLine.empty()) {
            // Error/Warning/Note (or Help without suggestion): source + caret underline
            std::cerr << blue << lineStr << " | " << reset
                      << sourceLine << "\n";

            // Caret/underline line
            uint32_t len = diag.highlightLength > 0 ? diag.highlightLength : 1;
            std::cerr << std::string(gutterWidth + 1, ' ')
                      << blue << "| " << reset;
            for (uint32_t i = 1; i < diag.location.column; ++i)
                std::cerr << ' ';
            std::cerr << sevColor << std::string(len, '^');

            // Inline label after underline
            if (!diag.inlineLabel.empty()) {
                std::cerr << " " << diag.inlineLabel;
            }
            std::cerr << reset << "\n";
        }
    }
}

void DiagnosticsEngine::reportHelp(SourceLocation loc, uint32_t length,
                                    const std::string &helpMessage,
                                    const std::string &suggestion,
                                    DiagID id) {
    Diagnostic d;
    d.id = id;
    d.level = DiagLevel::Help;
    d.location = loc;
    d.message = helpMessage;
    d.highlightLength = length;
    d.suggestion = suggestion;
    diagnostics_.push_back(std::move(d));
    if (printCallback_) {
        printCallback_(diagnostics_.back());
    }
}

} // namespace liva

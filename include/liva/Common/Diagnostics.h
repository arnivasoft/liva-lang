#pragma once

#include "liva/Common/SourceLocation.h"
#include "liva/Common/TerminalColors.h"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace liva {

/// Severity levels for diagnostics
enum class DiagLevel : uint8_t {
    Note,
    Warning,
    Error,
};

/// Unique identifier for each diagnostic message
enum class DiagID : uint16_t {
#define DIAG(ID, Level, Message) ID,
#include "liva/Common/DiagnosticKinds.def"
    NUM_DIAGNOSTICS
};

/// A single diagnostic message with location info
struct Diagnostic {
    DiagID id;
    DiagLevel level;
    SourceLocation location;
    std::string message;
};

/// Diagnostic engine: collects and reports compiler diagnostics
class DiagnosticsEngine {
public:
    explicit DiagnosticsEngine(const SourceManager *sm = nullptr) : sourceManager_(sm) {}

    void setSourceManager(const SourceManager *sm) { sourceManager_ = sm; }

    /// Report a diagnostic with formatted arguments
    template <typename... Args>
    void report(SourceLocation loc, DiagID id, Args &&...args) {
        // If we already hit the max error limit, suppress further diagnostics
        if (hasMaxErrors()) return;

        auto [level, fmt] = getDiagInfo(id);
        std::string msg = formatMessage(fmt, std::forward<Args>(args)...);
        diagnostics_.push_back({id, level, loc, std::move(msg)});
        if (level == DiagLevel::Error) {
            ++errorCount_;
        }
        if (printCallback_) {
            printCallback_(diagnostics_.back());
        }

        // Emit a final "too many errors" diagnostic when we just hit the limit
        if (level == DiagLevel::Error && hasMaxErrors()) {
            auto [limLevel, limFmt] = getDiagInfo(DiagID::err_too_many_errors);
            std::string limMsg = formatMessage(limFmt);
            diagnostics_.push_back({DiagID::err_too_many_errors, limLevel, loc, std::move(limMsg)});
            if (printCallback_) {
                printCallback_(diagnostics_.back());
            }
        }
    }

    bool hasErrors() const { return errorCount_ > 0; }
    uint32_t getErrorCount() const { return errorCount_; }
    bool hasMaxErrors() const { return maxErrors_ > 0 && errorCount_ >= static_cast<uint32_t>(maxErrors_); }
    void setMaxErrors(int max) { maxErrors_ = max; }
    const std::vector<Diagnostic> &getDiagnostics() const { return diagnostics_; }

    void clear() {
        diagnostics_.clear();
        errorCount_ = 0;
    }

    /// Set a callback to print diagnostics as they are reported
    using PrintCallback = std::function<void(const Diagnostic &)>;
    void setPrintCallback(PrintCallback cb) { printCallback_ = std::move(cb); }

    /// Format a diagnostic for display
    std::string formatDiagnostic(const Diagnostic &diag) const;

    /// Default print callback that writes to stderr (Rust-style format)
    static void printToStderr(const Diagnostic &diag, const SourceManager *sm,
                              bool useColor = false);

private:
    struct DiagInfo {
        DiagLevel level;
        const char *message;
    };

    static DiagInfo getDiagInfo(DiagID id);

    // Format message with argument substitution (%0, %1, etc.)
    static std::string formatMessage(const char *fmt);

    template <typename T, typename... Rest>
    static std::string formatMessage(const char *fmt, T &&first, Rest &&...rest) {
        std::string result;
        int argIndex = 0;
        formatMessageImpl(result, fmt, argIndex, std::forward<T>(first),
                          std::forward<Rest>(rest)...);
        return result;
    }

    static void formatMessageImpl(std::string &result, const char *fmt, int argIndex);

    template <typename T, typename... Rest>
    static void formatMessageImpl(std::string &result, const char *fmt, int argIndex, T &&first,
                                  Rest &&...rest) {
        while (*fmt) {
            if (*fmt == '%' && *(fmt + 1)) {
                int idx = *(fmt + 1) - '0';
                if (idx == argIndex) {
                    appendArg(result, std::forward<T>(first));
                    fmt += 2;
                    formatMessageImpl(result, fmt, argIndex + 1, std::forward<Rest>(rest)...);
                    return;
                }
            }
            result += *fmt++;
        }
    }

    static void appendArg(std::string &result, std::string_view arg) { result += arg; }
    static void appendArg(std::string &result, const char *arg) { result += arg; }
    static void appendArg(std::string &result, const std::string &arg) { result += arg; }
    static void appendArg(std::string &result, char arg) { result += arg; }
    static void appendArg(std::string &result, int arg) { result += std::to_string(arg); }
    static void appendArg(std::string &result, uint32_t arg) { result += std::to_string(arg); }
    static void appendArg(std::string &result, size_t arg) { result += std::to_string(arg); }

    const SourceManager *sourceManager_ = nullptr;
    std::vector<Diagnostic> diagnostics_;
    uint32_t errorCount_ = 0;
    int maxErrors_ = 20;  // default: stop after 20 errors (0 = unlimited)
    PrintCallback printCallback_;
};

} // namespace liva

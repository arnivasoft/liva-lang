#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace liva {

enum class ColorMode : uint8_t {
    Auto,
    Always,
    Never,
};

// ANSI escape sequences
namespace color {
inline constexpr const char *Reset     = "\033[0m";
inline constexpr const char *Bold      = "\033[1m";
inline constexpr const char *BoldRed   = "\033[1;31m";
inline constexpr const char *BoldYellow = "\033[1;33m";
inline constexpr const char *BoldCyan  = "\033[1;36m";
inline constexpr const char *BoldBlue  = "\033[1;34m";
inline constexpr const char *BoldWhite = "\033[1;37m";
} // namespace color

/// Check if stderr is connected to a terminal
inline bool isStderrTTY() {
#ifdef _WIN32
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(STDERR_FILENO) != 0;
#endif
}

/// Enable VT100 escape processing on Windows console
inline void enableWindowsVT100() {
#ifdef _WIN32
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hErr, &mode)) return;
    SetConsoleMode(hErr, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

/// Determine whether to use color output based on mode
inline bool shouldUseColor(ColorMode mode) {
    switch (mode) {
    case ColorMode::Always:
        enableWindowsVT100();
        return true;
    case ColorMode::Never:
        return false;
    case ColorMode::Auto: {
        // Respect NO_COLOR convention (https://no-color.org/)
        const char *noColor = std::getenv("NO_COLOR");
        if (noColor && noColor[0] != '\0')
            return false;
        if (!isStderrTTY())
            return false;
        enableWindowsVT100();
        return true;
    }
    }
    return false;
}

} // namespace liva

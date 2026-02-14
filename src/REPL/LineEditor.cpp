#include "liva/REPL/LineEditor.h"
#include <cstdio>
#include <iostream>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace liva {

LineEditor::LineEditor() = default;

LineEditor::~LineEditor() {
#ifndef _WIN32
    disableRawMode();
#endif
}

void LineEditor::setCompletionCallback(CompletionCallback cb) {
    completionCb_ = std::move(cb);
}

// ── Platform-specific raw mode ──────────────────────────────────────────

#ifdef _WIN32

void LineEditor::enableRawMode() {
    // On Windows, _getch() already reads raw characters — no setup needed.
    // We just disable echo/line-editing on the console input handle.
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hIn, &mode);
    // Disable line input and echo
    SetConsoleMode(hIn, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
}

void LineEditor::disableRawMode() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hIn, &mode);
    SetConsoleMode(hIn, mode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
}

int LineEditor::readChar() {
    int c = _getch();
    if (c == 0 || c == 0xE0) {
        // Extended key — read scan code
        int sc = _getch();
        switch (sc) {
        case 75: return 1001; // Left arrow
        case 77: return 1002; // Right arrow
        case 71: return 1003; // Home
        case 79: return 1004; // End
        case 83: return 1005; // Delete
        default: return -1;   // Ignore other extended keys
        }
    }
    return c;
}

#else // POSIX

void LineEditor::enableRawMode() {
    if (rawMode_)
        return;
    if (tcgetattr(STDIN_FILENO, &origTermios_) == -1)
        return;
    struct termios raw = origTermios_;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    rawMode_ = true;
}

void LineEditor::disableRawMode() {
    if (!rawMode_)
        return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios_);
    rawMode_ = false;
}

int LineEditor::readChar() {
    char c;
    if (read(STDIN_FILENO, &c, 1) != 1)
        return -1;
    // Handle escape sequences (arrows)
    if (c == 27) {
        char seq[2];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return 27;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return 27;
        if (seq[0] == '[') {
            switch (seq[1]) {
            case 'D': return 1001; // Left
            case 'C': return 1002; // Right
            case 'H': return 1003; // Home
            case 'F': return 1004; // End
            case '3': {
                char tilde;
                if (read(STDIN_FILENO, &tilde, 1) == 1 && tilde == '~')
                    return 1005; // Delete
                return -1;
            }
            default: return -1;
            }
        }
        return 27;
    }
    return static_cast<unsigned char>(c);
}

#endif

// ── refreshLine ─────────────────────────────────────────────────────────

void LineEditor::refreshLine(const std::string &prompt, const std::string &buf,
                             size_t cursor) {
    // Move to beginning of line, clear, print prompt+buf, reposition cursor
    std::string out;
    out += '\r';                             // carriage return
    out += prompt;
    out += buf;
    out += "\x1b[K";                         // clear to end of line
    // Reposition cursor: move to prompt + cursor offset
    size_t cursorCol = prompt.size() + cursor;
    out += '\r';
    if (cursorCol > 0) {
        out += "\x1b[" + std::to_string(cursorCol) + "C";
    }
    std::cout << out << std::flush;
}

// ── handleTab ───────────────────────────────────────────────────────────

void LineEditor::handleTab(const std::string &prompt, std::string &buf,
                           size_t &cursor) {
    if (!completionCb_)
        return;

    auto candidates = completionCb_(buf, cursor);
    if (candidates.empty())
        return;

    if (candidates.size() == 1) {
        // Single match — complete it
        // Find the start of the current word being completed
        size_t start = cursor;
        bool isCmd = false;
        {
            size_t fs = buf.find_first_not_of(" \t");
            if (fs != std::string::npos && buf[fs] == ':')
                isCmd = true;
        }
        while (start > 0) {
            char c = buf[start - 1];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' ||
                (isCmd && c == ':')) {
                --start;
            } else {
                break;
            }
        }
        // Replace prefix with full completion
        buf = buf.substr(0, start) + candidates[0] + buf.substr(cursor);
        cursor = start + candidates[0].size();
        refreshLine(prompt, buf, cursor);
    } else {
        // Multiple matches — find common prefix
        std::string common = candidates[0];
        for (size_t i = 1; i < candidates.size(); ++i) {
            size_t j = 0;
            while (j < common.size() && j < candidates[i].size() &&
                   common[j] == candidates[i][j])
                ++j;
            common = common.substr(0, j);
        }

        // Find the current word start
        size_t start = cursor;
        bool isCmd = false;
        {
            size_t fs = buf.find_first_not_of(" \t");
            if (fs != std::string::npos && buf[fs] == ':')
                isCmd = true;
        }
        while (start > 0) {
            char c = buf[start - 1];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '_' ||
                (isCmd && c == ':')) {
                --start;
            } else {
                break;
            }
        }
        std::string currentWord = buf.substr(start, cursor - start);

        // If common prefix is longer than current word, extend
        if (common.size() > currentWord.size()) {
            buf = buf.substr(0, start) + common + buf.substr(cursor);
            cursor = start + common.size();
            refreshLine(prompt, buf, cursor);
        } else {
            // Show all candidates below the current line
            std::cout << "\n";
            for (const auto &c : candidates)
                std::cout << c << "  ";
            std::cout << "\n";
            refreshLine(prompt, buf, cursor);
        }
    }
}

// ── readLine ────────────────────────────────────────────────────────────

bool LineEditor::readLine(const std::string &prompt, std::string &output) {
    // If stdin is not a TTY (piped), fall back to std::getline
#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD consoleMode;
    bool isTTY = (GetConsoleMode(hIn, &consoleMode) != 0);
#else
    bool isTTY = isatty(STDIN_FILENO);
#endif
    if (!isTTY) {
        std::cout << prompt << std::flush;
        if (!std::getline(std::cin, output))
            return false;
        return true;
    }

    enableRawMode();

    std::string buf;
    size_t cursor = 0;

    // Print initial prompt
    std::cout << prompt << std::flush;

    while (true) {
        int c = readChar();
        if (c == -1) {
            // EOF
            disableRawMode();
            return false;
        }

        switch (c) {
        case 13: // Enter (CR)
        case 10: // Enter (LF)
            disableRawMode();
            std::cout << "\n";
            output = buf;
            return true;

        case 9: // Tab
            handleTab(prompt, buf, cursor);
            break;

        case 3: // Ctrl+C — cancel line
            disableRawMode();
            std::cout << "^C\n";
            output.clear();
            return true;

        case 4: // Ctrl+D — EOF on empty line
            if (buf.empty()) {
                disableRawMode();
                return false;
            }
            // Non-empty: treat as delete-forward
            if (cursor < buf.size()) {
                buf.erase(cursor, 1);
                refreshLine(prompt, buf, cursor);
            }
            break;

        case 127: // Backspace (POSIX)
        case 8:   // Backspace (Windows)
            if (cursor > 0) {
                buf.erase(cursor - 1, 1);
                --cursor;
                refreshLine(prompt, buf, cursor);
            }
            break;

        case 1: // Ctrl+A — Home
        case 1003: // Home key
            cursor = 0;
            refreshLine(prompt, buf, cursor);
            break;

        case 5: // Ctrl+E — End
        case 1004: // End key
            cursor = buf.size();
            refreshLine(prompt, buf, cursor);
            break;

        case 1001: // Left arrow
            if (cursor > 0) {
                --cursor;
                refreshLine(prompt, buf, cursor);
            }
            break;

        case 1002: // Right arrow
            if (cursor < buf.size()) {
                ++cursor;
                refreshLine(prompt, buf, cursor);
            }
            break;

        case 1005: // Delete
            if (cursor < buf.size()) {
                buf.erase(cursor, 1);
                refreshLine(prompt, buf, cursor);
            }
            break;

        case 21: // Ctrl+U — kill line before cursor
            buf.erase(0, cursor);
            cursor = 0;
            refreshLine(prompt, buf, cursor);
            break;

        case 11: // Ctrl+K — kill line after cursor
            buf.erase(cursor);
            refreshLine(prompt, buf, cursor);
            break;

        default:
            if (c >= 32 && c < 127) {
                buf.insert(cursor, 1, static_cast<char>(c));
                ++cursor;
                refreshLine(prompt, buf, cursor);
            }
            break;
        }
    }
}

} // namespace liva

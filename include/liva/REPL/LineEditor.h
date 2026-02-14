#pragma once

#include <functional>
#include <string>
#include <vector>

#ifndef _WIN32
#include <termios.h>
#endif

namespace liva {

/// Minimal line editor with tab-completion support.
/// Uses raw terminal I/O (Windows: conio, POSIX: termios).
class LineEditor {
public:
    using CompletionCallback =
        std::function<std::vector<std::string>(const std::string &, size_t)>;

    LineEditor();
    ~LineEditor();

    /// Set the callback invoked on Tab press.
    void setCompletionCallback(CompletionCallback cb);

    /// Read a line from the terminal. Returns false on EOF.
    bool readLine(const std::string &prompt, std::string &output);

private:
    CompletionCallback completionCb_;

    void enableRawMode();
    void disableRawMode();
    int readChar();
    void refreshLine(const std::string &prompt, const std::string &buf,
                     size_t cursor);
    void handleTab(const std::string &prompt, std::string &buf, size_t &cursor);

#ifndef _WIN32
    struct termios origTermios_;
    bool rawMode_ = false;
#endif
};

} // namespace liva

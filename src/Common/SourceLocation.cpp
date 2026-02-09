#include "liva/Common/SourceLocation.h"

namespace liva {

std::string_view SourceManager::getLineContent(const SourceLocation &loc) const {
    if (!loc.isValid() || source_.empty())
        return {};

    // Find line start
    size_t lineStart = 0;
    uint32_t currentLine = 1;
    for (size_t i = 0; i < source_.size() && currentLine < loc.line; ++i) {
        if (source_[i] == '\n') {
            ++currentLine;
            lineStart = i + 1;
        }
    }

    // Find line end
    size_t lineEnd = source_.find('\n', lineStart);
    if (lineEnd == std::string::npos) {
        lineEnd = source_.size();
    }

    return std::string_view(source_).substr(lineStart, lineEnd - lineStart);
}

std::string SourceManager::formatLocation(const SourceLocation &loc) const {
    if (!loc.isValid())
        return "<invalid location>";
    return std::string(filename_) + ":" + std::to_string(loc.line) + ":" +
           std::to_string(loc.column);
}

} // namespace liva

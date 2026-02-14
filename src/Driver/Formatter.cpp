#include "liva/Driver/Driver.h"
#include <string>
#include <vector>

namespace liva {

std::string formatLivaSource(const std::string &content) {
    // Split content into lines
    std::vector<std::string> lines;
    std::string currentLine;
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\n') {
            lines.push_back(currentLine);
            currentLine.clear();
        } else if (content[i] == '\r') {
            continue;
        } else {
            currentLine += content[i];
        }
    }
    if (!currentLine.empty() || (!content.empty() && content.back() == '\n')) {
        lines.push_back(currentLine);
    }

    // Format each line with brace-depth indentation (4 spaces)
    int depth = 0;
    std::string formatted;
    for (size_t i = 0; i < lines.size(); ++i) {
        // Trim leading and trailing whitespace
        size_t start = 0;
        while (start < lines[i].size() &&
               (lines[i][start] == ' ' || lines[i][start] == '\t'))
            ++start;
        size_t end = lines[i].size();
        while (end > start &&
               (lines[i][end - 1] == ' ' || lines[i][end - 1] == '\t'))
            --end;
        std::string trimmed = lines[i].substr(start, end - start);

        // Count braces in this line
        int opens = 0;
        int closes = 0;
        for (char c : trimmed) {
            if (c == '{') ++opens;
            else if (c == '}') ++closes;
        }

        // If line starts with '}', this line should be at reduced depth
        bool leadingClose = (!trimmed.empty() && trimmed[0] == '}');
        int lineDepth = depth;
        if (leadingClose) {
            lineDepth = depth - 1;
            if (lineDepth < 0) lineDepth = 0;
        }

        // Build indented line
        std::string indent(static_cast<size_t>(lineDepth) * 4, ' ');
        if (!trimmed.empty()) {
            formatted += indent + trimmed;
        }
        if (i + 1 < lines.size()) {
            formatted += "\n";
        }

        // Update depth: add opens, subtract closes
        depth += opens - closes;
        if (depth < 0) depth = 0;
    }

    return formatted;
}

} // namespace liva

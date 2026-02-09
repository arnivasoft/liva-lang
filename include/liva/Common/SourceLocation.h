#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace liva {

/// Represents a position in source code
struct SourceLocation {
    uint32_t line = 1;
    uint32_t column = 1;
    uint32_t offset = 0;

    bool isValid() const { return line > 0; }

    static SourceLocation invalid() { return {0, 0, 0}; }

    bool operator==(const SourceLocation &other) const {
        return line == other.line && column == other.column && offset == other.offset;
    }

    bool operator<(const SourceLocation &other) const { return offset < other.offset; }
};

/// Represents a range in source code
struct SourceRange {
    SourceLocation start;
    SourceLocation end;

    bool isValid() const { return start.isValid() && end.isValid(); }

    static SourceRange invalid() {
        return {SourceLocation::invalid(), SourceLocation::invalid()};
    }
};

/// Holds the source file content and provides location services
class SourceManager {
public:
    SourceManager(std::string filename, std::string source)
        : filename_(std::move(filename)), source_(std::move(source)) {}

    std::string_view getFilename() const { return filename_; }
    std::string_view getSource() const { return source_; }

    /// Get the line content at a given location
    std::string_view getLineContent(const SourceLocation &loc) const;

    /// Format location as "filename:line:column"
    std::string formatLocation(const SourceLocation &loc) const;

private:
    std::string filename_;
    std::string source_;
};

} // namespace liva

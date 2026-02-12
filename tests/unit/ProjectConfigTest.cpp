#include "liva/Driver/ProjectConfig.h"
#include <gtest/gtest.h>

using namespace liva;

// === TOML Parser: Basic Values ===

TEST(TOMLParser, EmptyDocument) {
    auto result = parseTOML("");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.doc.sections.empty());
}

TEST(TOMLParser, CommentOnly) {
    auto result = parseTOML("# this is a comment\n# another comment\n");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.doc.sections.empty());
}

TEST(TOMLParser, StringValue) {
    auto result = parseTOML("[project]\nname = \"hello\"\n");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.doc.getString("project", "name"), "hello");
}

TEST(TOMLParser, IntegerValue) {
    auto result = parseTOML("[build]\nopt-level = 2\n");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.doc.getInt("build", "opt-level"), 2);
}

TEST(TOMLParser, BooleanValues) {
    auto result = parseTOML("[build]\ndebug-info = true\nstrip = false\n");
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.doc.getBool("build", "debug-info"));
    EXPECT_FALSE(result.doc.getBool("build", "strip"));
}

TEST(TOMLParser, StringArray) {
    auto result = parseTOML("[paths]\nmodules = [\"src\", \"lib\"]\n");
    EXPECT_TRUE(result.success);
    auto arr = result.doc.getStringArray("paths", "modules");
    ASSERT_EQ(arr.size(), 2u);
    EXPECT_EQ(arr[0], "src");
    EXPECT_EQ(arr[1], "lib");
}

TEST(TOMLParser, EmptyStringArray) {
    auto result = parseTOML("[paths]\nmodules = []\n");
    EXPECT_TRUE(result.success);
    auto arr = result.doc.getStringArray("paths", "modules");
    EXPECT_TRUE(arr.empty());
}

TEST(TOMLParser, StringEscapes) {
    auto result = parseTOML("[test]\nval = \"line1\\nline2\\ttab\\\\slash\"\n");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.doc.getString("test", "val"), "line1\nline2\ttab\\slash");
}

// === TOML Parser: Edge Cases ===

TEST(TOMLParser, MultipleSections) {
    std::string toml =
        "[project]\nname = \"app\"\n"
        "[build]\nopt-level = 2\n"
        "[paths]\nmodules = [\"lib\"]\n";
    auto result = parseTOML(toml);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.doc.getString("project", "name"), "app");
    EXPECT_EQ(result.doc.getInt("build", "opt-level"), 2);
    auto arr = result.doc.getStringArray("paths", "modules");
    ASSERT_EQ(arr.size(), 1u);
    EXPECT_EQ(arr[0], "lib");
}

TEST(TOMLParser, InlineComment) {
    auto result = parseTOML("[project]\nopt = 3 # optimization\n");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.doc.getInt("project", "opt"), 3);
}

TEST(TOMLParser, ExtraWhitespace) {
    auto result = parseTOML("[project]\n  name  =  \"app\"  \n");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.doc.getString("project", "name"), "app");
}

TEST(TOMLParser, WindowsLineEndings) {
    auto result = parseTOML("[project]\r\nname = \"app\"\r\nversion = \"1.0\"\r\n");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.doc.getString("project", "name"), "app");
    EXPECT_EQ(result.doc.getString("project", "version"), "1.0");
}

TEST(TOMLParser, DefaultValues) {
    auto result = parseTOML("");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.doc.getString("missing", "key", "default"), "default");
    EXPECT_EQ(result.doc.getInt("missing", "key", 42), 42);
    EXPECT_TRUE(result.doc.getBool("missing", "key", true));
    EXPECT_TRUE(result.doc.getStringArray("missing", "key").empty());
}

TEST(TOMLParser, SingleElementArray) {
    auto result = parseTOML("[paths]\nmodules = [\"src\"]\n");
    EXPECT_TRUE(result.success);
    auto arr = result.doc.getStringArray("paths", "modules");
    ASSERT_EQ(arr.size(), 1u);
    EXPECT_EQ(arr[0], "src");
}

// === TOML Parser: Errors ===

TEST(TOMLParser, ErrorUnterminatedString) {
    auto result = parseTOML("[project]\nname = \"hello\n");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorLine, 2);
}

TEST(TOMLParser, ErrorUnterminatedSection) {
    auto result = parseTOML("[project\nname = \"hello\"\n");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorLine, 1);
}

TEST(TOMLParser, ErrorMissingEquals) {
    auto result = parseTOML("[project]\nname \"hello\"\n");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorLine, 2);
}

TEST(TOMLParser, ErrorUnterminatedArray) {
    auto result = parseTOML("[paths]\nmodules = [\"src\"\n");
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorLine, 2);
}

// === ProjectConfig Loading ===

TEST(ProjectConfig, FullConfig) {
    std::string toml =
        "[project]\nname = \"myapp\"\nversion = \"1.2.3\"\nentry = \"src/main.liva\"\n"
        "[build]\nopt-level = 2\ndebug-info = true\n"
        "[paths]\nmodules = [\"lib\", \"vendor\"]\n";
    auto result = parseTOML(toml);
    ASSERT_TRUE(result.success);
    auto cfg = loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.name, "myapp");
    EXPECT_EQ(cfg.version, "1.2.3");
    EXPECT_EQ(cfg.entry, "src/main.liva");
    EXPECT_EQ(cfg.optLevel, 2);
    EXPECT_TRUE(cfg.debugInfo);
    ASSERT_EQ(cfg.modulePaths.size(), 2u);
    EXPECT_EQ(cfg.modulePaths[0], "lib");
    EXPECT_EQ(cfg.modulePaths[1], "vendor");
}

TEST(ProjectConfig, DefaultConfig) {
    auto result = parseTOML("");
    ASSERT_TRUE(result.success);
    auto cfg = loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.name, "untitled");
    EXPECT_EQ(cfg.version, "0.1.0");
    EXPECT_EQ(cfg.entry, "main.liva");
    EXPECT_EQ(cfg.optLevel, 0);
    EXPECT_FALSE(cfg.debugInfo);
    EXPECT_TRUE(cfg.modulePaths.empty());
}

TEST(ProjectConfig, PartialConfig) {
    auto result = parseTOML("[project]\nname = \"partial\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.name, "partial");
    EXPECT_EQ(cfg.version, "0.1.0");
    EXPECT_EQ(cfg.entry, "main.liva");
    EXPECT_EQ(cfg.optLevel, 0);
}

TEST(ProjectConfig, OptLevelBounds) {
    auto result = parseTOML("[build]\nopt-level = 3\n");
    ASSERT_TRUE(result.success);
    auto cfg = loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.optLevel, 3);
}

// === Path Utilities ===

TEST(PathUtils, JoinPath) {
    EXPECT_EQ(joinPath("foo", "bar"), "foo/bar");
    EXPECT_EQ(joinPath("foo/", "bar"), "foo/bar");
    EXPECT_EQ(joinPath("", "bar"), "bar");
    EXPECT_EQ(joinPath("foo", ""), "foo");
}

TEST(PathUtils, GetDirectoryOf) {
    EXPECT_EQ(getDirectoryOf("/home/user/file.txt"), "/home/user");
    EXPECT_EQ(getDirectoryOf("file.txt"), ".");
}

TEST(PathUtils, GetDirectoryOfWindows) {
    EXPECT_EQ(getDirectoryOf("C:\\Users\\test\\f.toml"), "C:\\Users\\test");
}

TEST(PathUtils, GetCurrentDirectory) {
    std::string cwd = getCurrentDirectory();
    EXPECT_FALSE(cwd.empty());
    EXPECT_NE(cwd, ".");
}

TEST(PathUtils, FileExistsNonExistent) {
    EXPECT_FALSE(fileExists("__nonexistent_file_12345__.txt"));
}

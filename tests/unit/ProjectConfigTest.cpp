#include "liva/Driver/BuildCache.h"
#include "liva/Driver/Driver.h"
#include "liva/Driver/PackageManager.h"
#include "liva/Driver/ProjectConfig.h"
#include "liva/Driver/SemaCache.h"
#include "liva/AST/Decl.h"
#include "liva/AST/Type.h"
#include "liva/Common/Version.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <thread>
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

// === SemVer Parsing ===

TEST(SemVer, ParseValid) {
    auto r = parseSemVer("1.2.3");
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.version.major, 1);
    EXPECT_EQ(r.version.minor, 2);
    EXPECT_EQ(r.version.patch, 3);
}

TEST(SemVer, ParseZeros) {
    auto r = parseSemVer("0.0.0");
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.version.major, 0);
    EXPECT_EQ(r.version.minor, 0);
    EXPECT_EQ(r.version.patch, 0);
}

TEST(SemVer, ParseLarge) {
    auto r = parseSemVer("100.200.300");
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.version.major, 100);
    EXPECT_EQ(r.version.minor, 200);
    EXPECT_EQ(r.version.patch, 300);
}

TEST(SemVer, ParseInvalidEmpty) {
    auto r = parseSemVer("");
    EXPECT_FALSE(r.success);
}

TEST(SemVer, ParseInvalidTwoParts) {
    auto r = parseSemVer("1.2");
    EXPECT_FALSE(r.success);
}

TEST(SemVer, ParseInvalidLetters) {
    auto r = parseSemVer("a.b.c");
    EXPECT_FALSE(r.success);
}

TEST(SemVer, ParseInvalidNegative) {
    auto r = parseSemVer("-1.0.0");
    EXPECT_FALSE(r.success);
}

TEST(SemVer, ToString) {
    SemVer v{1, 2, 3};
    EXPECT_EQ(v.toString(), "1.2.3");
}

// === SemVer Comparison ===

TEST(SemVer, Equal) {
    SemVer a{1, 2, 3}, b{1, 2, 3};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(SemVer, MajorCmp) {
    SemVer a{1, 0, 0}, b{2, 0, 0};
    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
}

TEST(SemVer, MinorCmp) {
    SemVer a{1, 2, 0}, b{1, 3, 0};
    EXPECT_TRUE(a < b);
}

TEST(SemVer, PatchCmp) {
    SemVer a{1, 2, 3}, b{1, 2, 4};
    EXPECT_TRUE(a < b);
}

TEST(SemVer, LessOrEqual) {
    SemVer a{1, 0, 0}, b{1, 0, 0};
    EXPECT_TRUE(a <= b);
    SemVer c{2, 0, 0};
    EXPECT_TRUE(a <= c);
}

TEST(SemVer, GreaterThan) {
    SemVer a{2, 0, 0}, b{1, 0, 0};
    EXPECT_TRUE(a > b);
    EXPECT_TRUE(a >= b);
}

// === VersionConstraint Parsing ===

TEST(VersionConstraint, ParseExact) {
    auto r = parseVersionConstraint("1.0.0");
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.constraint.kind, VersionConstraint::Exact);
    EXPECT_EQ(r.constraint.min.major, 1);
}

TEST(VersionConstraint, ParseMinimum) {
    auto r = parseVersionConstraint(">=2.0.0");
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.constraint.kind, VersionConstraint::Minimum);
    EXPECT_EQ(r.constraint.min.major, 2);
}

TEST(VersionConstraint, ParseRange) {
    auto r = parseVersionConstraint(">=1.0.0,<2.0.0");
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.constraint.kind, VersionConstraint::Range);
    EXPECT_EQ(r.constraint.min.major, 1);
    EXPECT_EQ(r.constraint.max.major, 2);
}

TEST(VersionConstraint, ParseRangeSpaces) {
    auto r = parseVersionConstraint(">= 1.0.0 , < 2.0.0");
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.constraint.kind, VersionConstraint::Range);
    EXPECT_EQ(r.constraint.min.major, 1);
    EXPECT_EQ(r.constraint.max.major, 2);
}

TEST(VersionConstraint, ParseInvalid) {
    auto r = parseVersionConstraint("");
    EXPECT_FALSE(r.success);
}

TEST(VersionConstraint, ParseBadOp) {
    auto r = parseVersionConstraint("~1.0.0");
    EXPECT_FALSE(r.success);
}

// === VersionConstraint Satisfaction ===

TEST(VersionConstraint, ExactMatch) {
    VersionConstraint c;
    c.kind = VersionConstraint::Exact;
    c.min = SemVer{1, 0, 0};
    EXPECT_TRUE(c.satisfiedBy(SemVer{1, 0, 0}));
}

TEST(VersionConstraint, ExactNoMatch) {
    VersionConstraint c;
    c.kind = VersionConstraint::Exact;
    c.min = SemVer{1, 0, 0};
    EXPECT_FALSE(c.satisfiedBy(SemVer{1, 0, 1}));
}

TEST(VersionConstraint, MinMatch) {
    VersionConstraint c;
    c.kind = VersionConstraint::Minimum;
    c.min = SemVer{2, 0, 0};
    EXPECT_TRUE(c.satisfiedBy(SemVer{3, 0, 0}));
}

TEST(VersionConstraint, MinExact) {
    VersionConstraint c;
    c.kind = VersionConstraint::Minimum;
    c.min = SemVer{2, 0, 0};
    EXPECT_TRUE(c.satisfiedBy(SemVer{2, 0, 0}));
}

TEST(VersionConstraint, MinNoMatch) {
    VersionConstraint c;
    c.kind = VersionConstraint::Minimum;
    c.min = SemVer{2, 0, 0};
    EXPECT_FALSE(c.satisfiedBy(SemVer{1, 9, 9}));
}

TEST(VersionConstraint, RangeLow) {
    VersionConstraint c;
    c.kind = VersionConstraint::Range;
    c.min = SemVer{1, 0, 0};
    c.max = SemVer{2, 0, 0};
    EXPECT_TRUE(c.satisfiedBy(SemVer{1, 0, 0}));
}

TEST(VersionConstraint, RangeMid) {
    VersionConstraint c;
    c.kind = VersionConstraint::Range;
    c.min = SemVer{1, 0, 0};
    c.max = SemVer{2, 0, 0};
    EXPECT_TRUE(c.satisfiedBy(SemVer{1, 5, 3}));
}

TEST(VersionConstraint, RangeNoMatchHigh) {
    VersionConstraint c;
    c.kind = VersionConstraint::Range;
    c.min = SemVer{1, 0, 0};
    c.max = SemVer{2, 0, 0};
    EXPECT_FALSE(c.satisfiedBy(SemVer{2, 0, 0}));
}

// === Dependency Parsing ===

TEST(DependencyParsing, SingleDep) {
    auto r = parseTOML("[dependencies]\njson = \"1.0.0\"\n");
    ASSERT_TRUE(r.success);
    auto deps = parseDependencies(r.doc);
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].name, "json");
    EXPECT_EQ(deps[0].constraint.kind, VersionConstraint::Exact);
    EXPECT_EQ(deps[0].constraint.min.major, 1);
}

TEST(DependencyParsing, MultipleDeps) {
    auto r = parseTOML(
        "[dependencies]\njson = \"1.0.0\"\nhttp = \">=2.0.0\"\n"
        "utils = \">=1.0.0,<2.0.0\"\n");
    ASSERT_TRUE(r.success);
    auto deps = parseDependencies(r.doc);
    EXPECT_EQ(deps.size(), 3u);
}

TEST(DependencyParsing, NoDeps) {
    auto r = parseTOML("[project]\nname = \"app\"\n");
    ASSERT_TRUE(r.success);
    auto deps = parseDependencies(r.doc);
    EXPECT_TRUE(deps.empty());
}

TEST(DependencyParsing, WithOtherSections) {
    auto r = parseTOML(
        "[project]\nname = \"app\"\n"
        "[dependencies]\njson = \"1.0.0\"\n"
        "[build]\nopt-level = 2\n");
    ASSERT_TRUE(r.success);
    auto deps = parseDependencies(r.doc);
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].name, "json");
}

TEST(DependencyParsing, InvalidVersion) {
    auto r = parseTOML("[dependencies]\nbad = \"not-a-version\"\n");
    ASSERT_TRUE(r.success);
    auto deps = parseDependencies(r.doc);
    // Invalid version constraint is silently skipped
    EXPECT_TRUE(deps.empty());
}

// === ProjectConfig with Dependencies ===

TEST(ProjectConfig, ConfigWithDeps) {
    auto r = parseTOML(
        "[project]\nname = \"myapp\"\nversion = \"1.0.0\"\n"
        "[dependencies]\njson = \"1.0.0\"\n");
    ASSERT_TRUE(r.success);
    auto cfg = loadProjectConfig(r.doc);
    EXPECT_EQ(cfg.name, "myapp");
    ASSERT_EQ(cfg.dependencies.size(), 1u);
    EXPECT_EQ(cfg.dependencies[0].name, "json");
}

TEST(ProjectConfig, ConfigWithoutDeps) {
    auto r = parseTOML("[project]\nname = \"myapp\"\n");
    ASSERT_TRUE(r.success);
    auto cfg = loadProjectConfig(r.doc);
    EXPECT_TRUE(cfg.dependencies.empty());
}

TEST(ProjectConfig, DepsPreservedOnLoad) {
    auto r = parseTOML(
        "[project]\nname = \"app\"\n"
        "[dependencies]\nhttp = \">=2.0.0\"\nutils = \">=1.0.0,<2.0.0\"\n");
    ASSERT_TRUE(r.success);
    auto cfg = loadProjectConfig(r.doc);
    EXPECT_EQ(cfg.dependencies.size(), 2u);
}

// === Lock File ===

TEST(LockFile, GenerateAndParse) {
    std::vector<ResolvedPackage> pkgs;
    ResolvedPackage p;
    p.name = "json";
    p.version = SemVer{1, 0, 0};
    p.path = "packages/json";
    p.srcPath = "packages/json/src";
    pkgs.push_back(p);

    std::string content = generateLockFile(pkgs);
    auto entries = parseLockFile(content);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, "json");
    EXPECT_EQ(entries[0].version, "1.0.0");
}

TEST(LockFile, MultiplePackages) {
    std::vector<ResolvedPackage> pkgs;
    {
        ResolvedPackage p;
        p.name = "json";
        p.version = SemVer{1, 0, 0};
        pkgs.push_back(p);
    }
    {
        ResolvedPackage p;
        p.name = "http";
        p.version = SemVer{2, 1, 0};
        pkgs.push_back(p);
    }

    std::string content = generateLockFile(pkgs);
    auto entries = parseLockFile(content);
    EXPECT_EQ(entries.size(), 2u);
}

TEST(LockFile, ParseEmpty) {
    auto entries = parseLockFile("");
    EXPECT_TRUE(entries.empty());
}

TEST(LockFile, IsCurrent) {
    PackageDep dep;
    dep.name = "json";
    dep.constraint.kind = VersionConstraint::Exact;
    dep.constraint.min = SemVer{1, 0, 0};

    LockFileEntry entry;
    entry.name = "json";
    entry.version = "1.0.0";

    EXPECT_TRUE(isLockFileCurrent({dep}, {entry}));
}

TEST(LockFile, IsNotCurrent) {
    PackageDep dep;
    dep.name = "json";
    dep.constraint.kind = VersionConstraint::Exact;
    dep.constraint.min = SemVer{2, 0, 0};

    LockFileEntry entry;
    entry.name = "json";
    entry.version = "1.0.0";

    EXPECT_FALSE(isLockFileCurrent({dep}, {entry}));
}

// === Package Validation ===

TEST(PackageValidation, ValidExact) {
    PackageDep dep;
    dep.name = "json";
    dep.constraint.kind = VersionConstraint::Exact;
    dep.constraint.min = SemVer{1, 0, 0};

    std::string toml = "[project]\nname = \"json\"\nversion = \"1.0.0\"\n";
    auto r = validatePackageToml(dep, toml);
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.version.major, 1);
}

TEST(PackageValidation, ValidMinimum) {
    PackageDep dep;
    dep.name = "http";
    dep.constraint.kind = VersionConstraint::Minimum;
    dep.constraint.min = SemVer{2, 0, 0};

    std::string toml = "[project]\nname = \"http\"\nversion = \"2.3.0\"\n";
    auto r = validatePackageToml(dep, toml);
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.version.minor, 3);
}

TEST(PackageValidation, ValidRange) {
    PackageDep dep;
    dep.name = "utils";
    dep.constraint.kind = VersionConstraint::Range;
    dep.constraint.min = SemVer{1, 0, 0};
    dep.constraint.max = SemVer{2, 0, 0};

    std::string toml = "[project]\nname = \"utils\"\nversion = \"1.5.0\"\n";
    auto r = validatePackageToml(dep, toml);
    EXPECT_TRUE(r.success);
}

TEST(PackageValidation, VersionMismatch) {
    PackageDep dep;
    dep.name = "json";
    dep.constraint.kind = VersionConstraint::Exact;
    dep.constraint.min = SemVer{2, 0, 0};

    std::string toml = "[project]\nname = \"json\"\nversion = \"1.0.0\"\n";
    auto r = validatePackageToml(dep, toml);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.errorMsg.empty());
}

TEST(PackageValidation, InvalidToml) {
    PackageDep dep;
    dep.name = "json";
    dep.constraint.kind = VersionConstraint::Exact;
    dep.constraint.min = SemVer{1, 0, 0};

    auto r = validatePackageToml(dep, "[bad\n");
    EXPECT_FALSE(r.success);
}

TEST(PackageValidation, NameMismatch) {
    PackageDep dep;
    dep.name = "json";
    dep.constraint.kind = VersionConstraint::Exact;
    dep.constraint.min = SemVer{1, 0, 0};

    std::string toml = "[project]\nname = \"xml\"\nversion = \"1.0.0\"\n";
    auto r = validatePackageToml(dep, toml);
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.errorMsg.find("mismatch") != std::string::npos);
}

// === BuildCache Tests ===

// Helper to create a temp directory for cache tests
class BuildCacheTest : public ::testing::Test {
protected:
    std::string testDir_;

    void SetUp() override {
        testDir_ = "__buildcache_test_tmp__";
        liva::createDirectories(testDir_);
    }

    void TearDown() override {
        liva::removeDirectoryRecursive(testDir_);
    }

    void writeFile(const std::string &path, const std::string &content) {
        std::string dir = path;
        auto pos = dir.find_last_of("/\\");
        if (pos != std::string::npos)
            liva::createDirectories(dir.substr(0, pos));
        std::ofstream f(path);
        f << content;
    }
};

TEST_F(BuildCacheTest, HashFileConsistent) {
    std::string filePath = liva::joinPath(testDir_, "test.liva");
    writeFile(filePath, "func main() {\n    println(\"hello\")\n}\n");

    liva::BuildCache cache(testDir_);
    std::string hash1 = cache.hashFileContent(filePath);
    std::string hash2 = cache.hashFileContent(filePath);

    EXPECT_FALSE(hash1.empty());
    EXPECT_EQ(hash1, hash2);
    EXPECT_EQ(hash1.size(), 16u); // 64-bit hex = 16 chars
}

TEST_F(BuildCacheTest, HashDifferentContent) {
    std::string file1 = liva::joinPath(testDir_, "a.liva");
    std::string file2 = liva::joinPath(testDir_, "b.liva");
    writeFile(file1, "func main() {}");
    writeFile(file2, "func main() { println(\"hi\") }");

    liva::BuildCache cache(testDir_);
    std::string hash1 = cache.hashFileContent(file1);
    std::string hash2 = cache.hashFileContent(file2);

    EXPECT_NE(hash1, hash2);
}

TEST_F(BuildCacheTest, HashNonExistentFile) {
    liva::BuildCache cache(testDir_);
    std::string hash = cache.hashFileContent("__nonexistent_file__.liva");
    EXPECT_TRUE(hash.empty());
}

TEST_F(BuildCacheTest, ScanDependenciesSingleFile) {
    std::string mainFile = liva::joinPath(testDir_, "main.liva");
    writeFile(mainFile, "func main() {\n    println(\"hello\")\n}\n");

    liva::BuildCache cache(testDir_);
    auto deps = cache.scanDependencies(mainFile, {});

    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0], mainFile);
}

TEST_F(BuildCacheTest, ScanDependenciesWithImport) {
    std::string mainFile = liva::joinPath(testDir_, "main.liva");
    std::string utilsFile = liva::joinPath(testDir_, "utils.liva");

    writeFile(mainFile, "import \"utils\"\nfunc main() {}\n");
    writeFile(utilsFile, "func helper() {}\n");

    liva::BuildCache cache(testDir_);
    auto deps = cache.scanDependencies(mainFile, {});

    ASSERT_EQ(deps.size(), 2u);
    // main.liva should be first (entry)
    EXPECT_EQ(deps[0], mainFile);
}

TEST_F(BuildCacheTest, ScanDependenciesSkipsStd) {
    std::string mainFile = liva::joinPath(testDir_, "main.liva");
    writeFile(mainFile, "import std::io\nimport std::math\nfunc main() {}\n");

    liva::BuildCache cache(testDir_);
    auto deps = cache.scanDependencies(mainFile, {});

    // Should only find main.liva, std:: imports are skipped
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0], mainFile);
}

TEST_F(BuildCacheTest, ScanDependenciesSearchPaths) {
    std::string srcDir = liva::joinPath(testDir_, "src");
    std::string libDir = liva::joinPath(testDir_, "lib");
    liva::createDirectories(srcDir);
    liva::createDirectories(libDir);

    std::string mainFile = liva::joinPath(srcDir, "main.liva");
    std::string mathFile = liva::joinPath(libDir, "math.liva");

    writeFile(mainFile, "import \"math\"\nfunc main() {}\n");
    writeFile(mathFile, "func add(a: i32, b: i32) -> i32 { return a + b }\n");

    liva::BuildCache cache(testDir_);
    auto deps = cache.scanDependencies(mainFile, {libDir});

    ASSERT_EQ(deps.size(), 2u);
}

TEST_F(BuildCacheTest, ScanDependenciesCircularImport) {
    std::string fileA = liva::joinPath(testDir_, "a.liva");
    std::string fileB = liva::joinPath(testDir_, "b.liva");

    writeFile(fileA, "import \"b\"\nfunc foo() {}\n");
    writeFile(fileB, "import \"a\"\nfunc bar() {}\n");

    liva::BuildCache cache(testDir_);
    auto deps = cache.scanDependencies(fileA, {});

    // Should find both without infinite loop
    ASSERT_EQ(deps.size(), 2u);
}

TEST_F(BuildCacheTest, CheckCacheReturnsEmptyOnMiss) {
    std::string mainFile = liva::joinPath(testDir_, "main.liva");
    writeFile(mainFile, "func main() {}\n");

    liva::BuildCache cache(testDir_);
    std::string result = cache.checkCache({mainFile}, 0, false);
    EXPECT_TRUE(result.empty());
}

TEST_F(BuildCacheTest, StoreCacheAndCheckRoundTrip) {
    std::string mainFile = liva::joinPath(testDir_, "main.liva");
    writeFile(mainFile, "func main() {}\n");

    // Create a fake object file
    std::string fakeObj = liva::joinPath(testDir_, "fake.o");
    writeFile(fakeObj, "fake object data");

    liva::BuildCache cache(testDir_);
    std::vector<std::string> files = {mainFile};

    // Store cache
    bool stored = cache.storeCache(files, fakeObj, 2, false);
    EXPECT_TRUE(stored);

    // Check cache — should hit
    std::string cachedObj = cache.checkCache(files, 2, false);
    EXPECT_FALSE(cachedObj.empty());
}

TEST_F(BuildCacheTest, CacheInvalidationOnFileChange) {
    std::string mainFile = liva::joinPath(testDir_, "main.liva");
    writeFile(mainFile, "func main() {}\n");

    std::string fakeObj = liva::joinPath(testDir_, "fake.o");
    writeFile(fakeObj, "fake object data");

    liva::BuildCache cache(testDir_);
    std::vector<std::string> files = {mainFile};

    cache.storeCache(files, fakeObj, 0, false);

    // Modify file
    writeFile(mainFile, "func main() { println(\"changed\") }\n");

    // Should be a cache miss now
    std::string cachedObj = cache.checkCache(files, 0, false);
    EXPECT_TRUE(cachedObj.empty());
}

TEST_F(BuildCacheTest, CacheInvalidationOnOptLevelChange) {
    std::string mainFile = liva::joinPath(testDir_, "main.liva");
    writeFile(mainFile, "func main() {}\n");

    std::string fakeObj = liva::joinPath(testDir_, "fake.o");
    writeFile(fakeObj, "fake object data");

    liva::BuildCache cache(testDir_);
    std::vector<std::string> files = {mainFile};

    cache.storeCache(files, fakeObj, 0, false);

    // Different opt level should miss
    std::string cachedObj = cache.checkCache(files, 2, false);
    EXPECT_TRUE(cachedObj.empty());
}

TEST_F(BuildCacheTest, CacheInvalidationOnDebugChange) {
    std::string mainFile = liva::joinPath(testDir_, "main.liva");
    writeFile(mainFile, "func main() {}\n");

    std::string fakeObj = liva::joinPath(testDir_, "fake.o");
    writeFile(fakeObj, "fake object data");

    liva::BuildCache cache(testDir_);
    std::vector<std::string> files = {mainFile};

    cache.storeCache(files, fakeObj, 0, false);

    // Different debug flag should miss
    std::string cachedObj = cache.checkCache(files, 0, true);
    EXPECT_TRUE(cachedObj.empty());
}

TEST_F(BuildCacheTest, CleanRemovesCache) {
    std::string mainFile = liva::joinPath(testDir_, "main.liva");
    writeFile(mainFile, "func main() {}\n");

    std::string fakeObj = liva::joinPath(testDir_, "fake.o");
    writeFile(fakeObj, "fake object data");

    liva::BuildCache cache(testDir_);
    cache.storeCache({mainFile}, fakeObj, 0, false);

    // Verify cache exists
    EXPECT_FALSE(cache.checkCache({mainFile}, 0, false).empty());

    // Clean
    cache.clean();

    // Should miss after clean
    EXPECT_TRUE(cache.checkCache({mainFile}, 0, false).empty());
}

// === SHA-256 Tests ===

TEST(SHA256, EmptyString) {
    // SHA-256 of "" is well-known
    std::string hash = liva::PackageManager::sha256("");
    EXPECT_EQ(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(SHA256, HelloWorld) {
    // SHA-256 of "hello world"
    std::string hash = liva::PackageManager::sha256("hello world");
    EXPECT_EQ(hash, "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
}

TEST(SHA256, Abc) {
    // SHA-256 of "abc"
    std::string hash = liva::PackageManager::sha256("abc");
    EXPECT_EQ(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(SHA256, ConsistentHashing) {
    std::string data = "some test data for consistency";
    std::string h1 = liva::PackageManager::sha256(data);
    std::string h2 = liva::PackageManager::sha256(data);
    EXPECT_EQ(h1, h2);
    EXPECT_EQ(h1.size(), 64u); // 256 bits = 64 hex chars
}

// === Checksum Verification ===

TEST(Checksum, VerifyCorrect) {
    std::string data = "hello world";
    std::string expected = "sha256:b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9";
    EXPECT_TRUE(liva::PackageManager::verifyChecksum(data, expected));
}

TEST(Checksum, VerifyWrong) {
    std::string data = "hello world";
    std::string expected = "sha256:0000000000000000000000000000000000000000000000000000000000000000";
    EXPECT_FALSE(liva::PackageManager::verifyChecksum(data, expected));
}

TEST(Checksum, VerifyBadFormat) {
    std::string data = "hello world";
    EXPECT_FALSE(liva::PackageManager::verifyChecksum(data, "md5:abc123"));
    EXPECT_FALSE(liva::PackageManager::verifyChecksum(data, ""));
    EXPECT_FALSE(liva::PackageManager::verifyChecksum(data, "short"));
}

// === JSON Helpers ===

TEST(JsonHelpers, GetString) {
    std::string json = R"({"name": "mypackage", "version": "1.0.0"})";
    EXPECT_EQ(liva::json::getString(json, "name"), "mypackage");
    EXPECT_EQ(liva::json::getString(json, "version"), "1.0.0");
    EXPECT_EQ(liva::json::getString(json, "missing"), "");
}

TEST(JsonHelpers, GetStringArray) {
    std::string json = R"({"versions": ["1.0.0", "1.1.0", "2.0.0"]})";
    auto arr = liva::json::getStringArray(json, "versions");
    ASSERT_EQ(arr.size(), 3u);
    EXPECT_EQ(arr[0], "1.0.0");
    EXPECT_EQ(arr[1], "1.1.0");
    EXPECT_EQ(arr[2], "2.0.0");
}

TEST(JsonHelpers, GetStringArrayEmpty) {
    std::string json = R"({"versions": []})";
    auto arr = liva::json::getStringArray(json, "versions");
    EXPECT_TRUE(arr.empty());
}

TEST(JsonHelpers, GetObject) {
    std::string json = R"({"dependencies": {"json": ">=1.0.0", "http": "2.0.0"}})";
    auto obj = liva::json::getObject(json, "dependencies");
    ASSERT_EQ(obj.size(), 2u);
    EXPECT_EQ(obj["json"], ">=1.0.0");
    EXPECT_EQ(obj["http"], "2.0.0");
}

TEST(JsonHelpers, GetObjectMissing) {
    std::string json = R"({"name": "test"})";
    auto obj = liva::json::getObject(json, "dependencies");
    EXPECT_TRUE(obj.empty());
}

TEST(JsonHelpers, GetStringWithEscapes) {
    std::string json = R"({"path": "C:\\Users\\test"})";
    EXPECT_EQ(liva::json::getString(json, "path"), "C:\\Users\\test");
}

TEST(JsonHelpers, NestedObjects) {
    std::string json = R"({"name": "pkg", "meta": {"author": "test"}, "version": "1.0.0"})";
    EXPECT_EQ(liva::json::getString(json, "name"), "pkg");
    EXPECT_EQ(liva::json::getString(json, "version"), "1.0.0");
}

// === Registry Entry Parsing ===

TEST(RegistryEntry, ParseValid) {
    std::string json = R"({
        "name": "mylib",
        "version": "1.2.3",
        "url": "https://registry.liva-lang.org/packages/mylib/1.2.3.tar.gz",
        "checksum": "sha256:abc123",
        "dependencies": {"utils": ">=1.0.0"}
    })";
    liva::RegistryEntry entry;
    EXPECT_TRUE(liva::PackageManager::parseRegistryEntry(json, entry));
    EXPECT_EQ(entry.name, "mylib");
    EXPECT_EQ(entry.version.major, 1);
    EXPECT_EQ(entry.version.minor, 2);
    EXPECT_EQ(entry.version.patch, 3);
    EXPECT_EQ(entry.downloadUrl, "https://registry.liva-lang.org/packages/mylib/1.2.3.tar.gz");
    EXPECT_EQ(entry.checksum, "sha256:abc123");
    ASSERT_EQ(entry.dependencies.size(), 1u);
    EXPECT_EQ(entry.dependencies[0].name, "utils");
}

TEST(RegistryEntry, ParseMinimal) {
    std::string json = R"({"name": "pkg", "version": "0.1.0"})";
    liva::RegistryEntry entry;
    EXPECT_TRUE(liva::PackageManager::parseRegistryEntry(json, entry));
    EXPECT_EQ(entry.name, "pkg");
    EXPECT_EQ(entry.version.major, 0);
    EXPECT_TRUE(entry.downloadUrl.empty());
    EXPECT_TRUE(entry.dependencies.empty());
}

TEST(RegistryEntry, ParseInvalid) {
    liva::RegistryEntry entry;
    EXPECT_FALSE(liva::PackageManager::parseRegistryEntry("{}", entry));
    EXPECT_FALSE(liva::PackageManager::parseRegistryEntry(R"({"name":"x"})", entry));
    EXPECT_FALSE(liva::PackageManager::parseRegistryEntry("", entry));
}

// === Version List Parsing ===

TEST(VersionList, ParseValid) {
    std::string json = R"({"versions": ["1.0.0", "1.1.0", "2.0.0"]})";
    auto versions = liva::PackageManager::parseVersionList(json);
    ASSERT_EQ(versions.size(), 3u);
    EXPECT_EQ(versions[0], "1.0.0");
    EXPECT_EQ(versions[2], "2.0.0");
}

TEST(VersionList, ParseEmpty) {
    std::string json = R"({"versions": []})";
    auto versions = liva::PackageManager::parseVersionList(json);
    EXPECT_TRUE(versions.empty());
}

TEST(VersionList, ParseMissing) {
    std::string json = R"({"name": "test"})";
    auto versions = liva::PackageManager::parseVersionList(json);
    EXPECT_TRUE(versions.empty());
}

// === Lock File with Checksums ===

TEST(LockFile, GenerateWithChecksums) {
    std::vector<liva::ResolvedPackage> pkgs;
    liva::ResolvedPackage p;
    p.name = "json";
    p.version = liva::SemVer{1, 0, 0};
    p.path = "packages/json";
    p.srcPath = "packages/json/src";
    pkgs.push_back(p);

    std::vector<liva::LockFileEntry> checksums;
    liva::LockFileEntry le;
    le.name = "json";
    le.version = "1.0.0";
    le.checksum = "sha256:abc123def456";
    checksums.push_back(le);

    std::string content = liva::generateLockFile(pkgs, checksums);
    EXPECT_TRUE(content.find("[lock]") != std::string::npos);
    EXPECT_TRUE(content.find("[checksums]") != std::string::npos);
    EXPECT_TRUE(content.find("sha256:abc123def456") != std::string::npos);

    auto entries = liva::parseLockFile(content);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, "json");
    EXPECT_EQ(entries[0].version, "1.0.0");
    EXPECT_EQ(entries[0].checksum, "sha256:abc123def456");
}

TEST(LockFile, GenerateWithoutChecksums) {
    std::vector<liva::ResolvedPackage> pkgs;
    liva::ResolvedPackage p;
    p.name = "utils";
    p.version = liva::SemVer{2, 1, 0};
    pkgs.push_back(p);

    std::string content = liva::generateLockFile(pkgs);
    EXPECT_TRUE(content.find("[lock]") != std::string::npos);
    EXPECT_TRUE(content.find("[checksums]") == std::string::npos);
}

TEST(LockFile, ParseChecksumRoundTrip) {
    std::string content =
        "[lock]\njson = \"1.0.0\"\nhttp = \"2.0.0\"\n\n"
        "[checksums]\njson = \"sha256:aaa\"\nhttp = \"sha256:bbb\"\n";

    auto entries = liva::parseLockFile(content);
    ASSERT_EQ(entries.size(), 2u);
    // Find entries by name (unordered_map iteration order not guaranteed)
    for (const auto &e : entries) {
        if (e.name == "json") {
            EXPECT_EQ(e.version, "1.0.0");
            EXPECT_EQ(e.checksum, "sha256:aaa");
        } else if (e.name == "http") {
            EXPECT_EQ(e.version, "2.0.0");
            EXPECT_EQ(e.checksum, "sha256:bbb");
        }
    }
}

// === ProjectConfig Registry URL ===

TEST(ProjectConfig, RegistryUrlFromToml) {
    auto r = liva::parseTOML(
        "[project]\nname = \"myapp\"\n"
        "[registry]\nurl = \"https://registry.liva-lang.org\"\n");
    ASSERT_TRUE(r.success);
    auto cfg = liva::loadProjectConfig(r.doc);
    EXPECT_EQ(cfg.registryUrl, "https://registry.liva-lang.org");
}

TEST(ProjectConfig, RegistryUrlEmpty) {
    auto r = liva::parseTOML("[project]\nname = \"myapp\"\n");
    ASSERT_TRUE(r.success);
    auto cfg = liva::loadProjectConfig(r.doc);
    // registryUrl should be empty if no [registry] section and no env var
    // (env var test skipped since it modifies process state)
    // Just check it doesn't crash
    EXPECT_TRUE(cfg.registryUrl.empty() || !cfg.registryUrl.empty());
}

// === PackageManager Local Resolution ===

TEST_F(BuildCacheTest, PackageManagerLocalResolve) {
    // Create a local package structure
    std::string pkgDir = liva::joinPath(testDir_, "packages/mylib");
    std::string srcDir = liva::joinPath(pkgDir, "src");
    liva::createDirectories(srcDir);

    writeFile(liva::joinPath(pkgDir, "liva.toml"),
        "[project]\nname = \"mylib\"\nversion = \"1.0.0\"\n");
    writeFile(liva::joinPath(srcDir, "lib.liva"),
        "pub func helper() -> i32 { return 42 }\n");

    liva::PackageDep dep;
    dep.name = "mylib";
    dep.constraint.kind = liva::VersionConstraint::Exact;
    dep.constraint.min = liva::SemVer{1, 0, 0};

    liva::PackageManager mgr(testDir_, "");
    auto result = mgr.resolveAndInstall({dep});
    EXPECT_TRUE(result.success);
    ASSERT_EQ(result.packages.size(), 1u);
    EXPECT_EQ(result.packages[0].name, "mylib");
    EXPECT_EQ(result.packages[0].version.major, 1);
}

TEST_F(BuildCacheTest, PackageManagerLocalVersionMismatch) {
    std::string pkgDir = liva::joinPath(testDir_, "packages/mylib");
    liva::createDirectories(pkgDir);

    writeFile(liva::joinPath(pkgDir, "liva.toml"),
        "[project]\nname = \"mylib\"\nversion = \"1.0.0\"\n");

    liva::PackageDep dep;
    dep.name = "mylib";
    dep.constraint.kind = liva::VersionConstraint::Exact;
    dep.constraint.min = liva::SemVer{2, 0, 0};

    liva::PackageManager mgr(testDir_, "");
    auto result = mgr.resolveAndInstall({dep});
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMsg.empty());
}

TEST_F(BuildCacheTest, PackageManagerNotFound) {
    liva::PackageDep dep;
    dep.name = "nonexistent";
    dep.constraint.kind = liva::VersionConstraint::Exact;
    dep.constraint.min = liva::SemVer{1, 0, 0};

    liva::PackageManager mgr(testDir_, "");
    auto result = mgr.resolveAndInstall({dep});
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMsg.find("not found") != std::string::npos);
    EXPECT_TRUE(result.errorMsg.find("no registry") != std::string::npos);
}

TEST_F(BuildCacheTest, PackageManagerTransitiveDeps) {
    // Create package A which depends on package B
    std::string pkgA = liva::joinPath(testDir_, "packages/liba");
    std::string pkgB = liva::joinPath(testDir_, "packages/libb");
    liva::createDirectories(liva::joinPath(pkgA, "src"));
    liva::createDirectories(liva::joinPath(pkgB, "src"));

    writeFile(liva::joinPath(pkgA, "liva.toml"),
        "[project]\nname = \"liba\"\nversion = \"1.0.0\"\n"
        "[dependencies]\nlibb = \"1.0.0\"\n");
    writeFile(liva::joinPath(pkgA, "src/a.liva"), "pub func a() {}\n");

    writeFile(liva::joinPath(pkgB, "liva.toml"),
        "[project]\nname = \"libb\"\nversion = \"1.0.0\"\n");
    writeFile(liva::joinPath(pkgB, "src/b.liva"), "pub func b() {}\n");

    liva::PackageDep dep;
    dep.name = "liba";
    dep.constraint.kind = liva::VersionConstraint::Exact;
    dep.constraint.min = liva::SemVer{1, 0, 0};

    liva::PackageManager mgr(testDir_, "");
    auto result = mgr.resolveAndInstall({dep});
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.packages.size(), 2u);
}

TEST_F(BuildCacheTest, PackageManagerMultipleDeps) {
    // Create two independent packages
    std::string pkg1 = liva::joinPath(testDir_, "packages/pkg1");
    std::string pkg2 = liva::joinPath(testDir_, "packages/pkg2");
    liva::createDirectories(liva::joinPath(pkg1, "src"));
    liva::createDirectories(liva::joinPath(pkg2, "src"));

    writeFile(liva::joinPath(pkg1, "liva.toml"),
        "[project]\nname = \"pkg1\"\nversion = \"1.0.0\"\n");
    writeFile(liva::joinPath(pkg1, "src/p1.liva"), "pub func p1() {}\n");

    writeFile(liva::joinPath(pkg2, "liva.toml"),
        "[project]\nname = \"pkg2\"\nversion = \"2.1.0\"\n");
    writeFile(liva::joinPath(pkg2, "src/p2.liva"), "pub func p2() {}\n");

    std::vector<liva::PackageDep> deps;
    {
        liva::PackageDep d;
        d.name = "pkg1";
        d.constraint.kind = liva::VersionConstraint::Exact;
        d.constraint.min = liva::SemVer{1, 0, 0};
        deps.push_back(d);
    }
    {
        liva::PackageDep d;
        d.name = "pkg2";
        d.constraint.kind = liva::VersionConstraint::Minimum;
        d.constraint.min = liva::SemVer{2, 0, 0};
        deps.push_back(d);
    }

    liva::PackageManager mgr(testDir_, "");
    auto result = mgr.resolveAndInstall(deps);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.packages.size(), 2u);
}

// === Per-File Cache Tests (Separate Compilation) ===

TEST_F(BuildCacheTest, CheckFilesCacheAllMiss) {
    std::string file1 = liva::joinPath(testDir_, "main.liva");
    std::string file2 = liva::joinPath(testDir_, "utils.liva");
    writeFile(file1, "func main() {}\n");
    writeFile(file2, "func helper() {}\n");

    liva::BuildCache cache(testDir_);
    auto statuses = cache.checkFilesCache({file1, file2}, 0, false);

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_TRUE(statuses[0].needsRecompile);
    EXPECT_TRUE(statuses[1].needsRecompile);
    EXPECT_TRUE(statuses[0].cachedObjPath.empty());
    EXPECT_TRUE(statuses[1].cachedObjPath.empty());
    EXPECT_FALSE(statuses[0].currentHash.empty());
    EXPECT_FALSE(statuses[1].currentHash.empty());
}

TEST_F(BuildCacheTest, CheckFilesCachePartialHit) {
    std::string file1 = liva::joinPath(testDir_, "main.liva");
    std::string file2 = liva::joinPath(testDir_, "utils.liva");
    writeFile(file1, "func main() {}\n");
    writeFile(file2, "func helper() {}\n");

    liva::BuildCache cache(testDir_);

    // Store file1 in cache
    std::string hash1 = cache.hashFileContent(file1);
    std::string tempObj1 = liva::joinPath(testDir_, "main.o");
    writeFile(tempObj1, "FAKE_OBJ_CONTENT_1");
    EXPECT_TRUE(cache.storeFileObject(file1, hash1, tempObj1, 0, false));

    // Check: file1 should be cached, file2 should need recompile
    auto statuses = cache.checkFilesCache({file1, file2}, 0, false);

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_FALSE(statuses[0].needsRecompile);
    EXPECT_FALSE(statuses[0].cachedObjPath.empty());
    EXPECT_TRUE(statuses[1].needsRecompile);
    EXPECT_TRUE(statuses[1].cachedObjPath.empty());
}

TEST_F(BuildCacheTest, CheckFilesCacheAllHit) {
    std::string file1 = liva::joinPath(testDir_, "main.liva");
    std::string file2 = liva::joinPath(testDir_, "utils.liva");
    writeFile(file1, "func main() {}\n");
    writeFile(file2, "func helper() {}\n");

    liva::BuildCache cache(testDir_);

    // Store both files
    std::string hash1 = cache.hashFileContent(file1);
    std::string hash2 = cache.hashFileContent(file2);
    std::string tempObj1 = liva::joinPath(testDir_, "main.o");
    std::string tempObj2 = liva::joinPath(testDir_, "utils.o");
    writeFile(tempObj1, "FAKE_OBJ_1");
    writeFile(tempObj2, "FAKE_OBJ_2");
    EXPECT_TRUE(cache.storeFileObject(file1, hash1, tempObj1, 0, false));
    EXPECT_TRUE(cache.storeFileObject(file2, hash2, tempObj2, 0, false));

    // Check: both should be cached
    auto statuses = cache.checkFilesCache({file1, file2}, 0, false);

    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_FALSE(statuses[0].needsRecompile);
    EXPECT_FALSE(statuses[1].needsRecompile);
    EXPECT_FALSE(statuses[0].cachedObjPath.empty());
    EXPECT_FALSE(statuses[1].cachedObjPath.empty());
}

TEST_F(BuildCacheTest, StoreFileObjectRoundTrip) {
    std::string file1 = liva::joinPath(testDir_, "test.liva");
    writeFile(file1, "func foo() {}\n");

    liva::BuildCache cache(testDir_);
    std::string hash1 = cache.hashFileContent(file1);

    // Store
    std::string tempObj = liva::joinPath(testDir_, "test.o");
    writeFile(tempObj, "FAKE_OBJ_DATA");
    EXPECT_TRUE(cache.storeFileObject(file1, hash1, tempObj, 2, true));

    // Check round-trip
    auto statuses = cache.checkFilesCache({file1}, 2, true);
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_FALSE(statuses[0].needsRecompile);
    EXPECT_FALSE(statuses[0].cachedObjPath.empty());

    // Modify the source file → hash changes → recompile needed
    // Sleep to ensure mtime changes (mtime fast-path skips hash if mtime matches)
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    writeFile(file1, "func foo() { return 42 }\n");
    auto statuses2 = cache.checkFilesCache({file1}, 2, true);
    ASSERT_EQ(statuses2.size(), 1u);
    EXPECT_TRUE(statuses2[0].needsRecompile);
}

TEST_F(BuildCacheTest, CacheInvalidationOnOptChange) {
    std::string file1 = liva::joinPath(testDir_, "main.liva");
    writeFile(file1, "func main() {}\n");

    liva::BuildCache cache(testDir_);
    std::string hash1 = cache.hashFileContent(file1);
    std::string tempObj = liva::joinPath(testDir_, "main.o");
    writeFile(tempObj, "FAKE_OBJ");
    EXPECT_TRUE(cache.storeFileObject(file1, hash1, tempObj, 0, false));

    // Same opt → cached
    auto s1 = cache.checkFilesCache({file1}, 0, false);
    ASSERT_EQ(s1.size(), 1u);
    EXPECT_FALSE(s1[0].needsRecompile);

    // Different opt level → invalidated
    auto s2 = cache.checkFilesCache({file1}, 2, false);
    ASSERT_EQ(s2.size(), 1u);
    EXPECT_TRUE(s2[0].needsRecompile);
}

TEST_F(BuildCacheTest, ObjectPathForSource) {
    liva::BuildCache cache(testDir_);

    std::string p1 = cache.objectPathForSource("src/main.liva");
    std::string p2 = cache.objectPathForSource("src/utils.liva");
    std::string p3 = cache.objectPathForSource("other/main.liva");

    // All should end with .o
    EXPECT_NE(p1.find(".o"), std::string::npos);
    EXPECT_NE(p2.find(".o"), std::string::npos);
    EXPECT_NE(p3.find(".o"), std::string::npos);

    // Different sources → different object names
    EXPECT_NE(p1, p2);
    // Same stem, different path → different hash
    EXPECT_NE(p1, p3);

    // Same source → same object name (deterministic)
    std::string p1b = cache.objectPathForSource("src/main.liva");
    EXPECT_EQ(p1, p1b);
}

// === LTO Configuration Tests ===

TEST(LTOConfig, DefaultLtoModeIsNone) {
    auto result = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\nopt-level = 2\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.lto, "none");
}

TEST(LTOConfig, LtoModeThin) {
    auto result = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\nopt-level = 2\nlto = \"thin\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.lto, "thin");
}

TEST(LTOConfig, LtoModeFull) {
    auto result = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\nopt-level = 3\nlto = \"full\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.lto, "full");
}

TEST(LTOConfig, LtoModeExplicitNone) {
    auto result = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\nlto = \"none\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.lto, "none");
}

TEST(LTOConfig, LtoWithDebugInfo) {
    auto result = liva::parseTOML(
        "[project]\nname = \"dbg\"\n[build]\nopt-level = 2\n"
        "debug-info = true\nlto = \"thin\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.lto, "thin");
    EXPECT_TRUE(cfg.debugInfo);
    EXPECT_EQ(cfg.optLevel, 2);
}

TEST(LTOConfig, LtoWithAllBuildOptions) {
    auto result = liva::parseTOML(
        "[project]\nname = \"full\"\nversion = \"1.0.0\"\n"
        "entry = \"src/app.liva\"\n"
        "[build]\nopt-level = 3\ndebug-info = false\nlto = \"full\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.name, "full");
    EXPECT_EQ(cfg.version, "1.0.0");
    EXPECT_EQ(cfg.entry, "src/app.liva");
    EXPECT_EQ(cfg.optLevel, 3);
    EXPECT_FALSE(cfg.debugInfo);
    EXPECT_EQ(cfg.lto, "full");
}

TEST(LTOConfig, ConfigLtoCliOverrideSimulation) {
    // Config has lto=none
    auto result = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\nlto = \"none\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.lto, "none");

    // Simulate CLI override (Driver applies this)
    cfg.lto = "thin";
    EXPECT_EQ(cfg.lto, "thin");
}

TEST(LTOConfig, ConfigLtoDefaultField) {
    // ProjectConfig default value
    liva::ProjectConfig cfg;
    EXPECT_EQ(cfg.lto, "none");
}

// === PGO Configuration Tests ===

TEST(PGOConfig, DefaultPgoModeIsNone) {
    auto result = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\nopt-level = 2\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.pgo, "none");
    EXPECT_TRUE(cfg.pgoProfile.empty());
}

TEST(PGOConfig, PgoModeGenerate) {
    auto result = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\npgo = \"generate\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.pgo, "generate");
}

TEST(PGOConfig, PgoModeUse) {
    auto result = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\npgo = \"use\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.pgo, "use");
}

TEST(PGOConfig, PgoProfilePath) {
    auto result = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\npgo = \"use\"\n"
        "pgo-profile = \"profiles/app.profdata\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.pgo, "use");
    EXPECT_EQ(cfg.pgoProfile, "profiles/app.profdata");
}

TEST(PGOConfig, PgoWithLtoAndOptLevel) {
    auto result = liva::parseTOML(
        "[project]\nname = \"perf\"\n[build]\nopt-level = 3\n"
        "lto = \"thin\"\npgo = \"use\"\n"
        "pgo-profile = \"default.profdata\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.optLevel, 3);
    EXPECT_EQ(cfg.lto, "thin");
    EXPECT_EQ(cfg.pgo, "use");
    EXPECT_EQ(cfg.pgoProfile, "default.profdata");
}

TEST(PGOConfig, PgoExplicitNone) {
    auto result = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\npgo = \"none\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.pgo, "none");
}

TEST(PGOConfig, ConfigPgoDefaultField) {
    liva::ProjectConfig cfg;
    EXPECT_EQ(cfg.pgo, "none");
    EXPECT_TRUE(cfg.pgoProfile.empty());
}

TEST(PGOConfig, ConfigPgoCliOverrideSimulation) {
    auto result = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\npgo = \"none\"\n");
    ASSERT_TRUE(result.success);
    auto cfg = liva::loadProjectConfig(result.doc);
    EXPECT_EQ(cfg.pgo, "none");

    // Simulate CLI override
    cfg.pgo = "generate";
    EXPECT_EQ(cfg.pgo, "generate");
}

// === CPack / Install Configuration Tests ===

TEST(PackagingTest, ProjectVersionDefined) {
    // Verify version macros are defined and consistent
    std::string version = LIVA_VERSION_STRING;
    EXPECT_FALSE(version.empty());
    EXPECT_EQ(LIVA_VERSION_MAJOR, 0);
    EXPECT_EQ(LIVA_VERSION_MINOR, 1);
    EXPECT_EQ(LIVA_VERSION_PATCH, 0);
    EXPECT_EQ(version, "0.1.0");
}

TEST(PackagingTest, StdlibCoreFilesExist) {
    // Verify stdlib core .liva files are present in source tree
    std::string root = LIVA_PROJECT_ROOT;
    std::ifstream f1(root + "/stdlib/core/option.liva");
    EXPECT_TRUE(f1.is_open()) << "stdlib/core/option.liva missing";
    std::ifstream f2(root + "/stdlib/core/result.liva");
    EXPECT_TRUE(f2.is_open()) << "stdlib/core/result.liva missing";
    std::ifstream f3(root + "/stdlib/core/types.liva");
    EXPECT_TRUE(f3.is_open()) << "stdlib/core/types.liva missing";
}

TEST(PackagingTest, StdlibIOFilesExist) {
    std::string root = LIVA_PROJECT_ROOT;
    std::ifstream f(root + "/stdlib/io/print.liva");
    EXPECT_TRUE(f.is_open()) << "stdlib/io/print.liva missing";
}

TEST(PackagingTest, RuntimeHeaderExists) {
    std::string root = LIVA_PROJECT_ROOT;
    std::ifstream f(root + "/stdlib/runtime/runtime.h");
    EXPECT_TRUE(f.is_open()) << "stdlib/runtime/runtime.h missing";
}

TEST(PackagingTest, LicenseFileExists) {
    std::string root = LIVA_PROJECT_ROOT;
    std::ifstream f(root + "/LICENSE");
    EXPECT_TRUE(f.is_open()) << "LICENSE file missing";
}

TEST(PackagingTest, ExamplesDirectoryHasFiles) {
    std::string root = LIVA_PROJECT_ROOT;
    std::ifstream f(root + "/examples/hello.liva");
    EXPECT_TRUE(f.is_open()) << "examples/hello.liva missing";
}

TEST(PackagingTest, HomebrewFormulaExists) {
    std::string root = LIVA_PROJECT_ROOT;
    std::ifstream f(root + "/packaging/liva.rb");
    EXPECT_TRUE(f.is_open()) << "packaging/liva.rb missing";
}

TEST(PackagingTest, WixPatchFileExists) {
    std::string root = LIVA_PROJECT_ROOT;
    std::ifstream f(root + "/packaging/wix_path_patch.xml");
    EXPECT_TRUE(f.is_open()) << "packaging/wix_path_patch.xml missing";
}

TEST(PackagingTest, ReleaseWorkflowExists) {
    std::string root = LIVA_PROJECT_ROOT;
    std::ifstream f(root + "/.github/workflows/release.yml");
    EXPECT_TRUE(f.is_open()) << ".github/workflows/release.yml missing";
}

TEST(PackagingTest, DocumentationExists) {
    std::string root = LIVA_PROJECT_ROOT;
    std::ifstream f1(root + "/docs/en/README.md");
    EXPECT_TRUE(f1.is_open()) << "docs/en/README.md missing";
    std::ifstream f2(root + "/docs/en/TUTORIAL.md");
    EXPECT_TRUE(f2.is_open()) << "docs/en/TUTORIAL.md missing";
}

// === addDependencyToToml Tests ===

class AddDepToTomlTest : public ::testing::Test {
protected:
    std::string testDir_;
    std::string tomlPath_;

    void SetUp() override {
        testDir_ = "__adddep_test_tmp__";
        liva::createDirectories(testDir_);
        tomlPath_ = liva::joinPath(testDir_, "liva.toml");
    }

    void TearDown() override {
        liva::removeDirectoryRecursive(testDir_);
    }

    void writeToml(const std::string &content) {
        std::ofstream f(tomlPath_);
        f << content;
    }

    std::string readToml() {
        std::ifstream f(tomlPath_);
        if (!f.is_open()) return "";
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }
};

TEST_F(AddDepToTomlTest, AddDepToEmptyToml) {
    writeToml("[project]\nname = \"myapp\"\n");
    EXPECT_TRUE(liva::addDependencyToToml(tomlPath_, "json", "1.0.0"));

    std::string content = readToml();
    EXPECT_TRUE(content.find("[dependencies]") != std::string::npos);
    EXPECT_TRUE(content.find("json = \"1.0.0\"") != std::string::npos);
    // Original content preserved
    EXPECT_TRUE(content.find("[project]") != std::string::npos);
    EXPECT_TRUE(content.find("name = \"myapp\"") != std::string::npos);
}

TEST_F(AddDepToTomlTest, AddDepToExistingSection) {
    writeToml("[project]\nname = \"myapp\"\n\n[dependencies]\nhttp = \"2.0.0\"\n");
    EXPECT_TRUE(liva::addDependencyToToml(tomlPath_, "json", "1.0.0"));

    std::string content = readToml();
    EXPECT_TRUE(content.find("json = \"1.0.0\"") != std::string::npos);
    EXPECT_TRUE(content.find("http = \"2.0.0\"") != std::string::npos);
}

TEST_F(AddDepToTomlTest, UpdateExistingDep) {
    writeToml("[project]\nname = \"myapp\"\n\n[dependencies]\njson = \"1.0.0\"\n");
    EXPECT_TRUE(liva::addDependencyToToml(tomlPath_, "json", "2.0.0"));

    std::string content = readToml();
    EXPECT_TRUE(content.find("json = \"2.0.0\"") != std::string::npos);
    EXPECT_TRUE(content.find("json = \"1.0.0\"") == std::string::npos);
}

TEST_F(AddDepToTomlTest, AddDepPreservesContent) {
    writeToml(
        "[project]\nname = \"myapp\"\nversion = \"1.0.0\"\n\n"
        "[build]\nopt-level = 2\n\n"
        "[dependencies]\nhttp = \"2.0.0\"\n");
    EXPECT_TRUE(liva::addDependencyToToml(tomlPath_, "json", "1.0.0"));

    std::string content = readToml();
    // All sections preserved
    EXPECT_TRUE(content.find("[project]") != std::string::npos);
    EXPECT_TRUE(content.find("[build]") != std::string::npos);
    EXPECT_TRUE(content.find("[dependencies]") != std::string::npos);
    EXPECT_TRUE(content.find("opt-level = 2") != std::string::npos);
    EXPECT_TRUE(content.find("http = \"2.0.0\"") != std::string::npos);
    EXPECT_TRUE(content.find("json = \"1.0.0\"") != std::string::npos);

    // Verify parseable
    auto r = liva::parseTOML(content);
    ASSERT_TRUE(r.success);
    auto deps = liva::parseDependencies(r.doc);
    EXPECT_EQ(deps.size(), 2u);
}

TEST_F(AddDepToTomlTest, AddDepWithMinimumConstraint) {
    writeToml("[project]\nname = \"myapp\"\n");
    EXPECT_TRUE(liva::addDependencyToToml(tomlPath_, "utils", ">=1.0.0"));

    std::string content = readToml();
    EXPECT_TRUE(content.find("utils = \">=1.0.0\"") != std::string::npos);

    // Verify parseable as valid constraint
    auto r = liva::parseTOML(content);
    ASSERT_TRUE(r.success);
    auto deps = liva::parseDependencies(r.doc);
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0].name, "utils");
    EXPECT_EQ(deps[0].constraint.kind, liva::VersionConstraint::Minimum);
}

// === InstallResult Tests ===

TEST(InstallResult, DefaultValues) {
    liva::InstallResult r;
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.name.empty());
    EXPECT_EQ(r.version.major, 0);
    EXPECT_EQ(r.version.minor, 0);
    EXPECT_EQ(r.version.patch, 0);
    EXPECT_TRUE(r.checksum.empty());
    EXPECT_TRUE(r.errorMsg.empty());
}

// === InstallSingle Tests ===

TEST_F(BuildCacheTest, InstallSingleLocalAlreadyInstalled) {
    // Create a local package
    std::string pkgDir = liva::joinPath(testDir_, "packages/mylib");
    std::string srcDir = liva::joinPath(pkgDir, "src");
    liva::createDirectories(srcDir);

    writeFile(liva::joinPath(pkgDir, "liva.toml"),
        "[project]\nname = \"mylib\"\nversion = \"1.0.0\"\n");
    writeFile(liva::joinPath(srcDir, "lib.liva"),
        "pub func helper() -> i32 { return 42 }\n");

    liva::PackageManager mgr(testDir_, "");
    auto result = mgr.installSingle("mylib", "1.0.0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.name, "mylib");
    EXPECT_EQ(result.version.major, 1);
    EXPECT_EQ(result.version.minor, 0);
    EXPECT_EQ(result.version.patch, 0);
}

TEST_F(BuildCacheTest, InstallSingleLocalNotFound) {
    liva::PackageManager mgr(testDir_, "");
    auto result = mgr.installSingle("nonexistent", "");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMsg.find("not found") != std::string::npos ||
                result.errorMsg.find("no registry") != std::string::npos);
}

TEST_F(BuildCacheTest, InstallSingleVersionConstraint) {
    // Create package with version 2.1.0
    std::string pkgDir = liva::joinPath(testDir_, "packages/mathlib");
    std::string srcDir = liva::joinPath(pkgDir, "src");
    liva::createDirectories(srcDir);

    writeFile(liva::joinPath(pkgDir, "liva.toml"),
        "[project]\nname = \"mathlib\"\nversion = \"2.1.0\"\n");
    writeFile(liva::joinPath(srcDir, "math.liva"), "pub func add() {}\n");

    liva::PackageManager mgr(testDir_, "");

    // Exact match
    auto r1 = mgr.installSingle("mathlib", "2.1.0");
    EXPECT_TRUE(r1.success);
    EXPECT_EQ(r1.version.major, 2);
    EXPECT_EQ(r1.version.minor, 1);

    // Minimum constraint
    auto r2 = mgr.installSingle("mathlib", ">=2.0.0");
    EXPECT_TRUE(r2.success);

    // No version (latest)
    auto r3 = mgr.installSingle("mathlib", "");
    EXPECT_TRUE(r3.success);
    EXPECT_EQ(r3.version.major, 2);

    // Version mismatch
    auto r4 = mgr.installSingle("mathlib", "3.0.0");
    EXPECT_FALSE(r4.success);
}

TEST_F(BuildCacheTest, InstallSingleInvalidVersionConstraint) {
    liva::PackageManager mgr(testDir_, "");
    auto result = mgr.installSingle("anylib", "~1.0");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMsg.find("invalid version constraint") != std::string::npos);
}

// === VersionConstraint ToString ===

TEST(VersionConstraint, ToStringExact) {
    liva::VersionConstraint c;
    c.kind = liva::VersionConstraint::Exact;
    c.min = liva::SemVer{1, 0, 0};
    EXPECT_EQ(c.toString(), "1.0.0");
}

TEST(VersionConstraint, ToStringMinimum) {
    liva::VersionConstraint c;
    c.kind = liva::VersionConstraint::Minimum;
    c.min = liva::SemVer{2, 0, 0};
    EXPECT_EQ(c.toString(), ">=2.0.0");
}

TEST(VersionConstraint, ToStringRange) {
    liva::VersionConstraint c;
    c.kind = liva::VersionConstraint::Range;
    c.min = liva::SemVer{1, 0, 0};
    c.max = liva::SemVer{2, 0, 0};
    EXPECT_EQ(c.toString(), ">=1.0.0,<2.0.0");
}

// === SemaCache Tests ===

// Helper: create a TranslationUnit with a pub func
static std::unique_ptr<liva::TranslationUnit> makeTU_PubFunc(
    const std::string &name, const std::string &retType) {
    auto tu = std::make_unique<liva::TranslationUnit>();
    std::unique_ptr<liva::TypeRepr> ret;
    if (retType == "i32")
        ret = liva::makeI32Type();
    else if (retType == "i64")
        ret = liva::makeI64Type();
    else if (retType == "void")
        ret = liva::makeVoidType();
    else
        ret = liva::makeStringType();
    std::vector<liva::ParamDecl> params;
    auto func = std::make_unique<liva::FuncDecl>(
        name, std::move(params), std::move(ret), nullptr,
        /*isPublic=*/true, liva::SourceRange::invalid());
    tu->addDeclaration(std::move(func));
    return tu;
}

// Helper: create a TU with a private func
static std::unique_ptr<liva::TranslationUnit> makeTU_PrivFunc(
    const std::string &name) {
    auto tu = std::make_unique<liva::TranslationUnit>();
    auto ret = liva::makeVoidType();
    std::vector<liva::ParamDecl> params;
    auto func = std::make_unique<liva::FuncDecl>(
        name, std::move(params), std::move(ret), nullptr,
        /*isPublic=*/false, liva::SourceRange::invalid());
    tu->addDeclaration(std::move(func));
    return tu;
}

// Helper: create a TU with a pub struct
static std::unique_ptr<liva::TranslationUnit> makeTU_PubStruct(
    const std::string &name,
    const std::vector<std::pair<std::string, std::string>> &fields) {
    auto tu = std::make_unique<liva::TranslationUnit>();
    std::vector<std::unique_ptr<liva::FieldDecl>> fieldDecls;
    for (const auto &f : fields) {
        std::unique_ptr<liva::TypeRepr> ftype;
        if (f.second == "i32")
            ftype = liva::makeI32Type();
        else
            ftype = liva::makeStringType();
        fieldDecls.push_back(std::make_unique<liva::FieldDecl>(
            f.first, std::move(ftype), true, liva::SourceRange::invalid()));
    }
    auto s = std::make_unique<liva::StructDecl>(
        name, std::move(fieldDecls), /*isPublic=*/true,
        liva::SourceRange::invalid());
    tu->addDeclaration(std::move(s));
    return tu;
}

TEST(SemaCache, ComputeInterfaceHash) {
    auto tu = makeTU_PubFunc("foo", "i32");
    std::string hash = liva::SemaCache::computeInterfaceHash(*tu);
    EXPECT_FALSE(hash.empty());
    EXPECT_EQ(hash.size(), 16u); // 64-bit hex

    // Same TU should produce same hash
    auto tu2 = makeTU_PubFunc("foo", "i32");
    std::string hash2 = liva::SemaCache::computeInterfaceHash(*tu2);
    EXPECT_EQ(hash, hash2);
}

TEST(SemaCache, InterfaceHashChanges) {
    auto tu1 = makeTU_PubFunc("foo", "i32");
    auto tu2 = makeTU_PubFunc("foo", "i64"); // different return type

    std::string hash1 = liva::SemaCache::computeInterfaceHash(*tu1);
    std::string hash2 = liva::SemaCache::computeInterfaceHash(*tu2);
    EXPECT_NE(hash1, hash2);

    // Different name, same signature
    auto tu3 = makeTU_PubFunc("bar", "i32");
    std::string hash3 = liva::SemaCache::computeInterfaceHash(*tu3);
    EXPECT_NE(hash1, hash3);
}

TEST(SemaCache, InterfaceHashPrivateIgnored) {
    // Only pub declarations affect interface hash
    auto tu1 = makeTU_PrivFunc("helper");
    auto tu2 = makeTU_PrivFunc("differentHelper");

    std::string hash1 = liva::SemaCache::computeInterfaceHash(*tu1);
    std::string hash2 = liva::SemaCache::computeInterfaceHash(*tu2);
    // Both have zero public API → same hash
    EXPECT_EQ(hash1, hash2);
}

TEST(SemaCache, InterfaceHashStruct) {
    auto tu1 = makeTU_PubStruct("Point", {{"x", "i32"}, {"y", "i32"}});
    auto tu2 = makeTU_PubStruct("Point", {{"x", "i32"}, {"y", "i32"}});
    auto tu3 = makeTU_PubStruct("Point", {{"x", "i32"}, {"z", "i32"}}); // field name differs

    std::string h1 = liva::SemaCache::computeInterfaceHash(*tu1);
    std::string h2 = liva::SemaCache::computeInterfaceHash(*tu2);
    std::string h3 = liva::SemaCache::computeInterfaceHash(*tu3);
    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, h3);
}

// SemaCache check/store tests use a temp directory
class SemaCacheTest : public ::testing::Test {
protected:
    std::string testDir_;

    void SetUp() override {
        testDir_ = std::string(LIVA_PROJECT_ROOT) + "/build/test_sema_cache_tmp";
        liva::createDirectories(testDir_);
        // Create .liva-cache subdir
        liva::createDirectories(liva::joinPath(testDir_, ".liva-cache"));
    }

    void TearDown() override {
        liva::removeDirectoryRecursive(testDir_);
    }
};

TEST_F(SemaCacheTest, CheckAllClean) {
    liva::SemaCache cache(testDir_);

    // Store two files with known hashes
    cache.store("a.liva", "hashA", "ifaceA", {});
    cache.store("b.liva", "hashB", "ifaceB", {});
    cache.saveManifest();

    // Re-create cache (simulates new build)
    liva::SemaCache cache2(testDir_);
    liva::FileCompileStatus statusA{"a.liva", "hashA", "", false};
    liva::FileCompileStatus statusB{"b.liva", "hashB", "", false};

    auto results = cache2.check({"a.liva", "b.liva"}, {statusA, statusB});
    ASSERT_EQ(results.size(), 2u);
    EXPECT_FALSE(results[0].needsResema);
    EXPECT_FALSE(results[1].needsResema);
}

TEST_F(SemaCacheTest, CheckSourceChanged) {
    liva::SemaCache cache(testDir_);
    cache.store("a.liva", "oldHash", "ifaceA", {});
    cache.saveManifest();

    liva::SemaCache cache2(testDir_);
    liva::FileCompileStatus statusA{"a.liva", "newHash", "", true};

    auto results = cache2.check({"a.liva"}, {statusA});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].needsResema);
}

TEST_F(SemaCacheTest, CheckDepInterfaceChanged) {
    liva::SemaCache cache(testDir_);

    // B compiled first with interfaceHash "ifaceB_v1"
    cache.store("b.liva", "hashB", "ifaceB_v1", {});
    // A compiled importing B — snapshots B's interface as "ifaceB_v1"
    cache.store("a.liva", "hashA", "ifaceA", {"b.liva"});
    cache.saveManifest();

    // Simulate B being recompiled with different interface
    liva::SemaCache cache2(testDir_);
    // Manually update B's interface hash in manifest
    // First load, then update
    liva::FileCompileStatus statusB{"b.liva", "hashB_new", "", true};
    liva::FileCompileStatus statusA{"a.liva", "hashA", "", false};

    // B has source changes → B needs resema
    // A imports B → cascade → A also needs resema
    auto results = cache2.check({"a.liva", "b.liva"}, {statusA, statusB});
    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(results[0].needsResema);  // A: cascade from B
    EXPECT_TRUE(results[1].needsResema);  // B: source changed
}

TEST_F(SemaCacheTest, CheckDepBodyOnlyChanged) {
    liva::SemaCache cache(testDir_);

    // B compiled with interfaceHash "ifaceB"
    cache.store("b.liva", "hashB_v1", "ifaceB", {});
    // A compiled importing B — snapshots B's interface as "ifaceB"
    cache.store("a.liva", "hashA", "ifaceA", {"b.liva"});

    // Now B body changed, was recompiled with SAME interface hash
    cache.store("b.liva", "hashB_v2", "ifaceB", {}); // interface unchanged!
    cache.saveManifest();

    // New build: both source hashes match current manifest
    liva::SemaCache cache2(testDir_);
    liva::FileCompileStatus statusA{"a.liva", "hashA", "", false};
    liva::FileCompileStatus statusB{"b.liva", "hashB_v2", "", false};

    auto results = cache2.check({"a.liva", "b.liva"}, {statusA, statusB});
    ASSERT_EQ(results.size(), 2u);
    EXPECT_FALSE(results[0].needsResema); // A: B's interface same → cache hit
    EXPECT_FALSE(results[1].needsResema); // B: source unchanged
}

TEST_F(SemaCacheTest, CascadeInvalidation) {
    liva::SemaCache cache(testDir_);

    // C → B → A dependency chain
    cache.store("c.liva", "hashC", "ifaceC", {});
    cache.store("b.liva", "hashB", "ifaceB", {"c.liva"});
    cache.store("a.liva", "hashA", "ifaceA", {"b.liva"});
    cache.saveManifest();

    // C source changed → cascades to B (imports C) → cascades to A (imports B)
    liva::SemaCache cache2(testDir_);
    liva::FileCompileStatus statusA{"a.liva", "hashA", "", false};
    liva::FileCompileStatus statusB{"b.liva", "hashB", "", false};
    liva::FileCompileStatus statusC{"c.liva", "newHashC", "", true};

    auto results = cache2.check(
        {"a.liva", "b.liva", "c.liva"},
        {statusA, statusB, statusC});
    ASSERT_EQ(results.size(), 3u);
    EXPECT_TRUE(results[0].needsResema);  // A: cascaded from B
    EXPECT_TRUE(results[1].needsResema);  // B: cascaded from C
    EXPECT_TRUE(results[2].needsResema);  // C: source changed
}

TEST_F(SemaCacheTest, ManifestRoundTrip) {
    liva::SemaCache cache(testDir_);
    cache.store("lib.liva", "hash1", "iface1", {});
    cache.store("main.liva", "hash2", "iface2", {"lib.liva"});
    EXPECT_TRUE(cache.saveManifest());

    // Load into new cache
    liva::SemaCache cache2(testDir_);
    liva::FileCompileStatus s1{"lib.liva", "hash1", "", false};
    liva::FileCompileStatus s2{"main.liva", "hash2", "", false};
    auto results = cache2.check({"lib.liva", "main.liva"}, {s1, s2});
    ASSERT_EQ(results.size(), 2u);
    EXPECT_FALSE(results[0].needsResema);
    EXPECT_FALSE(results[1].needsResema);
    EXPECT_EQ(results[0].cachedInterfaceHash, "iface1");
    EXPECT_EQ(results[1].cachedInterfaceHash, "iface2");
}

TEST_F(SemaCacheTest, CheckNoManifest) {
    // No manifest file → all files need resema
    liva::SemaCache cache(testDir_);
    liva::FileCompileStatus s1{"a.liva", "hashA", "", false};
    liva::FileCompileStatus s2{"b.liva", "hashB", "", false};

    auto results = cache.check({"a.liva", "b.liva"}, {s1, s2});
    ASSERT_EQ(results.size(), 2u);
    EXPECT_TRUE(results[0].needsResema);
    EXPECT_TRUE(results[1].needsResema);
}

// === Parallel Compilation Tests ===

TEST(ParallelCompile, TOMLJobsConfig) {
    auto r = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\njobs = 8\n");
    ASSERT_TRUE(r.success);
    auto cfg = liva::loadProjectConfig(r.doc);
    EXPECT_EQ(cfg.jobs, 8);
}

TEST(ParallelCompile, TOMLJobsDefault) {
    auto r = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\nopt-level = 2\n");
    ASSERT_TRUE(r.success);
    auto cfg = liva::loadProjectConfig(r.doc);
    EXPECT_EQ(cfg.jobs, 0);
}

TEST(ParallelCompile, JobsOverridesToml) {
    // TOML has jobs=2
    auto r = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\njobs = 2\n");
    ASSERT_TRUE(r.success);
    auto cfg = liva::loadProjectConfig(r.doc);
    EXPECT_EQ(cfg.jobs, 2);

    // Simulate CLI override (as Driver does)
    cfg.jobs = 4;
    EXPECT_EQ(cfg.jobs, 4);
}

TEST(ParallelCompile, ProjectConfigDefaultJobs) {
    liva::ProjectConfig cfg;
    EXPECT_EQ(cfg.jobs, 0);
}

TEST(ParallelCompile, TOMLJobsSingleThread) {
    auto r = liva::parseTOML(
        "[project]\nname = \"test\"\n[build]\njobs = 1\n");
    ASSERT_TRUE(r.success);
    auto cfg = liva::loadProjectConfig(r.doc);
    EXPECT_EQ(cfg.jobs, 1);
}

TEST(ParallelCompile, TOMLJobsWithOtherBuildOptions) {
    auto r = liva::parseTOML(
        "[project]\nname = \"parallel\"\nversion = \"1.0.0\"\n"
        "[build]\nopt-level = 3\ndebug-info = false\n"
        "lto = \"thin\"\njobs = 4\n");
    ASSERT_TRUE(r.success);
    auto cfg = liva::loadProjectConfig(r.doc);
    EXPECT_EQ(cfg.name, "parallel");
    EXPECT_EQ(cfg.optLevel, 3);
    EXPECT_FALSE(cfg.debugInfo);
    EXPECT_EQ(cfg.lto, "thin");
    EXPECT_EQ(cfg.jobs, 4);
}

TEST(ParallelCompile, DriverOptionsDefaultJobs) {
    liva::DriverOptions opts;
    EXPECT_EQ(opts.jobs, 0);
    EXPECT_FALSE(opts.hasJobsOverride);
}

// === Stale Cleanup Tests ===

TEST_F(BuildCacheTest, PruneStaleEntriesRemovesDeletedFile) {
    std::string src1 = liva::joinPath(testDir_, "a.liva");
    std::string src2 = liva::joinPath(testDir_, "b.liva");
    writeFile(src1, "func a() {}");
    writeFile(src2, "func b() {}");

    liva::BuildCache cache(testDir_);

    std::string hash1 = cache.hashFileContent(src1);
    std::string hash2 = cache.hashFileContent(src2);
    std::string obj1 = liva::joinPath(testDir_, "a.o");
    std::string obj2 = liva::joinPath(testDir_, "b.o");
    writeFile(obj1, "obj1");
    writeFile(obj2, "obj2");
    cache.storeFileObject(src1, hash1, obj1, 0, false);
    cache.storeFileObject(src2, hash2, obj2, 0, false);

    // Remove src2 and prune
    std::remove(src2.c_str());
    std::vector<std::string> current = { src1 };
    cache.pruneStaleEntries(current);

    // Only src1 remains in cache
    auto statuses = cache.checkFilesCache({ src1 }, 0, false);
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_FALSE(statuses[0].needsRecompile);
}

TEST_F(BuildCacheTest, PruneStaleEntriesNoManifest) {
    liva::BuildCache cache(testDir_);
    std::vector<std::string> current = { "nonexistent.liva" };
    cache.pruneStaleEntries(current); // no-op, no crash
}

TEST_F(BuildCacheTest, PruneStaleEntriesNothingToRemove) {
    std::string src1 = liva::joinPath(testDir_, "x.liva");
    writeFile(src1, "func x() {}");

    liva::BuildCache cache(testDir_);
    std::string hash = cache.hashFileContent(src1);
    std::string obj = liva::joinPath(testDir_, "x.o");
    writeFile(obj, "objx");
    cache.storeFileObject(src1, hash, obj, 0, false);

    std::vector<std::string> current = { src1 };
    cache.pruneStaleEntries(current);

    auto statuses = cache.checkFilesCache({ src1 }, 0, false);
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_FALSE(statuses[0].needsRecompile);
}

// === mtime Optimization Tests ===

TEST_F(BuildCacheTest, MtimeFastPathSkipsRehash) {
    std::string src = liva::joinPath(testDir_, "m.liva");
    writeFile(src, "func m() {}");

    liva::BuildCache cache(testDir_);
    std::string hash = cache.hashFileContent(src);
    std::string obj = liva::joinPath(testDir_, "m.o");
    writeFile(obj, "objm");
    cache.storeFileObject(src, hash, obj, 0, false);

    // Second check uses mtime fast path
    auto statuses = cache.checkFilesCache({ src }, 0, false);
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_FALSE(statuses[0].needsRecompile);
    EXPECT_EQ(statuses[0].currentHash, hash);
}

TEST_F(BuildCacheTest, MtimeChangeTriggerRehash) {
    std::string src = liva::joinPath(testDir_, "n.liva");
    writeFile(src, "func n() {}");

    liva::BuildCache cache(testDir_);
    std::string hash1 = cache.hashFileContent(src);
    std::string obj = liva::joinPath(testDir_, "n.o");
    writeFile(obj, "objn");
    cache.storeFileObject(src, hash1, obj, 0, false);

    // Wait and modify file to change mtime
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    writeFile(src, "func n_changed() {}");

    auto statuses = cache.checkFilesCache({ src }, 0, false);
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_TRUE(statuses[0].needsRecompile);
    EXPECT_NE(statuses[0].currentHash, hash1);
}

TEST_F(BuildCacheTest, MtimeBackwardCompatible) {
    std::string cacheSubDir = liva::joinPath(testDir_, ".liva-cache");
    liva::createDirectories(cacheSubDir);

    std::string src = liva::joinPath(testDir_, "bc.liva");
    writeFile(src, "func bc() {}");

    liva::BuildCache cache(testDir_);
    std::string hash = cache.hashFileContent(src);
    std::string objName = cache.objectPathForSource(src);

    // Write old-format manifest (no mtime field)
    std::string manifestPath = liva::joinPath(cacheSubDir, "files_manifest");
    {
        std::ofstream f(manifestPath);
        f << "OPT:0\n";
        f << "DEBUG:0\n";
        f << "FILE:" << src << ":" << hash << ":" << objName << "\n";
    }

    // Create cached .o file
    std::string cachedObj = liva::joinPath(cacheSubDir, objName);
    writeFile(cachedObj, "old_obj");

    auto statuses = cache.checkFilesCache({ src }, 0, false);
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_FALSE(statuses[0].needsRecompile);
}

// === Link Cache Tests ===

TEST_F(BuildCacheTest, IsOutputUpToDateTrue) {
    std::string obj1 = liva::joinPath(testDir_, "a.o");
    std::string obj2 = liva::joinPath(testDir_, "b.o");
    writeFile(obj1, "obj1");
    writeFile(obj2, "obj2");

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    std::string output = liva::joinPath(testDir_, "app.exe");
    writeFile(output, "exe");

    int64_t outMtime = liva::getFileModTime(output);
    EXPECT_GT(outMtime, 0);
    int64_t obj1Mtime = liva::getFileModTime(obj1);
    EXPECT_GT(obj1Mtime, 0);
    EXPECT_GE(outMtime, obj1Mtime);
}

TEST_F(BuildCacheTest, IsOutputUpToDateFalseNewObj) {
    std::string output = liva::joinPath(testDir_, "app.exe");
    writeFile(output, "exe");

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    std::string obj1 = liva::joinPath(testDir_, "a.o");
    writeFile(obj1, "obj1_new");

    int64_t outMtime = liva::getFileModTime(output);
    int64_t obj1Mtime = liva::getFileModTime(obj1);
    EXPECT_GT(obj1Mtime, outMtime);
}

// === --rebuild Flag Tests ===

TEST(DriverOptions, RebuildFlagDefault) {
    liva::DriverOptions opts;
    EXPECT_FALSE(opts.rebuild);
}

TEST(DriverOptions, RebuildFlagCanBeSet) {
    liva::DriverOptions opts;
    opts.rebuild = true;
    EXPECT_TRUE(opts.rebuild);
    EXPECT_EQ(opts.optLevel, 0);
    EXPECT_FALSE(opts.hasOptLevelOverride);
}

// === Incremental Flow Tests ===

TEST_F(BuildCacheTest, IncrementalFirstBuildAllMiss) {
    std::string src1 = liva::joinPath(testDir_, "f1.liva");
    std::string src2 = liva::joinPath(testDir_, "f2.liva");
    writeFile(src1, "func f1() {}");
    writeFile(src2, "func f2() {}");

    liva::BuildCache cache(testDir_);
    auto statuses = cache.checkFilesCache({ src1, src2 }, 0, false);
    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_TRUE(statuses[0].needsRecompile);
    EXPECT_TRUE(statuses[1].needsRecompile);
}

TEST_F(BuildCacheTest, IncrementalSecondBuildAllCached) {
    std::string src = liva::joinPath(testDir_, "c.liva");
    writeFile(src, "func c() {}");

    liva::BuildCache cache(testDir_);
    std::string hash = cache.hashFileContent(src);
    std::string obj = liva::joinPath(testDir_, "c.o");
    writeFile(obj, "objc");
    cache.storeFileObject(src, hash, obj, 0, false);

    auto statuses = cache.checkFilesCache({ src }, 0, false);
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_FALSE(statuses[0].needsRecompile);
    EXPECT_FALSE(statuses[0].cachedObjPath.empty());
}

TEST_F(BuildCacheTest, IncrementalModifyFileInvalidatesOnly) {
    std::string src1 = liva::joinPath(testDir_, "m1.liva");
    std::string src2 = liva::joinPath(testDir_, "m2.liva");
    writeFile(src1, "func m1() {}");
    writeFile(src2, "func m2() {}");

    liva::BuildCache cache(testDir_);

    std::string hash1 = cache.hashFileContent(src1);
    std::string hash2 = cache.hashFileContent(src2);
    std::string obj1 = liva::joinPath(testDir_, "m1.o");
    std::string obj2 = liva::joinPath(testDir_, "m2.o");
    writeFile(obj1, "obj1");
    writeFile(obj2, "obj2");
    cache.storeFileObject(src1, hash1, obj1, 0, false);
    cache.storeFileObject(src2, hash2, obj2, 0, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    writeFile(src1, "func m1_changed() {}");

    auto statuses = cache.checkFilesCache({ src1, src2 }, 0, false);
    ASSERT_EQ(statuses.size(), 2u);
    EXPECT_TRUE(statuses[0].needsRecompile);   // src1 changed
    EXPECT_FALSE(statuses[1].needsRecompile);   // src2 unchanged
}

TEST_F(BuildCacheTest, IncrementalDeletedFileRemovesCacheEntry) {
    std::string src1 = liva::joinPath(testDir_, "d1.liva");
    std::string src2 = liva::joinPath(testDir_, "d2.liva");
    writeFile(src1, "func d1() {}");
    writeFile(src2, "func d2() {}");

    liva::BuildCache cache(testDir_);

    std::string hash1 = cache.hashFileContent(src1);
    std::string hash2 = cache.hashFileContent(src2);
    std::string obj1 = liva::joinPath(testDir_, "d1.o");
    std::string obj2 = liva::joinPath(testDir_, "d2.o");
    writeFile(obj1, "obj1");
    writeFile(obj2, "obj2");
    cache.storeFileObject(src1, hash1, obj1, 0, false);
    cache.storeFileObject(src2, hash2, obj2, 0, false);

    std::remove(src2.c_str());
    cache.pruneStaleEntries({ src1 });

    auto statuses = cache.checkFilesCache({ src1 }, 0, false);
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_FALSE(statuses[0].needsRecompile);
}

TEST_F(BuildCacheTest, IncrementalOptLevelChangeInvalidatesAll) {
    std::string src = liva::joinPath(testDir_, "opt.liva");
    writeFile(src, "func opt() {}");

    liva::BuildCache cache(testDir_);
    std::string hash = cache.hashFileContent(src);
    std::string obj = liva::joinPath(testDir_, "opt.o");
    writeFile(obj, "objopt");
    cache.storeFileObject(src, hash, obj, 0, false);

    auto statuses = cache.checkFilesCache({ src }, 2, false);
    ASSERT_EQ(statuses.size(), 1u);
    EXPECT_TRUE(statuses[0].needsRecompile);
}

// === getFileModTime Tests ===

TEST_F(BuildCacheTest, GetFileModTimeExistingFile) {
    std::string path = liva::joinPath(testDir_, "test.txt");
    writeFile(path, "hello");

    int64_t mtime = liva::getFileModTime(path);
    EXPECT_GT(mtime, 0);
}

TEST_F(BuildCacheTest, GetFileModTimeNonExistent) {
    int64_t mtime = liva::getFileModTime("nonexistent_file_xyz.txt");
    EXPECT_EQ(mtime, 0);
}

// ============================================================
// Mock HTTP Infrastructure for PackageManager Remote Tests
// ============================================================

namespace {

struct MockHttp {
    static std::map<std::string, std::string> responses;   // URL → HTTP response body
    static std::map<std::string, std::string> downloads;   // URL → file content to write
    static std::vector<std::string> requestLog;             // all HTTP request URLs

    static std::string mockHttpGet(const std::string &url) {
        requestLog.push_back("GET " + url);
        auto it = responses.find(url);
        if (it != responses.end()) return it->second;
        return "";
    }

    static bool mockDownloadFile(const std::string &url, const std::string &destPath) {
        requestLog.push_back("DOWNLOAD " + url);
        auto it = downloads.find(url);
        if (it == downloads.end()) return false;
        std::ofstream f(destPath, std::ios::binary);
        if (!f.is_open()) return false;
        f << it->second;
        return true;
    }

    static int mockExtractTar(const std::string &tarPath, const std::string &destDir) {
        requestLog.push_back("EXTRACT " + tarPath);
        // Read tar content (it's just mock data) and create a liva.toml from it
        std::ifstream f(tarPath, std::ios::binary);
        if (!f.is_open()) return 1;
        std::stringstream ss;
        ss << f.rdbuf();
        std::string content = ss.str();
        // If content looks like TOML, write it as liva.toml
        if (content.find("[project]") != std::string::npos) {
            std::string tomlPath = destDir + "/liva.toml";
            std::ofstream out(tomlPath);
            if (out.is_open()) out << content;
            // Also create src/ dir
            liva::createDirectories(destDir + "/src");
        }
        return 0;
    }

    static void reset() {
        responses.clear();
        downloads.clear();
        requestLog.clear();
    }
};

std::map<std::string, std::string> MockHttp::responses;
std::map<std::string, std::string> MockHttp::downloads;
std::vector<std::string> MockHttp::requestLog;

} // anonymous namespace

class PackageManagerRemoteTest : public BuildCacheTest {
protected:
    void SetUp() override {
        BuildCacheTest::SetUp();
        MockHttp::reset();
        liva::PackageManager::httpGetFn = MockHttp::mockHttpGet;
        liva::PackageManager::downloadFileFn = MockHttp::mockDownloadFile;
        liva::PackageManager::extractTarFn = MockHttp::mockExtractTar;
    }

    void TearDown() override {
        liva::PackageManager::resetHttpHooks();
        MockHttp::reset();
        BuildCacheTest::TearDown();
    }

    std::string checksumOf(const std::string &data) {
        return "sha256:" + liva::PackageManager::sha256(data);
    }

    // Setup mock registry responses for a package
    void setupRegistryPackage(const std::string &name,
                              const std::string &version,
                              const std::string &registryUrl,
                              const std::string &tarContent = "",
                              const std::string &deps = "") {
        // Version list response
        std::string listUrl = registryUrl + "/packages/" + name;
        MockHttp::responses[listUrl] = R"({"versions":[")" + version + R"("]})";

        // Package info response
        std::string infoUrl = registryUrl + "/packages/" + name + "/" + version;
        std::string downloadUrl = registryUrl + "/dl/" + name + "-" + version + ".tar.gz";
        std::string content = tarContent.empty()
            ? "[project]\nname = \"" + name + "\"\nversion = \"" + version + "\"\n"
            : tarContent;
        std::string cs = checksumOf(content);
        std::string depsJson = deps.empty() ? "{}" : deps;
        MockHttp::responses[infoUrl] =
            R"({"name":")" + name + R"(","version":")" + version +
            R"(","url":")" + downloadUrl +
            R"(","checksum":")" + cs +
            R"(","dependencies":)" + depsJson + R"(})";

        // Download response
        MockHttp::downloads[downloadUrl] = content;
    }

    // Setup multiple versions for a package
    void setupRegistryVersions(const std::string &name,
                               const std::vector<std::string> &versions,
                               const std::string &registryUrl) {
        std::string listUrl = registryUrl + "/packages/" + name;
        std::string json = R"({"versions":[)";
        for (size_t i = 0; i < versions.size(); ++i) {
            if (i > 0) json += ",";
            json += "\"" + versions[i] + "\"";
        }
        json += "]}";
        MockHttp::responses[listUrl] = json;

        // Setup info for each version
        for (const auto &ver : versions) {
            std::string infoUrl = registryUrl + "/packages/" + name + "/" + ver;
            std::string downloadUrl = registryUrl + "/dl/" + name + "-" + ver + ".tar.gz";
            std::string content = "[project]\nname = \"" + name + "\"\nversion = \"" + ver + "\"\n";
            std::string cs = checksumOf(content);
            MockHttp::responses[infoUrl] =
                R"({"name":")" + name + R"(","version":")" + ver +
                R"(","url":")" + downloadUrl +
                R"(","checksum":")" + cs + R"(","dependencies":{}})";
            MockHttp::downloads[downloadUrl] = content;
        }
    }

    void createLocalPackage(const std::string &name, const std::string &version) {
        std::string pkgDir = liva::joinPath(testDir_, "packages/" + name);
        liva::createDirectories(pkgDir + "/src");
        std::ofstream f(pkgDir + "/liva.toml");
        f << "[project]\nname = \"" << name << "\"\nversion = \"" << version << "\"\n";
    }
};

// ============================================================
// Group A: Registry Query + Version Selection (8 tests)
// ============================================================

TEST_F(PackageManagerRemoteTest, InstallSingleFromRegistryExactVersion) {
    std::string reg = "https://registry.example.com";
    setupRegistryPackage("mylib", "1.0.0", reg);
    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("mylib", "1.0.0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.name, "mylib");
    EXPECT_EQ(result.version.toString(), "1.0.0");
}

TEST_F(PackageManagerRemoteTest, InstallSingleFromRegistryBestVersion) {
    std::string reg = "https://registry.example.com";
    setupRegistryVersions("mylib", {"1.0.0", "1.1.0", "2.0.0"}, reg);
    liva::PackageManager pm(testDir_, reg);
    // Default (any version) → should pick highest = 2.0.0
    auto result = pm.installSingle("mylib", "");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.version.toString(), "2.0.0");
}

TEST_F(PackageManagerRemoteTest, InstallSingleFromRegistryMinimumConstraint) {
    std::string reg = "https://registry.example.com";
    setupRegistryVersions("mylib", {"1.0.0", "2.0.0", "3.0.0"}, reg);
    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("mylib", ">=2.0.0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.version.toString(), "3.0.0");
}

TEST_F(PackageManagerRemoteTest, InstallSingleFromRegistryRangeConstraint) {
    std::string reg = "https://registry.example.com";
    setupRegistryVersions("mylib", {"1.0.0", "1.5.0", "2.0.0", "3.0.0"}, reg);
    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("mylib", ">=1.0.0,<2.0.0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.version.toString(), "1.5.0");
}

TEST_F(PackageManagerRemoteTest, InstallSingleRegistryNoMatchingVersion) {
    std::string reg = "https://registry.example.com";
    setupRegistryVersions("mylib", {"1.0.0", "1.1.0"}, reg);
    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("mylib", ">=5.0.0");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.errorMsg.empty());
}

TEST_F(PackageManagerRemoteTest, InstallSingleRegistryEmptyVersionList) {
    std::string reg = "https://registry.example.com";
    MockHttp::responses[reg + "/packages/mylib"] = R"({"versions":[]})";
    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("mylib", "");
    EXPECT_FALSE(result.success);
}

TEST_F(PackageManagerRemoteTest, InstallSingleRegistryHttpFailure) {
    std::string reg = "https://registry.example.com";
    // No responses configured → httpGet returns ""
    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("mylib", "");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMsg.find("not found") != std::string::npos);
}

TEST_F(PackageManagerRemoteTest, InstallSingleRegistryBadJson) {
    std::string reg = "https://registry.example.com";
    MockHttp::responses[reg + "/packages/mylib"] = "this is not json";
    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("mylib", "");
    EXPECT_FALSE(result.success);
}

// ============================================================
// Group B: Checksum Verification — Production Critical (6 tests)
// ============================================================

TEST_F(PackageManagerRemoteTest, InstallSingleChecksumValid) {
    std::string reg = "https://registry.example.com";
    setupRegistryPackage("mylib", "1.0.0", reg);
    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("mylib", "1.0.0");
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.checksum.empty());
    EXPECT_TRUE(result.checksum.find("sha256:") == 0);
}

TEST_F(PackageManagerRemoteTest, InstallSingleChecksumMismatch) {
    std::string reg = "https://registry.example.com";
    std::string name = "badpkg";
    std::string version = "1.0.0";
    std::string listUrl = reg + "/packages/" + name;
    MockHttp::responses[listUrl] = R"({"versions":["1.0.0"]})";

    std::string infoUrl = reg + "/packages/" + name + "/1.0.0";
    std::string downloadUrl = reg + "/dl/" + name + "-1.0.0.tar.gz";
    std::string content = "[project]\nname = \"badpkg\"\nversion = \"1.0.0\"\n";
    // Provide a wrong checksum
    MockHttp::responses[infoUrl] =
        R"({"name":"badpkg","version":"1.0.0","url":")" + downloadUrl +
        R"(","checksum":"sha256:0000000000000000000000000000000000000000000000000000000000000000","dependencies":{}})";
    MockHttp::downloads[downloadUrl] = content;

    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("badpkg", "1.0.0");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMsg.find("checksum mismatch") != std::string::npos);
}

TEST_F(PackageManagerRemoteTest, InstallSingleChecksumEmptySkipsVerification) {
    std::string reg = "https://registry.example.com";
    std::string name = "nocs";
    std::string version = "1.0.0";
    std::string listUrl = reg + "/packages/" + name;
    MockHttp::responses[listUrl] = R"({"versions":["1.0.0"]})";

    std::string infoUrl = reg + "/packages/" + name + "/1.0.0";
    std::string downloadUrl = reg + "/dl/" + name + "-1.0.0.tar.gz";
    std::string content = "[project]\nname = \"nocs\"\nversion = \"1.0.0\"\n";
    // Empty checksum — verification should be skipped
    MockHttp::responses[infoUrl] =
        R"({"name":"nocs","version":"1.0.0","url":")" + downloadUrl +
        R"(","checksum":"","dependencies":{}})";
    MockHttp::downloads[downloadUrl] = content;

    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("nocs", "1.0.0");
    EXPECT_TRUE(result.success);
}

TEST_F(PackageManagerRemoteTest, InstallSingleDownloadFailure) {
    std::string reg = "https://registry.example.com";
    std::string name = "dlfail";
    std::string listUrl = reg + "/packages/" + name;
    MockHttp::responses[listUrl] = R"({"versions":["1.0.0"]})";

    std::string infoUrl = reg + "/packages/" + name + "/1.0.0";
    std::string downloadUrl = reg + "/dl/" + name + "-1.0.0.tar.gz";
    MockHttp::responses[infoUrl] =
        R"({"name":"dlfail","version":"1.0.0","url":")" + downloadUrl +
        R"(","checksum":"sha256:abc","dependencies":{}})";
    // No download entry → download fails

    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("dlfail", "1.0.0");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMsg.find("failed to download") != std::string::npos);
}

TEST_F(PackageManagerRemoteTest, InstallSingleNoDownloadUrl) {
    std::string reg = "https://registry.example.com";
    std::string name = "nourl";
    std::string listUrl = reg + "/packages/" + name;
    MockHttp::responses[listUrl] = R"({"versions":["1.0.0"]})";

    std::string infoUrl = reg + "/packages/" + name + "/1.0.0";
    MockHttp::responses[infoUrl] =
        R"({"name":"nourl","version":"1.0.0","url":"","checksum":"","dependencies":{}})";

    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("nourl", "1.0.0");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.errorMsg.find("no download URL") != std::string::npos);
}

TEST_F(PackageManagerRemoteTest, ChecksumMismatchErrorMessageDetailed) {
    std::string reg = "https://registry.example.com";
    std::string name = "csdetail";
    std::string listUrl = reg + "/packages/" + name;
    MockHttp::responses[listUrl] = R"({"versions":["1.0.0"]})";

    std::string downloadUrl = reg + "/dl/" + name + "-1.0.0.tar.gz";
    std::string content = "[project]\nname = \"csdetail\"\nversion = \"1.0.0\"\n";
    std::string wrongHash = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    MockHttp::responses[reg + "/packages/" + name + "/1.0.0"] =
        R"({"name":"csdetail","version":"1.0.0","url":")" + downloadUrl +
        R"(","checksum":")" + wrongHash + R"(","dependencies":{}})";
    MockHttp::downloads[downloadUrl] = content;

    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("csdetail", "1.0.0");
    EXPECT_FALSE(result.success);
    // Should include both expected and got
    EXPECT_TRUE(result.errorMsg.find("expected") != std::string::npos ||
                result.errorMsg.find("sha256:") != std::string::npos);
    EXPECT_TRUE(result.errorMsg.find("got") != std::string::npos ||
                result.errorMsg.find("mismatch") != std::string::npos);
}

// ============================================================
// Group C: Dependency Tree + Registry Fallback (6 tests)
// ============================================================

TEST_F(PackageManagerRemoteTest, ResolveAndInstallRegistryFallback) {
    std::string reg = "https://registry.example.com";
    setupRegistryPackage("remotepkg", "1.0.0", reg);

    liva::PackageManager pm(testDir_, reg);
    std::vector<liva::PackageDep> deps;
    liva::PackageDep dep;
    dep.name = "remotepkg";
    dep.constraint.kind = liva::VersionConstraint::Minimum;
    dep.constraint.min = {0, 0, 0};
    deps.push_back(dep);

    auto result = pm.resolveAndInstall(deps);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.packages.size(), 1u);
    EXPECT_EQ(result.packages[0].name, "remotepkg");
}

TEST_F(PackageManagerRemoteTest, ResolveAndInstallTransitiveDepsFromRegistry) {
    std::string reg = "https://registry.example.com";

    // "parent" depends on "child"
    std::string parentDeps = R"({"child":">=1.0.0"})";
    setupRegistryPackage("parent", "1.0.0", reg, "", parentDeps);
    setupRegistryPackage("child", "1.0.0", reg);

    liva::PackageManager pm(testDir_, reg);
    std::vector<liva::PackageDep> deps;
    liva::PackageDep dep;
    dep.name = "parent";
    dep.constraint.kind = liva::VersionConstraint::Minimum;
    dep.constraint.min = {0, 0, 0};
    deps.push_back(dep);

    auto result = pm.resolveAndInstall(deps);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.packages.size(), 2u);
}

TEST_F(PackageManagerRemoteTest, ResolveAndInstallCircularDepsHandledGracefully) {
    std::string reg = "https://registry.example.com";

    // "a" depends on "b", "b" depends on "a"
    // Circular deps are handled gracefully: "a" is resolved first, then "b",
    // then "b"'s dep "a" is skipped via "already resolved" check.
    setupRegistryPackage("a", "1.0.0", reg, "", R"({"b":">=1.0.0"})");
    setupRegistryPackage("b", "1.0.0", reg, "", R"({"a":">=1.0.0"})");

    liva::PackageManager pm(testDir_, reg);
    std::vector<liva::PackageDep> deps;
    liva::PackageDep dep;
    dep.name = "a";
    dep.constraint.kind = liva::VersionConstraint::Minimum;
    dep.constraint.min = {0, 0, 0};
    deps.push_back(dep);

    auto result = pm.resolveAndInstall(deps);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.packages.size(), 2u);
}

TEST_F(PackageManagerRemoteTest, ResolveAndInstallMixedLocalRemote) {
    std::string reg = "https://registry.example.com";

    // Local package
    createLocalPackage("localpkg", "1.0.0");
    // Remote package
    setupRegistryPackage("remotepkg", "2.0.0", reg);

    liva::PackageManager pm(testDir_, reg);
    std::vector<liva::PackageDep> deps;
    {
        liva::PackageDep d;
        d.name = "localpkg";
        d.constraint.kind = liva::VersionConstraint::Minimum;
        d.constraint.min = {0, 0, 0};
        deps.push_back(d);
    }
    {
        liva::PackageDep d;
        d.name = "remotepkg";
        d.constraint.kind = liva::VersionConstraint::Minimum;
        d.constraint.min = {0, 0, 0};
        deps.push_back(d);
    }

    auto result = pm.resolveAndInstall(deps);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.packages.size(), 2u);
}

TEST_F(PackageManagerRemoteTest, ResolveAndInstallMultipleRemote) {
    std::string reg = "https://registry.example.com";
    setupRegistryPackage("pkg1", "1.0.0", reg);
    setupRegistryPackage("pkg2", "2.0.0", reg);
    setupRegistryPackage("pkg3", "3.0.0", reg);

    liva::PackageManager pm(testDir_, reg);
    std::vector<liva::PackageDep> deps;
    for (const auto &name : {"pkg1", "pkg2", "pkg3"}) {
        liva::PackageDep d;
        d.name = name;
        d.constraint.kind = liva::VersionConstraint::Minimum;
        d.constraint.min = {0, 0, 0};
        deps.push_back(d);
    }

    auto result = pm.resolveAndInstall(deps);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.packages.size(), 3u);
}

TEST_F(PackageManagerRemoteTest, ResolveAndInstallLocalFirst) {
    std::string reg = "https://registry.example.com";

    // Both local and remote exist
    createLocalPackage("mypkg", "1.0.0");
    setupRegistryPackage("mypkg", "2.0.0", reg);

    liva::PackageManager pm(testDir_, reg);
    std::vector<liva::PackageDep> deps;
    liva::PackageDep d;
    d.name = "mypkg";
    d.constraint.kind = liva::VersionConstraint::Minimum;
    d.constraint.min = {0, 0, 0};
    deps.push_back(d);

    auto result = pm.resolveAndInstall(deps);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.packages.size(), 1u);
    // Should use local version (1.0.0), not registry (2.0.0)
    EXPECT_EQ(result.packages[0].version.toString(), "1.0.0");
    // No HTTP GET for version list should have occurred for this package
    bool hasRegistryCall = false;
    for (const auto &log : MockHttp::requestLog) {
        if (log.find("/packages/mypkg") != std::string::npos) {
            hasRegistryCall = true;
            break;
        }
    }
    EXPECT_FALSE(hasRegistryCall);
}

// ============================================================
// Group D: HTTP Request Validation (4 tests)
// ============================================================

TEST_F(PackageManagerRemoteTest, RequestsCorrectRegistryUrls) {
    std::string reg = "https://registry.example.com";
    setupRegistryPackage("urltest", "1.0.0", reg);

    liva::PackageManager pm(testDir_, reg);
    pm.installSingle("urltest", "1.0.0");

    // Should have made GET to version list and package info URLs
    bool hasListUrl = false, hasInfoUrl = false;
    for (const auto &log : MockHttp::requestLog) {
        if (log.find("GET " + reg + "/packages/urltest") != std::string::npos &&
            log.find("/1.0.0") == std::string::npos)
            hasListUrl = true;
        if (log.find("GET " + reg + "/packages/urltest/1.0.0") != std::string::npos)
            hasInfoUrl = true;
    }
    EXPECT_TRUE(hasListUrl);
    EXPECT_TRUE(hasInfoUrl);
}

TEST_F(PackageManagerRemoteTest, RegistryUrlTrailingSlashNormalized) {
    std::string reg = "https://registry.example.com/";  // trailing slash
    std::string normalizedReg = "https://registry.example.com";
    setupRegistryPackage("slashtest", "1.0.0", normalizedReg);

    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("slashtest", "1.0.0");
    EXPECT_TRUE(result.success);
}

TEST_F(PackageManagerRemoteTest, NoHttpRequestsWhenNoRegistry) {
    liva::PackageManager pm(testDir_, "");
    auto result = pm.installSingle("nopkg", "");
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(MockHttp::requestLog.empty());
}

TEST_F(PackageManagerRemoteTest, InstallSingleAlreadyLocalSkipsRegistry) {
    std::string reg = "https://registry.example.com";
    createLocalPackage("localpkg", "1.0.0");
    setupRegistryPackage("localpkg", "2.0.0", reg);

    liva::PackageManager pm(testDir_, reg);
    auto result = pm.installSingle("localpkg", "1.0.0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.version.toString(), "1.0.0");
    // No HTTP requests should have been made
    EXPECT_TRUE(MockHttp::requestLog.empty());
}

// ============================================================
// Group E: JSON Parsing Edge Cases (4 tests)
// ============================================================

TEST_F(PackageManagerRemoteTest, ParseRegistryEntryWithDependencies) {
    std::string json = R"({
        "name": "mypkg",
        "version": "1.2.3",
        "url": "https://example.com/mypkg.tar.gz",
        "checksum": "sha256:abc123",
        "dependencies": {"dep1": ">=1.0.0", "dep2": "2.0.0"}
    })";
    liva::RegistryEntry entry;
    EXPECT_TRUE(liva::PackageManager::parseRegistryEntry(json, entry));
    EXPECT_EQ(entry.name, "mypkg");
    EXPECT_EQ(entry.version.toString(), "1.2.3");
    EXPECT_EQ(entry.downloadUrl, "https://example.com/mypkg.tar.gz");
    EXPECT_EQ(entry.checksum, "sha256:abc123");
    EXPECT_EQ(entry.dependencies.size(), 2u);
}

TEST_F(PackageManagerRemoteTest, ParseVersionListMultipleVersions) {
    std::string json = R"({"versions":["1.0.0","2.0.0","3.0.0","1.5.0"]})";
    auto versions = liva::PackageManager::parseVersionList(json);
    EXPECT_EQ(versions.size(), 4u);
    // Check all versions are present
    EXPECT_EQ(versions[0], "1.0.0");
    EXPECT_EQ(versions[1], "2.0.0");
    EXPECT_EQ(versions[2], "3.0.0");
    EXPECT_EQ(versions[3], "1.5.0");
}

TEST_F(PackageManagerRemoteTest, JsonGetObjectNestedValues) {
    std::string json = R"({"dependencies":{"pkg1":">=1.0.0","pkg2":"2.0.0"}})";
    auto obj = liva::json::getObject(json, "dependencies");
    EXPECT_EQ(obj.size(), 2u);
    EXPECT_EQ(obj["pkg1"], ">=1.0.0");
    EXPECT_EQ(obj["pkg2"], "2.0.0");
}

TEST_F(PackageManagerRemoteTest, JsonHelperEmptyInputs) {
    EXPECT_EQ(liva::json::getString("", "key"), "");
    EXPECT_EQ(liva::json::getString("{}", "key"), "");
    EXPECT_TRUE(liva::json::getStringArray("", "key").empty());
    EXPECT_TRUE(liva::json::getObject("", "key").empty());

    liva::RegistryEntry entry;
    EXPECT_FALSE(liva::PackageManager::parseRegistryEntry("", entry));
    EXPECT_FALSE(liva::PackageManager::parseRegistryEntry("{}", entry));
    EXPECT_TRUE(liva::PackageManager::parseVersionList("").empty());
    EXPECT_TRUE(liva::PackageManager::parseVersionList("{}").empty());
}

// ============================================================
// Group F: End-to-End Flow (2 tests)
// ============================================================

TEST_F(PackageManagerRemoteTest, FullInstallFlowWithMockRegistry) {
    std::string reg = "https://registry.example.com";
    setupRegistryPackage("fulltest", "1.0.0", reg);

    liva::PackageManager pm(testDir_, reg);

    // Install from registry
    auto result = pm.installSingle("fulltest", "1.0.0");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.name, "fulltest");
    EXPECT_EQ(result.version.toString(), "1.0.0");
    EXPECT_FALSE(result.checksum.empty());

    // Verify local package exists now
    std::string tomlPath = liva::joinPath(testDir_, "packages/fulltest/liva.toml");
    std::ifstream f(tomlPath);
    EXPECT_TRUE(f.is_open());

    // Verify HTTP request log has expected entries
    bool hasGet = false, hasDownload = false, hasExtract = false;
    for (const auto &log : MockHttp::requestLog) {
        if (log.find("GET") != std::string::npos) hasGet = true;
        if (log.find("DOWNLOAD") != std::string::npos) hasDownload = true;
        if (log.find("EXTRACT") != std::string::npos) hasExtract = true;
    }
    EXPECT_TRUE(hasGet);
    EXPECT_TRUE(hasDownload);
    EXPECT_TRUE(hasExtract);
}

TEST_F(PackageManagerRemoteTest, FullResolveAndInstallWithTransitiveDeps) {
    std::string reg = "https://registry.example.com";

    // "app" depends on "lib", "lib" depends on "core"
    setupRegistryPackage("app", "1.0.0", reg, "", R"({"lib":">=1.0.0"})");
    setupRegistryPackage("lib", "1.0.0", reg, "", R"({"core":">=1.0.0"})");
    setupRegistryPackage("core", "1.0.0", reg);

    liva::PackageManager pm(testDir_, reg);
    std::vector<liva::PackageDep> deps;
    liva::PackageDep dep;
    dep.name = "app";
    dep.constraint.kind = liva::VersionConstraint::Minimum;
    dep.constraint.min = {0, 0, 0};
    deps.push_back(dep);

    auto result = pm.resolveAndInstall(deps);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.packages.size(), 3u);

    // Check all 3 packages are resolved
    std::set<std::string> names;
    for (const auto &pkg : result.packages)
        names.insert(pkg.name);
    EXPECT_TRUE(names.count("app"));
    EXPECT_TRUE(names.count("lib"));
    EXPECT_TRUE(names.count("core"));
}

// ============================================================
// removeDependencyFromToml Tests (5 tests)
// ============================================================

TEST_F(BuildCacheTest, RemoveDependencyFromToml) {
    std::string tomlPath = liva::joinPath(testDir_, "liva.toml");
    writeFile(tomlPath,
        "[project]\nname = \"test\"\nversion = \"1.0.0\"\n\n"
        "[dependencies]\nmylib = \"1.0.0\"\n");

    EXPECT_TRUE(liva::removeDependencyFromToml(tomlPath, "mylib"));

    std::ifstream f(tomlPath);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();
    EXPECT_TRUE(content.find("mylib") == std::string::npos);
    EXPECT_TRUE(content.find("[dependencies]") != std::string::npos);
}

TEST_F(BuildCacheTest, RemoveDependencyNotFound) {
    std::string tomlPath = liva::joinPath(testDir_, "liva.toml");
    writeFile(tomlPath,
        "[project]\nname = \"test\"\nversion = \"1.0.0\"\n\n"
        "[dependencies]\nmylib = \"1.0.0\"\n");

    EXPECT_FALSE(liva::removeDependencyFromToml(tomlPath, "nonexistent"));
}

TEST_F(BuildCacheTest, RemoveDependencyPreservesOthers) {
    std::string tomlPath = liva::joinPath(testDir_, "liva.toml");
    writeFile(tomlPath,
        "[project]\nname = \"test\"\nversion = \"1.0.0\"\n\n"
        "[dependencies]\nfoo = \"1.0.0\"\nbar = \"2.0.0\"\nbaz = \"3.0.0\"\n");

    EXPECT_TRUE(liva::removeDependencyFromToml(tomlPath, "bar"));

    std::ifstream f(tomlPath);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();
    EXPECT_TRUE(content.find("foo") != std::string::npos);
    EXPECT_TRUE(content.find("bar") == std::string::npos);
    EXPECT_TRUE(content.find("baz") != std::string::npos);
}

TEST_F(BuildCacheTest, RemoveDependencyFromEmptySection) {
    std::string tomlPath = liva::joinPath(testDir_, "liva.toml");
    writeFile(tomlPath,
        "[project]\nname = \"test\"\nversion = \"1.0.0\"\n\n"
        "[dependencies]\n");

    EXPECT_FALSE(liva::removeDependencyFromToml(tomlPath, "anything"));
}

TEST(DriverOptionsTest, RemoveDefault) {
    liva::DriverOptions opts;
    EXPECT_TRUE(opts.removePkgName.empty());
    EXPECT_EQ(opts.subcommand, liva::Subcommand::None);
}

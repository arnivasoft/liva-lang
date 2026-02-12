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

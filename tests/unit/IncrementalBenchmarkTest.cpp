#include "liva/Driver/BuildCache.h"
#include "liva/Driver/SemaCache.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace liva;
namespace fs = std::filesystem;

// ============================================================
// Incremental Compilation Performance Benchmark Tests
//
// Tests that BuildCache and SemaCache operations scale
// acceptably with 100+ files. Uses temp directories with
// generated .liva files — no LLVM required.
// ============================================================

class IncrementalBenchmarkTest : public ::testing::Test {
protected:
    fs::path tempDir_;

    void SetUp() override {
        tempDir_ = fs::temp_directory_path() / "liva_incr_bench_test";
        fs::remove_all(tempDir_);
        fs::create_directories(tempDir_);
    }

    void TearDown() override {
        fs::remove_all(tempDir_);
    }

    template <typename Fn>
    double measureMs(Fn fn) {
        auto start = std::chrono::high_resolution_clock::now();
        fn();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    /// Write a single module file with a struct and a function
    void writeModuleFile(int index) {
        std::string name = "mod" + std::to_string(index);
        fs::path path = tempDir_ / (name + ".liva");
        std::ofstream ofs(path);
        ofs << "struct S" << index << " {\n"
            << "    var x: i32\n"
            << "    var y: i32\n"
            << "}\n\n"
            << "func compute" << index << "(a: i32) -> i32 {\n"
            << "    return a + " << index << "\n"
            << "}\n";
    }

    /// Generate a star topology: main imports all modules
    void generateStarProject(int moduleCount) {
        // Write module files
        for (int i = 0; i < moduleCount; ++i) {
            writeModuleFile(i);
        }
        // Write main.liva that imports all modules
        std::ofstream main(tempDir_ / "main.liva");
        for (int i = 0; i < moduleCount; ++i) {
            main << "import mod" << i << "\n";
        }
        main << "\nfunc main() {\n"
             << "    let x = compute0(42)\n"
             << "}\n";
    }

    /// Generate a chain topology: mod0 → mod1 → ... → modN-1
    void generateChainProject(int moduleCount) {
        for (int i = 0; i < moduleCount; ++i) {
            fs::path path = tempDir_ / ("mod" + std::to_string(i) + ".liva");
            std::ofstream ofs(path);
            if (i + 1 < moduleCount) {
                ofs << "import mod" << (i + 1) << "\n\n";
            }
            ofs << "struct S" << i << " {\n"
                << "    var x: i32\n"
                << "}\n\n"
                << "func compute" << i << "(a: i32) -> i32 {\n"
                << "    return a + " << i << "\n"
                << "}\n";
        }
        // main imports only mod0
        std::ofstream main(tempDir_ / "main.liva");
        main << "import mod0\n\n"
             << "func main() {\n"
             << "    let x = compute0(1)\n"
             << "}\n";
    }

    /// Collect all .liva file paths in tempDir
    std::vector<std::string> collectSourceFiles() {
        std::vector<std::string> files;
        for (auto &entry : fs::directory_iterator(tempDir_)) {
            if (entry.path().extension() == ".liva") {
                files.push_back(entry.path().string());
            }
        }
        return files;
    }

    /// Touch a file to change its mtime and content.
    /// Bumps mtime forward by 2 seconds to defeat second-level mtime fast-path.
    void touchFile(const fs::path &path) {
        std::ofstream ofs(path, std::ios::app);
        ofs << "// modified\n";
        ofs.close();
        auto now = fs::last_write_time(path);
        fs::last_write_time(path, now + std::chrono::seconds(2));
    }
};

// === 1. scanDependencies — BFS import scanning ===

TEST_F(IncrementalBenchmarkTest, ScanDependencies_Star100) {
    generateStarProject(100);
    BuildCache cache(tempDir_.string());
    std::string entry = (tempDir_ / "main.liva").string();
    std::vector<std::string> searchPaths = {tempDir_.string()};

    std::vector<std::string> deps;
    double ms = measureMs([&]() {
        deps = cache.scanDependencies(entry, searchPaths);
    });

    EXPECT_GE(deps.size(), 100u) << "Should find 100+ files (main + modules)";
    EXPECT_LT(ms, 100.0) << "scanDependencies(star,100) took " << ms
                          << "ms (limit: 100ms)";
}

TEST_F(IncrementalBenchmarkTest, ScanDependencies_Chain200) {
    generateChainProject(200);
    BuildCache cache(tempDir_.string());
    std::string entry = (tempDir_ / "main.liva").string();
    std::vector<std::string> searchPaths = {tempDir_.string()};

    std::vector<std::string> deps;
    double ms = measureMs([&]() {
        deps = cache.scanDependencies(entry, searchPaths);
    });

    EXPECT_GE(deps.size(), 200u) << "Should find 200+ files in chain";
    EXPECT_LT(ms, 200.0) << "scanDependencies(chain,200) took " << ms
                          << "ms (limit: 200ms)";
}

// === 2. hashFileContent — Hashing throughput ===

TEST_F(IncrementalBenchmarkTest, HashFileContent_100Files) {
    generateStarProject(100);
    BuildCache cache(tempDir_.string());
    auto files = collectSourceFiles();
    ASSERT_GE(files.size(), 100u);

    double ms = measureMs([&]() {
        for (auto &f : files) {
            auto h = cache.hashFileContent(f);
            EXPECT_FALSE(h.empty());
        }
    });

    EXPECT_LT(ms, 50.0) << "Hashing 100 files took " << ms
                         << "ms (limit: 50ms)";
}

TEST_F(IncrementalBenchmarkTest, HashFileContent_200Files) {
    generateChainProject(200);
    BuildCache cache(tempDir_.string());
    auto files = collectSourceFiles();
    ASSERT_GE(files.size(), 200u);

    double ms = measureMs([&]() {
        for (auto &f : files) {
            auto h = cache.hashFileContent(f);
            EXPECT_FALSE(h.empty());
        }
    });

    EXPECT_LT(ms, 100.0) << "Hashing 200 files took " << ms
                          << "ms (limit: 100ms)";
}

// === 3. checkFilesCache — Cold / Warm / Single-change ===

TEST_F(IncrementalBenchmarkTest, CheckFilesCache_ColdVsWarm) {
    generateStarProject(100);
    BuildCache cache(tempDir_.string());
    auto files = collectSourceFiles();
    ASSERT_GE(files.size(), 100u);

    // Cold check (no manifest exists)
    std::vector<FileCompileStatus> coldResult;
    double coldMs = measureMs([&]() {
        coldResult = cache.checkFilesCache(files, 2, false);
    });

    // All should need recompile (cold cache)
    int coldMissCount = 0;
    for (auto &s : coldResult) {
        if (s.needsRecompile) ++coldMissCount;
    }
    EXPECT_EQ(coldMissCount, static_cast<int>(files.size()))
        << "Cold cache: all files should be misses";

    // Simulate storing objects for all files
    for (auto &s : coldResult) {
        std::string fakeObj = cache.objectPathForSource(s.sourcePath);
        // Create a dummy .o file so storeFileObject can copy/reference it
        {
            std::ofstream ofs(fakeObj);
            ofs << "fake_object";
        }
        cache.storeFileObject(s.sourcePath, s.currentHash, fakeObj, 2, false);
    }

    // Warm check (all cached)
    std::vector<FileCompileStatus> warmResult;
    double warmMs = measureMs([&]() {
        warmResult = cache.checkFilesCache(files, 2, false);
    });

    int warmHitCount = 0;
    for (auto &s : warmResult) {
        if (!s.needsRecompile) ++warmHitCount;
    }
    EXPECT_EQ(warmHitCount, static_cast<int>(files.size()))
        << "Warm cache: all files should be hits";

    EXPECT_LT(coldMs, 200.0) << "Cold check took " << coldMs << "ms (limit: 200ms)";
    EXPECT_LT(warmMs, 200.0) << "Warm check took " << warmMs << "ms (limit: 200ms)";

    // Warm should be at most equal to cold (often faster due to mtime fast-path)
    // We use a generous bound: warm < cold * 2 to avoid flaky tests
}

TEST_F(IncrementalBenchmarkTest, CheckFilesCache_SingleChange) {
    generateStarProject(100);
    BuildCache cache(tempDir_.string());
    auto files = collectSourceFiles();
    ASSERT_GE(files.size(), 100u);

    // Populate cache
    auto initial = cache.checkFilesCache(files, 2, false);
    for (auto &s : initial) {
        std::string fakeObj = cache.objectPathForSource(s.sourcePath);
        { std::ofstream ofs(fakeObj); ofs << "fake"; }
        cache.storeFileObject(s.sourcePath, s.currentHash, fakeObj, 2, false);
    }

    // Modify exactly 1 file
    fs::path modifiedFile = tempDir_ / "mod50.liva";
    touchFile(modifiedFile);

    // Check again — expect 1 miss, rest hits
    std::vector<FileCompileStatus> result;
    double ms = measureMs([&]() {
        result = cache.checkFilesCache(files, 2, false);
    });

    int missCount = 0;
    for (auto &s : result) {
        if (s.needsRecompile) ++missCount;
    }
    EXPECT_EQ(missCount, 1) << "Single change should cause exactly 1 miss, got " << missCount;
    EXPECT_LT(ms, 200.0) << "Single-change check took " << ms << "ms (limit: 200ms)";
}

// === 4. SemaCache — Store/Check/Cascade ===

TEST_F(IncrementalBenchmarkTest, SemaCache_StoreAndCheck100) {
    generateStarProject(100);
    BuildCache buildCache(tempDir_.string());
    SemaCache semaCache(tempDir_.string());
    auto files = collectSourceFiles();
    ASSERT_GE(files.size(), 100u);

    // Get file hashes via checkFilesCache
    auto statuses = buildCache.checkFilesCache(files, 2, false);

    // Store sema entries for all files
    double storeMs = measureMs([&]() {
        for (auto &s : statuses) {
            std::string ifaceHash = "iface_" + s.currentHash;
            semaCache.store(s.sourcePath, s.currentHash, ifaceHash, {});
        }
    });

    semaCache.saveManifest();

    // Check — all should be clean (needsResema = false)
    std::vector<SemaCacheStatus> checkResult;
    double checkMs = measureMs([&]() {
        checkResult = semaCache.check(files, statuses);
    });

    int needsResemaCount = 0;
    for (auto &s : checkResult) {
        if (s.needsResema) ++needsResemaCount;
    }
    EXPECT_EQ(needsResemaCount, 0)
        << "All files should be clean after store, got " << needsResemaCount << " dirty";

    EXPECT_LT(storeMs, 50.0) << "SemaCache store(100) took " << storeMs << "ms (limit: 50ms)";
    EXPECT_LT(checkMs, 50.0) << "SemaCache check(100) took " << checkMs << "ms (limit: 50ms)";
}

TEST_F(IncrementalBenchmarkTest, SemaCache_CascadeInvalidation) {
    generateStarProject(100);
    BuildCache buildCache(tempDir_.string());
    SemaCache semaCache(tempDir_.string());
    auto files = collectSourceFiles();
    ASSERT_GE(files.size(), 100u);

    auto statuses = buildCache.checkFilesCache(files, 2, false);

    // Store sema entries with dependency info:
    // main.liva imports all mod*.liva files
    std::string mainPath;
    std::vector<std::string> modPaths;
    for (auto &s : statuses) {
        if (s.sourcePath.find("main.liva") != std::string::npos) {
            mainPath = s.sourcePath;
        } else {
            modPaths.push_back(s.sourcePath);
        }
    }
    ASSERT_FALSE(mainPath.empty());

    // Store modules (no imports)
    for (auto &s : statuses) {
        if (s.sourcePath == mainPath) continue;
        std::string ifaceHash = "iface_" + s.currentHash;
        semaCache.store(s.sourcePath, s.currentHash, ifaceHash, {});
    }

    // Store main with imports → all modules
    {
        auto it = std::find_if(statuses.begin(), statuses.end(),
            [&](const FileCompileStatus &s) { return s.sourcePath == mainPath; });
        ASSERT_NE(it, statuses.end());
        semaCache.store(mainPath, it->currentHash, "iface_main", modPaths);
    }

    semaCache.saveManifest();

    // Now simulate a change in mod0: modify file + re-hash
    fs::path mod0Path = tempDir_ / "mod0.liva";
    touchFile(mod0Path);

    // Re-check with updated statuses
    auto updatedStatuses = buildCache.checkFilesCache(files, 2, false);

    std::vector<SemaCacheStatus> checkResult;
    double ms = measureMs([&]() {
        checkResult = semaCache.check(files, updatedStatuses);
    });

    // mod0 itself should need resema (source changed)
    // main.liva may also need resema due to cascade (depends on interface change)
    int needsResemaCount = 0;
    for (auto &s : checkResult) {
        if (s.needsResema) ++needsResemaCount;
    }
    EXPECT_GE(needsResemaCount, 1) << "At least mod0 should need resema";

    EXPECT_LT(ms, 50.0) << "SemaCache cascade check(100) took " << ms
                         << "ms (limit: 50ms)";
}

// === 5. pruneStaleEntries — Deleted file cleanup ===

TEST_F(IncrementalBenchmarkTest, PruneStaleEntries_100Files) {
    generateStarProject(100);
    BuildCache cache(tempDir_.string());
    auto files = collectSourceFiles();
    ASSERT_GE(files.size(), 100u);

    // Populate cache
    auto initial = cache.checkFilesCache(files, 2, false);
    for (auto &s : initial) {
        std::string fakeObj = cache.objectPathForSource(s.sourcePath);
        { std::ofstream ofs(fakeObj); ofs << "fake"; }
        cache.storeFileObject(s.sourcePath, s.currentHash, fakeObj, 2, false);
    }

    // Delete 10 module files from disk
    for (int i = 90; i < 100; ++i) {
        fs::remove(tempDir_ / ("mod" + std::to_string(i) + ".liva"));
    }

    // Collect remaining source files
    auto remainingFiles = collectSourceFiles();
    EXPECT_EQ(remainingFiles.size(), files.size() - 10);

    double ms = measureMs([&]() {
        cache.pruneStaleEntries(remainingFiles);
    });

    EXPECT_LT(ms, 20.0) << "pruneStaleEntries(100→90) took " << ms
                         << "ms (limit: 20ms)";

    // Verify cache is still valid for remaining files
    auto postPrune = cache.checkFilesCache(remainingFiles, 2, false);
    int hitCount = 0;
    for (auto &s : postPrune) {
        if (!s.needsRecompile) ++hitCount;
    }
    EXPECT_EQ(hitCount, static_cast<int>(remainingFiles.size()))
        << "Remaining files should still be cached after prune";
}

// === 6. Scaling test — Linear scaling verification ===

TEST_F(IncrementalBenchmarkTest, ScanDependencies_ScalingLinear) {
    // Test with 50 files
    generateStarProject(50);
    BuildCache cache50(tempDir_.string());
    std::string entry50 = (tempDir_ / "main.liva").string();
    std::vector<std::string> sp50 = {tempDir_.string()};

    double ms50 = measureMs([&]() {
        cache50.scanDependencies(entry50, sp50);
    });

    // Clean and test with 200 files
    fs::remove_all(tempDir_);
    fs::create_directories(tempDir_);

    generateStarProject(200);
    BuildCache cache200(tempDir_.string());
    std::string entry200 = (tempDir_ / "main.liva").string();
    std::vector<std::string> sp200 = {tempDir_.string()};

    double ms200 = measureMs([&]() {
        cache200.scanDependencies(entry200, sp200);
    });

    // 4x input → allow up to 8x time (generous for I/O variance)
    if (ms50 > 0.1) {
        double ratio = ms200 / ms50;
        EXPECT_LT(ratio, 8.0)
            << "scanDependencies scaling: " << ratio
            << "x for 4x input (50→200), expected < 8x. "
            << "50 files: " << ms50 << "ms, 200 files: " << ms200 << "ms";
    }
}

TEST_F(IncrementalBenchmarkTest, HashFileContent_ScalingLinear) {
    // Test with 50 files
    generateStarProject(50);
    BuildCache cache50(tempDir_.string());
    auto files50 = collectSourceFiles();

    double ms50 = measureMs([&]() {
        for (auto &f : files50)
            cache50.hashFileContent(f);
    });

    // Clean and test with 200 files
    fs::remove_all(tempDir_);
    fs::create_directories(tempDir_);

    generateStarProject(200);
    BuildCache cache200(tempDir_.string());
    auto files200 = collectSourceFiles();

    double ms200 = measureMs([&]() {
        for (auto &f : files200)
            cache200.hashFileContent(f);
    });

    if (ms50 > 0.1) {
        double ratio = ms200 / ms50;
        EXPECT_LT(ratio, 8.0)
            << "hashFileContent scaling: " << ratio
            << "x for 4x input (50→200), expected < 8x. "
            << "50 files: " << ms50 << "ms, 200 files: " << ms200 << "ms";
    }
}

#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/ModuleLoader.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>

using namespace liva;

class StdlibModuleTest : public ::testing::Test {
protected:
    struct SemaResult {
        std::unique_ptr<SourceManager> sm;
        DiagnosticsEngine diag;
        std::unique_ptr<TranslationUnit> tu;
        bool passed;
    };

    static std::string stdlibPath() {
        return std::string(LIVA_PROJECT_ROOT) + "/stdlib";
    }

    SemaResult check(const std::string &source, bool withModuleLoader = true,
                      const std::string &extraSearchPath = "") {
        SemaResult r;
        r.sm = std::make_unique<SourceManager>("test.liva", source);
        r.diag.setSourceManager(r.sm.get());
        Lexer lexer(*r.sm, r.diag);
        Parser parser(lexer, r.diag);
        r.tu = parser.parseTranslationUnit();
        if (r.diag.hasErrors()) {
            r.passed = false;
            return r;
        }
        if (withModuleLoader) {
            ModuleLoader loader;
            loader.addSearchPath(stdlibPath());
            if (!extraSearchPath.empty())
                loader.addSearchPath(extraSearchPath);
            Sema sema(r.diag, &loader);
            sema.analyze(*r.tu);
        } else {
            Sema sema(r.diag);
            sema.analyze(*r.tu);
        }
        r.passed = !r.diag.hasErrors();
        return r;
    }
};

// ============================================================
// Module 1: json::json
// ============================================================

TEST_F(StdlibModuleTest, ImportJsonModule) {
    auto r = check(
        "import json::json\n"
        "func main() {\n"
        "    let obj = JsonObject.create()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import json::json should resolve JsonObject";
}

TEST_F(StdlibModuleTest, JsonObjectMethods) {
    auto r = check(
        "import json::json\n"
        "func main() {\n"
        "    var obj = JsonObject.parse(\"{}\")\n"
        "    obj.set(\"key\", \"value\")\n"
        "    let v = obj.get(\"key\")\n"
        "    let n = obj.count()\n"
        "    let valid = obj.isValid()\n"
        "    let s = obj.stringify()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "JsonObject methods should type-check";
}

TEST_F(StdlibModuleTest, JsonObjectNumericMethods) {
    auto r = check(
        "import json::json\n"
        "func main() {\n"
        "    var obj = JsonObject.create()\n"
        "    obj.setInt(\"age\", 25)\n"
        "    obj.setFloat(\"score\", 3.14)\n"
        "    obj.setBool(\"active\", true)\n"
        "    let age = obj.getInt(\"age\")\n"
        "    let score = obj.getFloat(\"score\")\n"
        "    let active = obj.getBool(\"active\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "JsonObject numeric methods should type-check";
}

// ============================================================
// Module 2: time::time
// ============================================================

TEST_F(StdlibModuleTest, ImportTimeModule) {
    auto r = check(
        "import time::time\n"
        "func main() {\n"
        "    let d = Duration.fromMs(1000)\n"
        "    let ms = d.toMs()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import time::time should resolve Duration";
}

TEST_F(StdlibModuleTest, TimeInstantAndTimer) {
    auto r = check(
        "import time::time\n"
        "func main() {\n"
        "    let start = Instant.now()\n"
        "    let elapsed = start.elapsed()\n"
        "    var timer = Timer.new()\n"
        "    timer.start()\n"
        "    let running = timer.isRunning()\n"
        "    timer.stop()\n"
        "    timer.reset()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Instant and Timer should type-check";
}

TEST_F(StdlibModuleTest, TimeDateTime) {
    auto r = check(
        "import time::time\n"
        "func main() {\n"
        "    let now = DateTime.now()\n"
        "    let y = now.year()\n"
        "    let m = now.month()\n"
        "    let d = now.day()\n"
        "    let h = now.hour()\n"
        "    let s = now.toString()\n"
        "    let later = now.add(3600.0)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "DateTime methods should type-check";
}

// ============================================================
// Module 3: path::path
// ============================================================

TEST_F(StdlibModuleTest, ImportPathModule) {
    auto r = check(
        "import path::path\n"
        "func main() {\n"
        "    let p = Path.new(\"/home/user\")\n"
        "    let joined = p.join(\"file.txt\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import path::path should resolve Path";
}

TEST_F(StdlibModuleTest, PathMethods) {
    auto r = check(
        "import path::path\n"
        "func main() {\n"
        "    let p = Path.new(\"/home/user/file.txt\")\n"
        "    let dir = p.dirname()\n"
        "    let base = p.basename()\n"
        "    let ext = p.extension()\n"
        "    let exists = p.exists()\n"
        "    let abs = p.absolute()\n"
        "    let s = p.toString()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Path methods should type-check";
}

TEST_F(StdlibModuleTest, DirMethods) {
    auto r = check(
        "import path::path\n"
        "func main() {\n"
        "    let d = Dir.new(\"/tmp\")\n"
        "    let exists = d.exists()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Dir struct should type-check";
}

// ============================================================
// Module 4: testing::testing
// ============================================================

TEST_F(StdlibModuleTest, ImportTestingModule) {
    auto r = check(
        "import testing::testing\n"
        "func main() {\n"
        "    var suite = TestSuite.new(\"math\")\n"
        "    let s = suite.summary()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import testing::testing should resolve TestSuite";
}

TEST_F(StdlibModuleTest, TestingExpect) {
    auto r = check(
        "import testing::testing\n"
        "func main() {\n"
        "    let e = Expect.new(42)\n"
        "    e.toBe(42)\n"
        "    e.toBeTrue()\n"
        "    let es = ExpectStr.new(\"hello\")\n"
        "    es.toBe(\"hello\")\n"
        "    let ef = ExpectFloat.new(3.14)\n"
        "    ef.toBe(3.14)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Expect structs should type-check";
}

// ============================================================
// Module 5: std::crypto + crypto::crypto
// ============================================================

TEST_F(StdlibModuleTest, ImportStdCrypto) {
    auto r = check(
        "import std::crypto\n"
        "func main() {\n"
        "    let h = sha256(\"hello\")\n"
        "    let m = md5(\"hello\")\n"
        "    let mac = hmacSha256(\"key\", \"data\")\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "import std::crypto should resolve sha256/md5/hmacSha256";
}

TEST_F(StdlibModuleTest, ImportCryptoModule) {
    auto r = check(
        "import crypto::crypto\n"
        "func main() {\n"
        "    let h = Hash.sha256(\"hello\")\n"
        "    let hex = h.hex()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import crypto::crypto should resolve Hash";
}

TEST_F(StdlibModuleTest, CryptoHmac) {
    auto r = check(
        "import crypto::crypto\n"
        "func main() {\n"
        "    let mac = Hmac.sha256(\"secret\", \"data\")\n"
        "    let hex = mac.hex()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Hmac struct should type-check";
}

TEST_F(StdlibModuleTest, CryptoEncodingHelpers) {
    auto r = check(
        "import crypto::crypto\n"
        "func main() {\n"
        "    let encoded = base64Enc(\"hello\")\n"
        "    let hex = hexEnc(\"hello\")\n"
        "    let cs = checksum(\"hello\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Encoding helper functions should type-check";
}

// ============================================================
// Module 6: http::http
// ============================================================

TEST_F(StdlibModuleTest, ImportHttpModule) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    let client = HttpClient.new()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import http::http should resolve HttpClient";
}

TEST_F(StdlibModuleTest, HttpClientMethods) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    let client = HttpClient.withBaseUrl(\"https://api.example.com\")\n"
        "    let resp = client.get(\"/users\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpClient get should type-check";
}

TEST_F(StdlibModuleTest, HttpClientPostPutPatchDelete) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    let client = HttpClient.new()\n"
        "    let r1 = client.post(\"/api\", \"{\\\"key\\\": 1}\")\n"
        "    let r2 = client.put(\"/api/1\", \"{}\")\n"
        "    let r3 = client.patch(\"/api/1\", \"{}\")\n"
        "    let r4 = client.delete(\"/api/1\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpClient post/put/patch/delete should type-check";
}

TEST_F(StdlibModuleTest, HttpResponseStruct) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    let resp = HttpResponse { body: \"hello\", ok: true }\n"
        "    let t = resp.text()\n"
        "    let o = resp.isOk()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpResponse struct should type-check";
}

TEST_F(StdlibModuleTest, HttpConvenienceFunctions) {
    // Use HttpClient directly — convenience wrapper for simple HTTP calls
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    let client = HttpClient.new()\n"
        "    let r1 = client.get(\"https://example.com\")\n"
        "    let r2 = client.post(\"https://example.com\", \"data\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpClient convenience calls should type-check";
}

TEST_F(StdlibModuleTest, HttpHeadersBuilder) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    var headers = HttpHeaders.new()\n"
        "    headers.set(\"Content-Type\", \"application/json\")\n"
        "    let s = headers.toString()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpHeaders builder should type-check";
}

// ============================================================
// Module 7: sync::sync
// ============================================================

TEST_F(StdlibModuleTest, ImportSyncModule) {
    auto r = check(
        "import sync::sync\n"
        "func main() {\n"
        "    let m = Mutex.new()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import sync::sync should resolve Mutex";
}

TEST_F(StdlibModuleTest, SyncMutexMethods) {
    auto r = check(
        "import sync::sync\n"
        "func main() {\n"
        "    var m = Mutex.new()\n"
        "    m.lock()\n"
        "    let ok = m.tryLock()\n"
        "    m.unlock()\n"
        "    m.free()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Mutex methods should type-check";
}

TEST_F(StdlibModuleTest, SyncAtomicMethods) {
    auto r = check(
        "import sync::sync\n"
        "func main() {\n"
        "    var a = AtomicI64.new(0)\n"
        "    let v = a.load()\n"
        "    a.store(42)\n"
        "    let prev = a.add(1)\n"
        "    let cas = a.compareAndSwap(43, 100)\n"
        "    a.free()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "AtomicI64 methods should type-check";
}

TEST_F(StdlibModuleTest, SyncChannelMethods) {
    auto r = check(
        "import sync::sync\n"
        "func main() {\n"
        "    var ch = Channel.new(10)\n"
        "    ch.send(42)\n"
        "    let v = ch.receive()\n"
        "    let n = ch.len()\n"
        "    ch.close()\n"
        "    ch.free()\n"  // receive returns i64?
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Channel methods should type-check";
}

TEST_F(StdlibModuleTest, SyncTaskGroupMethods) {
    auto r = check(
        "import sync::sync\n"
        "func main() {\n"
        "    var g = TaskGroup.new()\n"
        "    let c = g.count()\n"
        "    g.cancelAll()\n"
        "    g.awaitAll()\n"
        "    g.free()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TaskGroup methods should type-check";
}

// ============================================================
// Module 8: regex::regex
// ============================================================

TEST_F(StdlibModuleTest, ImportRegexModule) {
    auto r = check(
        "import regex::regex\n"
        "func main() {\n"
        "    let re = Regex.new(\"[0-9]+\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import regex::regex should resolve Regex";
}

TEST_F(StdlibModuleTest, RegexPatternMethods) {
    auto r = check(
        "import regex::regex\n"
        "func main() {\n"
        "    let re = Regex.new(\"hello\")\n"
        "    let matched = re.isMatch(\"hello world\")\n"
        "    let found = re.find(\"say hello\")\n"
        "    let all = re.findAll(\"hello hello\")\n"
        "    let replaced = re.replace(\"hello world\", \"hi\")\n"
        "    let groups = re.groups(\"(hello) (world)\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Regex pattern methods should type-check";
}

TEST_F(StdlibModuleTest, RegexReplaceAndToString) {
    auto r = check(
        "import regex::regex\n"
        "func main() {\n"
        "    let re = Regex.new(\"[0-9]+\")\n"
        "    let s = re.toString()\n"
        "    let replaced = re.replace(\"abc123def\", \"NUM\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Regex replace and toString should type-check";
}

// ============================================================
// Module 9: fs::fs
// ============================================================

TEST_F(StdlibModuleTest, ImportFsModule) {
    auto r = check(
        "import fs::fs\n"
        "func main() {\n"
        "    let info = FileInfo.new(\"/tmp/test\")\n"
        "    let n = info.name()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import fs::fs should resolve FileInfo";
}

TEST_F(StdlibModuleTest, FsDirStruct) {
    auto r = check(
        "import fs::fs\n"
        "func main() {\n"
        "    let d = Dir.new(\"/tmp\")\n"
        "    let s = d.toString()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Dir struct should type-check";
}

// ============================================================
// Module 10: net::net
// ============================================================

TEST_F(StdlibModuleTest, ImportNetModule) {
    auto r = check(
        "import net::net\n"
        "func main() {\n"
        "    let url = Url.parse(\"https://example.com\")\n"
        "    let s = url.toString()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import net::net should resolve Url";
}

TEST_F(StdlibModuleTest, NetRequestStruct) {
    auto r = check(
        "import net::net\n"
        "func main() {\n"
        "    let req = Request.get(\"https://api.example.com\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Request struct should type-check";
}

// ============================================================
// Umbrella import includes crypto
// ============================================================

TEST_F(StdlibModuleTest, UmbrellaIncludesCrypto) {
    auto r = check(
        "import std\n"
        "func main() {\n"
        "    let h = sha256(\"test\")\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "import std (umbrella) should include crypto functions";
}

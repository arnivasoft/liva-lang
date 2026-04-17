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

TEST_F(StdlibModuleTest, NetRequestAllMethods) {
    auto r = check(
        "import net::net\n"
        "func main() {\n"
        "    let r1 = Request.get(\"https://api.example.com\")\n"
        "    let r2 = Request.post(\"https://api.example.com\", \"data\")\n"
        "    let r3 = Request.put(\"https://api.example.com/1\", \"{}\")\n"
        "    let r4 = Request.patch(\"https://api.example.com/1\", \"{}\")\n"
        "    let r5 = Request.delete(\"https://api.example.com/1\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Request put/patch/delete should type-check";
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

// ============================================================
// Module 11: random::random
// ============================================================

TEST_F(StdlibModuleTest, ImportRandomModule) {
    auto r = check(
        "import random::random\n"
        "func main() {\n"
        "    let n = randInt(0, 100)\n"
        "    let f = randFloat()\n"
        "    let b = randBool()\n"
        "    let p = randPercent()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import random::random should resolve randInt/randFloat/randBool/randPercent";
}

TEST_F(StdlibModuleTest, RandomStructMethods) {
    auto r = check(
        "import random::random\n"
        "func main() {\n"
        "    let r = Random.new()\n"
        "    let n = r.nextInt(1, 10)\n"
        "    let f = r.nextFloat()\n"
        "    let b = r.nextBool()\n"
        "    let p = r.percent()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Random struct methods should type-check";
}

TEST_F(StdlibModuleTest, RandomSeedAndUuid) {
    auto r = check(
        "import random::random\n"
        "func main() {\n"
        "    randSeed(42)\n"
        "    let big = randI64()\n"
        "    let id = randUuid()\n"
        "    let r = Random.new()\n"
        "    let bigR = r.nextLong()\n"
        "    let idR = r.uuid()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "randSeed/randI64/randUuid should type-check";
}

// ============================================================
// Module 12: os::os
// ============================================================

TEST_F(StdlibModuleTest, ImportOsModule) {
    auto r = check(
        "import os::os\n"
        "func main() {\n"
        "    let home = getEnv(\"HOME\")\n"
        "    let a = getArgs()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import os::os should resolve getEnv/getArgs";
}

TEST_F(StdlibModuleTest, OsProcessMethods) {
    auto r = check(
        "import os::os\n"
        "func main() {\n"
        "    let p = Process.start(\"echo hi\")\n"
        "    let code = p.wait()\n"
        "    let out = p.read()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Process struct should type-check";
}

TEST_F(StdlibModuleTest, OsRunHelpers) {
    auto r = check(
        "import os::os\n"
        "func main() {\n"
        "    let code = runCommand(\"ls\")\n"
        "    let out = runCommandOutput(\"ls\")\n"
        "    sleep(100)\n"
        "    let ms = clockMs()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "os::runCommand/runCommandOutput/sleep/clockMs should type-check";
}

// ============================================================
// Module 13: log::log
// ============================================================

TEST_F(StdlibModuleTest, ImportLogModule) {
    auto r = check(
        "import log::log\n"
        "func main() {\n"
        "    debug(\"d\")\n"
        "    info(\"i\")\n"
        "    warn(\"w\")\n"
        "    error(\"e\")\n"
        "    setLevel(1)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import log::log should resolve debug/info/warn/error";
}

TEST_F(StdlibModuleTest, LogLoggerStruct) {
    auto r = check(
        "import log::log\n"
        "func main() {\n"
        "    let l = Logger.new(\"http\")\n"
        "    l.info(\"hello\")\n"
        "    l.error(\"oops\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Logger struct should type-check";
}

// ============================================================
// Module 14: math::math
// ============================================================

TEST_F(StdlibModuleTest, ImportMathModule) {
    auto r = check(
        "import math::math\n"
        "func main() {\n"
        "    let a = absF(-3.5)\n"
        "    let s = sqrtF(16.0)\n"
        "    let p = powF(2.0, 10.0)\n"
        "    let pi = PI()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import math::math should resolve basic functions and constants";
}

TEST_F(StdlibModuleTest, MathClampAndSign) {
    auto r = check(
        "import math::math\n"
        "func main() {\n"
        "    let c = clampI(15, 0, 10)\n"
        "    let cf = clampF(1.5, 0.0, 1.0)\n"
        "    let s = signI(-5)\n"
        "    let d = degToRad(180.0)\n"
        "    let r = radToDeg(3.14)\n"
        "    let ev = isEven(4)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Math clamp/sign/conversions should type-check";
}

// ============================================================
// Module 15: convert::convert
// ============================================================

TEST_F(StdlibModuleTest, ImportConvertModule) {
    auto r = check(
        "import convert::convert\n"
        "func main() {\n"
        "    let n = toInt(\"42\")\n"
        "    let f = toFloat(\"3.14\")\n"
        "    let s = i32ToStr(123)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import convert::convert should resolve toInt/toFloat/i32ToStr";
}

TEST_F(StdlibModuleTest, ConvertWithDefaults) {
    auto r = check(
        "import convert::convert\n"
        "func main() {\n"
        "    let n = toIntOr(\"abc\", 0)\n"
        "    let f = toFloatOr(\"xyz\", 1.5)\n"
        "    let c = codepointToStr(65)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Convert fallback helpers should type-check";
}

// ============================================================
// DateTime arithmetic helpers
// ============================================================

TEST_F(StdlibModuleTest, DateTimeArithmetic) {
    auto r = check(
        "import time::time\n"
        "func main() {\n"
        "    let dt = DateTime.now()\n"
        "    let t1 = dt.addSeconds(30)\n"
        "    let t2 = dt.addMinutes(15)\n"
        "    let t3 = dt.addHours(2)\n"
        "    let t4 = dt.addDays(7)\n"
        "    let t5 = dt.addWeeks(1)\n"
        "    let t6 = dt.subDays(3)\n"
        "    let u = dt.toUnix()\n"
        "    let d = dt.diffDays(t4)\n"
        "    let h = dt.diffHours(t3)\n"
        "    let m = dt.diffMinutes(t2)\n"
        "    let after = dt.isAfter(t1)\n"
        "    let before = dt.isBefore(t1)\n"
        "    let eq = dt.equals(t1)\n"
        "    let dateStr = dt.toDate()\n"
        "    let timeStr = dt.toTime()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "DateTime arithmetic helpers should type-check";
}

TEST_F(StdlibModuleTest, DateTimeFromUnix) {
    auto r = check(
        "import time::time\n"
        "func main() {\n"
        "    let dt = fromUnix(1700000000)\n"
        "    let t = today()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "fromUnix/today helpers should type-check";
}

// ============================================================
// File metadata
// ============================================================

TEST_F(StdlibModuleTest, FsFileInfoMetadata) {
    auto r = check(
        "import fs::fs\n"
        "func main() {\n"
        "    let info = FileInfo.new(\"/tmp/test\")\n"
        "    let sz = info.size()\n"
        "    let mt = info.modifiedTime()\n"
        "    let f = info.isFile()\n"
        "    let d = info.isDir()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "FileInfo metadata methods should type-check";
}

TEST_F(StdlibModuleTest, FsDirOperations) {
    auto r = check(
        "import fs::fs\n"
        "func main() {\n"
        "    let d = Dir.new(\"/tmp/foo\")\n"
        "    let e = d.exists()\n"
        "    let c = d.create()\n"
        "    let r = d.remove()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Dir create/remove/exists should type-check";
}

// ============================================================
// Regex split
// ============================================================

TEST_F(StdlibModuleTest, RegexSplit) {
    auto r = check(
        "import regex::regex\n"
        "func main() {\n"
        "    let re = Regex.new(\",\\\\s*\")\n"
        "    let parts = re.split(\"a, b, c\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Regex split should type-check";
}

// ============================================================
// JSON pretty printing
// ============================================================

TEST_F(StdlibModuleTest, JsonStringifyPretty) {
    auto r = check(
        "import json::json\n"
        "func main() {\n"
        "    var obj = JsonObject.create()\n"
        "    obj.setInt(\"age\", 25)\n"
        "    let pretty = obj.stringifyPretty(2)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "JsonObject stringifyPretty should type-check";
}

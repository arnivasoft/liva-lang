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

TEST_F(StdlibModuleTest, CryptoSha1AndSha512) {
    auto r = check(
        "import crypto::crypto\n"
        "func main() {\n"
        "    let h1 = Hash.sha1(\"hello\")\n"
        "    let h2 = Hash.sha512(\"hello\")\n"
        "    let s1 = h1.hex()\n"
        "    let s2 = h2.hex()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Hash.sha1/sha512 should type-check";
}

TEST_F(StdlibModuleTest, EncodingBase64Url) {
    auto r = check(
        "import encoding::encoding\n"
        "func main() {\n"
        "    let e: string = toBase64Url(\"hello\")\n"
        "    let d: string? = fromBase64Url(e)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "toBase64Url/fromBase64Url should type-check";
}

TEST_F(StdlibModuleTest, EncodingBase64UrlStruct) {
    auto r = check(
        "import encoding::encoding\n"
        "func main() {\n"
        "    let b = Base64Url.encode(\"hello\")\n"
        "    let s: string = b.toString()\n"
        "    let d: string? = b.decode()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Base64Url struct should type-check";
}

TEST_F(StdlibModuleTest, ImportStreamModule) {
    auto r = check(
        "import stream::stream\n"
        "func main() {\n"
        "    var arr: [string] = []\n"
        "    let s = StringStream.from(arr)\n"
        "    var nums: [i64] = []\n"
        "    let ns = IntStream.from(nums)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import stream::stream should resolve StringStream/IntStream";
}

TEST_F(StdlibModuleTest, RandomUuidV7Resolves) {
    auto r = check(
        "import random::random\n"
        "func main() {\n"
        "    let u: string = randUuidV7()\n"
        "    let r = Random.new()\n"
        "    let v: string = r.uuidV7()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "randUuidV7 + Random.uuidV7 should type-check";
}

TEST_F(StdlibModuleTest, StreamGenericResolves) {
    auto r = check(
        "import stream::stream\n"
        "func main() {\n"
        "    var arr: [i64] = []\n"
        "    let s = Stream.from(arr)\n"
        "    let n: i64 = s.count()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "generic Stream<T> should monomorphize from arr type";
}

TEST_F(StdlibModuleTest, ImportCsvModule) {
    auto r = check(
        "import csv::csv\n"
        "func main() {\n"
        "    let r: [string] = csvParseRow(\"a,b,c\")\n"
        "    var fields: [string] = []\n"
        "    fields.push(\"x\")\n"
        "    let line: string = csvJoinRow(fields)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import csv::csv should resolve csvParseRow/csvJoinRow";
}

TEST_F(StdlibModuleTest, ImportJwtModule) {
    auto r = check(
        "import jwt::jwt\n"
        "func main() {\n"
        "    let t = Jwt.signHS256(\"secret\", \"{}\")\n"
        "    let s: string = t.toString()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import jwt::jwt should resolve Jwt";
}

TEST_F(StdlibModuleTest, TimeDateTimeToISO) {
    auto r = check(
        "import time::time\n"
        "func main() {\n"
        "    let dt = DateTime.now()\n"
        "    let s: String = dt.toISO()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "DateTime.toISO should type-check";
}

TEST_F(StdlibModuleTest, TimeFromISOReturnsOptional) {
    auto r = check(
        "import time::time\n"
        "func main() {\n"
        "    let dt: DateTime? = fromISO(\"2026-01-15T12:34:56Z\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "fromISO should return DateTime?";
}

TEST_F(StdlibModuleTest, JwtVerifyReturnsOptional) {
    auto r = check(
        "import jwt::jwt\n"
        "func main() {\n"
        "    let p: string? = Jwt.verifyHS256(\"a.b.c\", \"k\")\n"
        "    let q: string? = Jwt.verifyHS512(\"a.b.c\", \"k\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Jwt.verify methods should return string?";
}

TEST_F(StdlibModuleTest, CryptoHmacSha1AndSha512) {
    auto r = check(
        "import crypto::crypto\n"
        "func main() {\n"
        "    let m1 = Hmac.sha1(\"k\", \"d\")\n"
        "    let m2 = Hmac.sha512(\"k\", \"d\")\n"
        "    let s1 = m1.hex()\n"
        "    let s2 = m2.hex()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Hmac.sha1/sha512 should type-check";
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
        "    let resp = HttpResponse { handle: 0, status: 200, body: \"hello\", ok: true }\n"
        "    let t = resp.text()\n"
        "    let o = resp.isOk()\n"
        "    let c = resp.statusCode()\n"
        "    let ok2 = resp.is2xx()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpResponse struct should type-check";
}

TEST_F(StdlibModuleTest, HttpResponseStatusCategories) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    let resp = HttpResponse { handle: 0, status: 404, body: \"\", ok: false }\n"
        "    let c4 = resp.is4xx()\n"
        "    let c5 = resp.is5xx()\n"
        "    let c3 = resp.is3xx()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpResponse status category methods should type-check";
}

TEST_F(StdlibModuleTest, HttpClientSend) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    var c = HttpClient.new()\n"
        "    c.withTimeout(5000)\n"
        "    let resp = c.send(\"GET\", \"http://example.com\", \"\")\n"
        "    let s = resp.statusCode()\n"
        "    let h = resp.header(\"Content-Type\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpClient.send + response header should type-check";
}

TEST_F(StdlibModuleTest, HttpClientFullMethods) {
    auto r = check(
        "import http::http\n"
        "func main() {\n"
        "    let c = HttpClient.new()\n"
        "    let r1 = c.getFull(\"http://example.com\")\n"
        "    let r2 = c.postFull(\"http://example.com\", \"data\")\n"
        "    let r3 = c.putFull(\"http://example.com/1\", \"data\")\n"
        "    let r4 = c.patchFull(\"http://example.com/1\", \"{}\")\n"
        "    let r5 = c.deleteFull(\"http://example.com/1\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HttpClient full methods should type-check";
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

// ============================================================
// Collections extensions: Stack / Queue / Deque / HashSet
// ============================================================

TEST_F(StdlibModuleTest, CollectionsGenericStack) {
    auto r = check(
        "import collections::collections\n"
        "func main() {\n"
        "    var s: Stack<i64> = Stack.new()\n"
        "    s.push(42)\n"
        "    let p = s.pop()\n"
        "    let sz = s.size()\n"
        "    let e = s.isEmpty()\n"
        "    var sStr: Stack<String> = Stack.new()\n"
        "    sStr.push(\"hello\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Generic Stack<T> should type-check via inference";
}

TEST_F(StdlibModuleTest, CollectionsGenericQueue) {
    auto r = check(
        "import collections::collections\n"
        "func main() {\n"
        "    var q: Queue<i64> = Queue.new()\n"
        "    q.enqueue(1)\n"
        "    q.enqueue(2)\n"
        "    let sz = q.size()\n"
        "    let e = q.isEmpty()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Generic Queue<T> should type-check via inference";
}

TEST_F(StdlibModuleTest, CollectionsStack) {
    auto r = check(
        "import collections::collections\n"
        "func main() {\n"
        "    var s = StackI64.new()\n"
        "    s.push(42)\n"
        "    let t = s.peek()\n"
        "    let p = s.pop()\n"
        "    let e = s.isEmpty()\n"
        "    let sz = s.size()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "StackI64 should type-check";
}

TEST_F(StdlibModuleTest, CollectionsStackStr) {
    auto r = check(
        "import collections::collections\n"
        "func main() {\n"
        "    var s = StackStr.new()\n"
        "    s.push(\"hello\")\n"
        "    let top = s.peek()\n"
        "    let v = s.pop()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "StackStr should type-check";
}

TEST_F(StdlibModuleTest, CollectionsQueue) {
    auto r = check(
        "import collections::collections\n"
        "func main() {\n"
        "    var q = QueueI64.new()\n"
        "    q.enqueue(1)\n"
        "    q.enqueue(2)\n"
        "    let front = q.peek()\n"
        "    let head = q.dequeue()\n"
        "    let sz = q.size()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "QueueI64 should type-check";
}

TEST_F(StdlibModuleTest, CollectionsDeque) {
    auto r = check(
        "import collections::collections\n"
        "func main() {\n"
        "    var d = DequeI64.new()\n"
        "    d.pushBack(1)\n"
        "    d.pushFront(0)\n"
        "    let b = d.back()\n"
        "    let f = d.front()\n"
        "    let pb = d.popBack()\n"
        "    let pf = d.popFront()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "DequeI64 should type-check";
}

TEST_F(StdlibModuleTest, CollectionsHashSetI64) {
    auto r = check(
        "import collections::collections\n"
        "func main() {\n"
        "    var s = HashSetI64.new()\n"
        "    s.add(42)\n"
        "    let h = s.contains(42)\n"
        "    let r = s.remove(42)\n"
        "    let sz = s.size()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HashSetI64 should type-check";
}

TEST_F(StdlibModuleTest, CollectionsHashSetStr) {
    auto r = check(
        "import collections::collections\n"
        "func main() {\n"
        "    var s = HashSetStr.new()\n"
        "    s.add(\"hello\")\n"
        "    let h = s.contains(\"hello\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "HashSetStr should type-check";
}

TEST_F(StdlibModuleTest, CollectionsMathHelpers) {
    auto r = check(
        "import collections::collections\n"
        "func main() {\n"
        "    let nums: [i64] = [1, 2, 3, 4, 5]\n"
        "    let s = sumI64(nums)\n"
        "    let p = productI64(nums)\n"
        "    let mn = minOfI64(nums)\n"
        "    let mx = maxOfI64(nums)\n"
        "    let a = avgI64(nums)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "sum/product/min/max/avg helpers should type-check";
}

// ============================================================
// Testing: extended assertions
// ============================================================

TEST_F(StdlibModuleTest, TestingExpectComparisons) {
    auto r = check(
        "import testing::testing\n"
        "func main() {\n"
        "    let e = Expect.new(42)\n"
        "    e.toNotBe(99)\n"
        "    e.toBeGt(10)\n"
        "    e.toBeGte(42)\n"
        "    e.toBeLt(100)\n"
        "    e.toBeLte(42)\n"
        "    e.toBeInRange(0, 100)\n"
        "    e.toBePositive()\n"
        "    let z = Expect.new(0)\n"
        "    z.toBeZero()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Expect comparison assertions should type-check";
}

TEST_F(StdlibModuleTest, TestingExpectStrExtra) {
    auto r = check(
        "import testing::testing\n"
        "func main() {\n"
        "    let s = ExpectStr.new(\"hello world\")\n"
        "    s.toContain(\"world\")\n"
        "    s.toNotContain(\"xyz\")\n"
        "    s.toStartWith(\"hello\")\n"
        "    s.toEndWith(\"world\")\n"
        "    s.toHaveLength(11)\n"
        "    s.toNotBeEmpty()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "ExpectStr extra assertions should type-check";
}

TEST_F(StdlibModuleTest, TestingExpectFloatTolerance) {
    auto r = check(
        "import testing::testing\n"
        "func main() {\n"
        "    let f = ExpectFloat.new(3.14)\n"
        "    f.toBeNear(3.14159, 0.01)\n"
        "    f.toBeGt(3.0)\n"
        "    f.toBeBetween(3.0, 4.0)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "ExpectFloat tolerance assertions should type-check";
}

TEST_F(StdlibModuleTest, TestingExpectArrays) {
    auto r = check(
        "import testing::testing\n"
        "func main() {\n"
        "    let nums: [i64] = [1, 2, 3]\n"
        "    let a = ExpectArrI64.new(nums)\n"
        "    a.toHaveLength(3)\n"
        "    a.toNotBeEmpty()\n"
        "    a.toContain(2)\n"
        "    a.toNotContain(99)\n"
        "    let strs: [String] = [\"a\", \"b\"]\n"
        "    let sa = ExpectArrStr.new(strs)\n"
        "    sa.toHaveLength(2)\n"
        "    sa.toContain(\"a\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "ExpectArrI64/ExpectArrStr should type-check";
}

TEST_F(StdlibModuleTest, TestingTestGroup) {
    auto r = check(
        "import testing::testing\n"
        "func addTest() {\n"
        "    let e = Expect.new(2)\n"
        "    e.toBe(2)\n"
        "}\n"
        "func main() {\n"
        "    var g = TestGroup.new(\"math\")\n"
        "    g.test(\"addition\", addTest)\n"
        "    let p = g.passed()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TestGroup.test should type-check";
}

TEST_F(StdlibModuleTest, TestingFailedCounter) {
    auto r = check(
        "import testing::testing\n"
        "func passingTest() {\n"
        "    let e = Expect.new(1)\n"
        "    e.toBe(1)\n"
        "}\n"
        "func failingTest() {\n"
        "    let e = Expect.new(1)\n"
        "    e.toBe(2)\n"
        "}\n"
        "func main() {\n"
        "    var suite = TestSuite.new(\"ex\")\n"
        "    suite.run(\"pass\", passingTest)\n"
        "    suite.run(\"fail\", failingTest)\n"
        "    let t = suite.total()\n"
        "    let ok = suite.allPassed()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TestSuite.run using testRunClosure should type-check";
}

TEST_F(StdlibModuleTest, TestRunClosureBuiltin) {
    auto r = check(
        "func body() {\n"
        "    let x = 1\n"
        "}\n"
        "func main() {\n"
        "    let ok = testRunClosure(\"case\", body)\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "testRunClosure builtin should be globally available";
}

TEST_F(StdlibModuleTest, TestingRunWithHooks) {
    auto r = check(
        "import testing::testing\n"
        "func setup() { let x = 1 }\n"
        "func runTest() {\n"
        "    let e = Expect.new(1)\n"
        "    e.toBe(1)\n"
        "}\n"
        "func teardown() { let x = 1 }\n"
        "func main() {\n"
        "    var suite = TestSuite.new(\"s\")\n"
        "    suite.runWithHooks(\"t\", setup, runTest, teardown)\n"
        "    suite.skip(\"pending\")\n"
        "    let all = suite.allPassed()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TestSuite runWithHooks/skip should type-check";
}

// ============================================================
// Module: strings::strings (UTF-8 utilities)
// ============================================================

TEST_F(StdlibModuleTest, StringsUtf8Builtins) {
    auto r = check(
        "func main() {\n"
        "    let n = strCharCount(\"hello\")\n"
        "    let cp = strCodepointAt(\"hello\", 0)\n"
        "    let a = strIsAscii(\"hello\")\n"
        "    let isA = charIsAlpha(65)\n"
        "    let isD = charIsDigit(48)\n"
        "    let up = charToUpper(97)\n"
        "    let lo = charToLower(65)\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "UTF-8 builtins should be globally resolvable";
}

TEST_F(StdlibModuleTest, StringsCodepointHelpers) {
    auto r = check(
        "import strings::strings\n"
        "func main() {\n"
        "    let cps = toCodepoints(\"abc\")\n"
        "    let a = countAlpha(\"abc 123\")\n"
        "    let d = countDigit(\"abc 123\")\n"
        "    let an = isAlnum(\"abc123\")\n"
        "    let bl = isBlank(\"   \")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "strings module UTF-8 helpers should type-check";
}

TEST_F(StdlibModuleTest, StringsCharPredicates) {
    auto r = check(
        "func main() {\n"
        "    let a = charIsAlnum(65)\n"
        "    let s = charIsSpace(32)\n"
        "    let u = charIsUpper(65)\n"
        "    let l = charIsLower(97)\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "char predicate builtins should type-check";
}

// ============================================================
// Module: errors::errors (Result context chaining)
// ============================================================

TEST_F(StdlibModuleTest, ImportErrorsModule) {
    auto r = check(
        "import errors::errors\n"
        "func readConfig() -> Result<String, String> {\n"
        "    return Result.err(\"not found\")\n"
        "}\n"
        "func main() {\n"
        "    let r1 = withContext(readConfig(), \"loading config\")\n"
        "    let r2 = readConfig()\n"
        "    let ok = isOk(r2)\n"
        "    let r3 = readConfig()\n"
        "    let e = isErr(r3)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import errors::errors should resolve withContext/isOk/isErr";
}

TEST_F(StdlibModuleTest, ErrorsUnwrapHelpers) {
    auto r = check(
        "import errors::errors\n"
        "func op() -> Result<i32, String> {\n"
        "    return Result.ok(42)\n"
        "}\n"
        "func main() {\n"
        "    let v = unwrapOr(op(), 0)\n"
        "    let e = errOr(op(), \"no error\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "unwrapOr/errOr helpers should type-check";
}

TEST_F(StdlibModuleTest, ErrorChainStruct) {
    auto r = check(
        "import errors::errors\n"
        "func main() {\n"
        "    let e1 = ErrorChain.new(\"base error\")\n"
        "    let e2 = ErrorChain.wrap(\"outer\", \"inner cause\")\n"
        "    let s = e2.toString()\n"
        "    let r = e2.root()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "ErrorChain struct should type-check";
}

TEST_F(StdlibModuleTest, ErrorsLazyContext) {
    auto r = check(
        "import errors::errors\n"
        "func op() -> Result<i32, String> { return Result.err(\"x\") }\n"
        "func makeMsg() -> String { return \"loading\" }\n"
        "func main() {\n"
        "    let r = withContextLazy(op(), makeMsg)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "withContextLazy should accept a closure producer";
}

// ============================================================
// Module: io::io (buffered I/O helpers)
// ============================================================

TEST_F(StdlibModuleTest, ImportIoModule) {
    auto r = check(
        "import io::io\n"
        "func main() {\n"
        "    let lines = readLines(\"test.txt\")\n"
        "    let arr: [String] = [\"a\", \"b\"]\n"
        "    let ok = writeLines(\"out.txt\", arr)\n"
        "    let app = appendLine(\"log.txt\", \"msg\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import io::io should resolve readLines/writeLines/appendLine";
}

TEST_F(StdlibModuleTest, IoLineReader) {
    auto r = check(
        "import io::io\n"
        "func main() {\n"
        "    let opened = LineReader.open(\"data.txt\")\n"
        "    if let reader = opened {\n"
        "        let line = reader.next()\n"
        "        let eof = reader.isEof()\n"
        "        reader.close()\n"
        "    }\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "LineReader methods should type-check";
}

TEST_F(StdlibModuleTest, IoLineWriter) {
    auto r = check(
        "import io::io\n"
        "func main() {\n"
        "    let opened = LineWriter.open(\"out.txt\")\n"
        "    if let w = opened {\n"
        "        w.write(\"line 1\")\n"
        "        w.writeRaw(\"raw\")\n"
        "        w.close()\n"
        "    }\n"
        "    let a = LineWriter.openAppend(\"log.txt\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "LineWriter methods should type-check";
}

// ============================================================
// Module: encoding::encoding
// ============================================================

TEST_F(StdlibModuleTest, ImportEncodingModule) {
    auto r = check(
        "import encoding::encoding\n"
        "func main() {\n"
        "    let b = toBase64(\"hello\")\n"
        "    let d = fromBase64(b)\n"
        "    let h = toHex(\"hi\")\n"
        "    let cs = checksum(\"data\")\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import encoding::encoding should resolve";
}

TEST_F(StdlibModuleTest, EncodingUrlHelpers) {
    auto r = check(
        "import encoding::encoding\n"
        "func main() {\n"
        "    let e = toUrl(\"hello world!\")\n"
        "    let d = fromUrl(e)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "URL encode/decode helpers should type-check";
}

TEST_F(StdlibModuleTest, EncodingBase64Struct) {
    auto r = check(
        "import encoding::encoding\n"
        "func main() {\n"
        "    let enc = Base64.encode(\"secret\")\n"
        "    let s = enc.toString()\n"
        "    let back = enc.decode()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Base64 struct should type-check";
}

TEST_F(StdlibModuleTest, EncodingHexStruct) {
    auto r = check(
        "import encoding::encoding\n"
        "func main() {\n"
        "    let enc = Hex.encode(\"data\")\n"
        "    let s = enc.toString()\n"
        "    let back = enc.decode()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Hex struct should type-check";
}

TEST_F(StdlibModuleTest, EncodingUrlStruct) {
    auto r = check(
        "import encoding::encoding\n"
        "func main() {\n"
        "    let enc = Url.encode(\"hello world\")\n"
        "    let s = enc.toString()\n"
        "    let back = enc.decode()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Url struct should type-check";
}

TEST_F(StdlibModuleTest, UrlEncodeBuiltin) {
    auto r = check(
        "func main() {\n"
        "    let e = urlEncode(\"a b+c\")\n"
        "    let d = urlDecode(\"a%20b%2Bc\")\n"
        "}\n");
    EXPECT_TRUE(r.passed) << "urlEncode/urlDecode builtins should be globally available";
}

TEST_F(StdlibModuleTest, CollectionsSliceHelpers) {
    auto r = check(
        "import collections::collections\n"
        "func main() {\n"
        "    let nums: [i64] = [1, 2, 3, 4, 5]\n"
        "    let h = takeI64(nums, 3)\n"
        "    let t = skipI64(nums, 2)\n"
        "    let f = firstI64(nums)\n"
        "    let l = lastI64(nums)\n"
        "    let i = findIndexI64(nums, 3)\n"
        "    let u = uniqueI64(nums)\n"
        "    let r = reverseI64(nums)\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "take/skip/first/last/findIndex/unique/reverse should type-check";
}

// ============================================================
// Module: async::async — retry + Once
// ============================================================

TEST_F(StdlibModuleTest, AsyncRetryTypeChecks) {
    auto r = check(
        "import async::async\n"
        "func main() {\n"
        "    let ok: bool = retry(3, 10, || -> bool { return true })\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "retry(maxAttempts, baseDelayMs, action) should type-check";
}

TEST_F(StdlibModuleTest, AsyncOnceTypeChecks) {
    auto r = check(
        "import async::async\n"
        "func main() {\n"
        "    let o = Once.new()\n"
        "    let ran: bool = o.doIt(|| { println(\"hi\") })\n"
        "    let d: bool = o.done()\n"
        "    o.free()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "Once struct API should type-check";
}

// ============================================================
// Module: toml::toml — minimal TOML parser
// ============================================================

TEST_F(StdlibModuleTest, ImportTomlModule) {
    auto r = check(
        "import toml::toml\n"
        "func main() {\n"
        "    var doc = TomlDocument.parse(\"a = 1\")\n"
        "    let ok: bool = doc.isValid()\n"
        "    doc.free()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "import toml::toml should resolve TomlDocument";
}

TEST_F(StdlibModuleTest, TomlDocumentMethods) {
    auto r = check(
        "import toml::toml\n"
        "func main() {\n"
        "    var doc = TomlDocument.parse(\"[a]\\nname = \\\"x\\\"\")\n"
        "    let s: string? = doc.getString(\"a\", \"name\")\n"
        "    let i: i64? = doc.getInt(\"a\", \"missing\")\n"
        "    let b: bool? = doc.getBool(\"a\", \"flag\")\n"
        "    let h: bool = doc.hasKey(\"a\", \"name\")\n"
        "    doc.free()\n"
        "}\n",
        true, "stdlib");
    EXPECT_TRUE(r.passed) << "TomlDocument methods should type-check";
}

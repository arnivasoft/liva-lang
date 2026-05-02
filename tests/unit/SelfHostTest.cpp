// Self-hosting end-to-end tests: compile Liva programs, run them, verify output.
// Only built when LIVA_HAS_LLVM is defined (Clang build with LLVM backend).

#ifdef LIVA_HAS_LLVM

#include "liva/Driver/CompilerInstance.h"
#include "liva/Driver/Driver.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

class SelfHostTest : public ::testing::Test {
protected:
    std::string buildDir_;
    std::string tmpExe_;
    std::string tmpOut_;

    void SetUp() override {
        buildDir_ = LIVA_BUILD_DIR;
#ifdef _WIN32
        tmpExe_ = buildDir_ + "/_selfhost_test.exe";
#else
        tmpExe_ = buildDir_ + "/_selfhost_test";
#endif
        tmpOut_ = buildDir_ + "/_selfhost_out.txt";
    }

    void TearDown() override {
        std::remove(tmpExe_.c_str());
        std::remove(tmpOut_.c_str());
        // Clean up temp object file left by compile()
        std::remove((tmpExe_ + ".o").c_str());
    }

    std::string compileAndRun(const std::string &source) {
        liva::CompilerInstance compiler;
        compiler.setSource("_selfhost.liva", source);
#ifdef _WIN32
        compiler.setExecutablePath(buildDir_ + "/livac.exe");
#else
        compiler.setExecutablePath(buildDir_ + "/livac");
#endif

        bool compiled = compiler.compile(tmpExe_);
        if (!compiled) {
            ADD_FAILURE() << "Compilation failed";
            return "";
        }

        std::string cmd = "\"" + tmpExe_ + "\"" + " > " + "\"" + tmpOut_ + "\"" + " 2>&1";
#ifdef _WIN32
        // Windows cmd.exe needs outer quotes when inner quotes are present
        cmd = "\"" + cmd + "\"";
#endif
        std::system(cmd.c_str());

        std::ifstream ifs(tmpOut_);
        if (!ifs.is_open()) {
            ADD_FAILURE() << "Cannot open output file";
            return "";
        }
        std::stringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    }

    void expectOutput(const std::string &source, const std::string &expected) {
        std::string output = compileAndRun(source);
        EXPECT_EQ(output, expected);
    }

    void expectCompiles(const std::string &source) {
        liva::CompilerInstance compiler;
        compiler.setSource("_selfhost.liva", source);
#ifdef _WIN32
        compiler.setExecutablePath(buildDir_ + "/livac.exe");
#else
        compiler.setExecutablePath(buildDir_ + "/livac");
#endif
        EXPECT_TRUE(compiler.compile(tmpExe_));
    }

    void expectCompileFail(const std::string &source) {
        liva::CompilerInstance compiler;
        compiler.setSource("_selfhost.liva", source);
        EXPECT_FALSE(compiler.checkOnly());
    }
};

// ============================================================
// 1. Basic I/O
// ============================================================

TEST_F(SelfHostTest, HelloWorld) {
    expectOutput(R"--(
func main() {
    println("Hello, World!")
}
)--", "Hello, World!\n");
}

TEST_F(SelfHostTest, PrintInt) {
    expectOutput(R"--(
func main() {
    println(42)
}
)--", "42\n");
}

TEST_F(SelfHostTest, PrintBool) {
    expectOutput(R"--(
func main() {
    let a: bool = true
    let b: bool = false
    if a { println("true") } else { println("false") }
    if b { println("true") } else { println("false") }
}
)--", "true\nfalse\n");
}

// ============================================================
// 2. Arithmetic & Variables
// ============================================================

TEST_F(SelfHostTest, Arithmetic) {
    expectOutput(R"--(
func main() {
    println(2 + 3 * 4)
}
)--", "14\n");
}

TEST_F(SelfHostTest, Variables) {
    expectOutput(R"--(
func main() {
    let x: i32 = 10
    var y: i32 = 20
    y = y + x
    println(y)
}
)--", "30\n");
}

TEST_F(SelfHostTest, FloatArithmetic) {
    expectOutput(R"--(
func main() {
    let x: f64 = 3.14
    let y: f64 = 2.0
    println(x * y)
}
)--", "6.280000\n");
}

// ============================================================
// 3. Control Flow
// ============================================================

TEST_F(SelfHostTest, IfElse) {
    expectOutput(R"--(
func main() {
    let x: i32 = 10
    if x > 5 {
        println("big")
    } else {
        println("small")
    }
}
)--", "big\n");
}

TEST_F(SelfHostTest, WhileLoop) {
    expectOutput(R"--(
func main() {
    var sum: i32 = 0
    var i: i32 = 1
    while i <= 10 {
        sum = sum + i
        i = i + 1
    }
    println(sum)
}
)--", "55\n");
}

TEST_F(SelfHostTest, ForRange) {
    expectOutput(R"--(
func main() {
    var sum: i32 = 0
    for i in 0..5 {
        sum = sum + i
    }
    println(sum)
}
)--", "10\n");
}

TEST_F(SelfHostTest, ForInclusiveRange) {
    expectOutput(R"--(
func main() {
    var sum: i32 = 0
    for i in 0..=5 {
        sum = sum + i
    }
    println(sum)
}
)--", "15\n");
}

TEST_F(SelfHostTest, InclusiveRangeArraySlice) {
    expectOutput(R"--(
func main() {
    let arr: [i32] = [10, 20, 30, 40, 50]
    let s = arr[1..=3]
    for x in s {
        println(x)
    }
}
)--", "20\n30\n40\n");
}

TEST_F(SelfHostTest, RwlockBasicLifecycle) {
    expectOutput(R"--(
func main() {
    let rw: i64 = rwlockCreate()
    rwlockWriteLock(rw)
    rwlockWriteUnlock(rw)
    rwlockReadLock(rw)
    rwlockReadUnlock(rw)
    let acquired: bool = rwlockTryWriteLock(rw)
    if acquired {
        rwlockWriteUnlock(rw)
        println("ok")
    }
    rwlockFree(rw)
}
)--", "ok\n");
}

TEST_F(SelfHostTest, CondVarNotifyWithoutWaiters) {
    // notify_one/notify_all with no waiters is well-defined and does nothing.
    // This exercises the IR wiring without needing multithreading.
    expectOutput(R"--(
func main() {
    let m: i64 = mutexCreate()
    let cv: i64 = condVarCreate()
    mutexLock(m)
    condVarNotifyOne(cv)
    condVarNotifyAll(cv)
    mutexUnlock(m)
    condVarFree(cv)
    mutexFree(m)
    println("done")
}
)--", "done\n");
}

TEST_F(SelfHostTest, ChannelTrySendReceiveSuccess) {
    expectOutput(R"--(
func main() {
    let ch: i64 = channelCreate(2)
    let ok1: bool = channelTrySend(ch, 100)
    let ok2: bool = channelTrySend(ch, 200)
    if ok1 {
        if ok2 {
            println("both sent")
        }
    }
    let v1: i64? = channelTryReceive(ch)
    let v2: i64? = channelTryReceive(ch)
    let v3: i64? = channelTryReceive(ch)
    if let x = v1 {
        println(x)
    }
    if let y = v2 {
        println(y)
    }
    var hadV3: bool = false
    if let z = v3 {
        hadV3 = true
    }
    if !hadV3 {
        println("empty")
    }
    channelFree(ch)
}
)--", "both sent\n100\n200\nempty\n");
}

TEST_F(SelfHostTest, ChannelTrySendFullReturnsFalse) {
    expectOutput(R"--(
func main() {
    let ch: i64 = channelCreate(1)
    let ok1: bool = channelTrySend(ch, 1)
    let ok2: bool = channelTrySend(ch, 2)
    if ok1 {
        if !ok2 {
            println("full")
        }
    }
    channelFree(ch)
}
)--", "full\n");
}

// === Compiler bug fix: function-typed parameters in struct methods ===
// Previously visitImplDecl forgot to register fn-typed params in varFuncTypes_,
// so calling them inside the method body produced "unresolved function".

TEST_F(SelfHostTest, ClosureParamInStructMethod) {
    expectOutput(R"--(
struct Calc {
    var n: i64
}

impl Calc {
    pub func apply(ref self, fn: (i64) -> i64) -> i64 {
        return fn(self.n)
    }
}

func main() {
    let c = Calc { n: 21 as i64 }
    let r: i64 = c.apply(|x: i64| -> i64 { return x * (2 as i64) })
    println(r)
}
)--", "42\n");
}

TEST_F(SelfHostTest, TomlParseAndGetters) {
    expectOutput(R"--(
import toml::toml
func main() {
    let text: string = "[project]\nname = \"liva\"\nversion = 42\nactive = true\n"
    var doc = TomlDocument.parse(text)
    if doc.isValid() {
        if let name = doc.getString("project", "name") {
            println(name)
        }
        if let v = doc.getInt("project", "version") {
            println(v)
        }
        if let a = doc.getBool("project", "active") {
            if a {
                println("active")
            }
        }
        if doc.hasKey("project", "name") {
            println("has name")
        }
        if !doc.hasKey("project", "missing") {
            println("no missing")
        }
    }
    doc.free()
}
)--", "liva\n42\nactive\nhas name\nno missing\n");
}

TEST_F(SelfHostTest, Sha1KnownVectors) {
    expectOutput(R"--(
import std::crypto
func main() {
    println(sha1("abc"))
    println(sha1("hello world"))
}
)--",
        "a9993e364706816aba3e25717850c26c9cd0d89d\n"
        "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed\n");
}

TEST_F(SelfHostTest, Sha512KnownVector) {
    expectOutput(R"--(
import std::crypto
func main() {
    println(sha512("abc"))
}
)--",
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f\n");
}

TEST_F(SelfHostTest, HmacSha1KnownVector) {
    expectOutput(R"--(
import std::crypto
func main() {
    println(hmacSha1("key", "The quick brown fox jumps over the lazy dog"))
}
)--", "de7c9b85b8b78aa6bc8a7a36f70a90701c9db4d9\n");
}

TEST_F(SelfHostTest, HmacSha512KnownVector) {
    expectOutput(R"--(
import std::crypto
func main() {
    println(hmacSha512("key", "The quick brown fox jumps over the lazy dog"))
}
)--",
        "b42af09057bac1e2d41708e48a902e09b5ff7f12ab428a4fe86653c73dd248fb"
        "82f948a549f7b791a5b41915ee4d1ec3935357e4e2317250d0372afa2ebeeb3a\n");
}

TEST_F(SelfHostTest, Base64UrlRoundTrip) {
    // RFC 4648 §10: "Hello World" -> "SGVsbG8gV29ybGQ=" (std);
    // base64url drops padding -> "SGVsbG8gV29ybGQ".
    expectOutput(R"--(
import std::crypto
func main() {
    let e: string = base64UrlEncode("Hello World")
    println(e)
    if let d = base64UrlDecode(e) {
        println(d)
    }
}
)--", "SGVsbG8gV29ybGQ\nHello World\n");
}

TEST_F(SelfHostTest, Base64UrlNoPaddingNoSlashOrPlus) {
    // Bytes 0xFB 0xFF would yield "+/" and "==" in std base64;
    // base64url should produce "-_" and no padding.
    expectOutput(R"--(
import std::crypto
func main() {
    if let raw = hexDecode("fbff") {
        println(base64UrlEncode(raw))
    }
}
)--", "-_8\n");
}

TEST_F(SelfHostTest, JwtSignHS256HeaderShape) {
    // Canonical header for HS256 JWT, base64url-encoded:
    //   {"alg":"HS256","typ":"JWT"} -> "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"
    expectOutput(R"--(
import jwt::jwt
func main() {
    let t = Jwt.signHS256("k", "{}")
    let s: string = t.toString()
    println(s.startsWith("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."))
}
)--", "true\n");
}

TEST_F(SelfHostTest, JwtRoundTripHS256) {
    expectOutput(R"--(
import jwt::jwt
func main() {
    let payload: string = "{\"sub\":\"alice\"}"
    let tok = Jwt.signHS256("topsecret", payload)
    if let got = Jwt.verifyHS256(tok.toString(), "topsecret") {
        println(got)
    } else {
        println("verify failed")
    }
}
)--", "{\"sub\":\"alice\"}\n");
}

TEST_F(SelfHostTest, JwtRoundTripHS512) {
    expectOutput(R"--(
import jwt::jwt
func main() {
    let tok = Jwt.signHS512("k", "{\"r\":1}")
    if let got = Jwt.verifyHS512(tok.toString(), "k") {
        println(got)
    }
}
)--", "{\"r\":1}\n");
}

TEST_F(SelfHostTest, JwtVerifyRejectsWrongSecret) {
    expectOutput(R"--(
import jwt::jwt
func main() {
    let tok = Jwt.signHS256("right", "{}")
    if let got = Jwt.verifyHS256(tok.toString(), "wrong") {
        println("accepted: " + got)
    } else {
        println("rejected")
    }
}
)--", "rejected\n");
}

TEST_F(SelfHostTest, IsoRoundTripUtc) {
    // 2026-01-15 00:00:00 UTC = 1768435200
    expectOutput(R"--(
import std::datetime
func main() {
    let s: string = isoFormatUtc(1768435200.0 as f64)
    println(s)
    if let t = isoParse(s) {
        println(t as i64)
    }
}
)--", "2026-01-15T00:00:00Z\n1768435200\n");
}

TEST_F(SelfHostTest, IsoParseTimezoneOffset) {
    // "2026-01-15T12:34:56+03:00" — UTC = 09:34:56 = 1768469696
    expectOutput(R"--(
import std::datetime
func main() {
    if let t = isoParse("2026-01-15T12:34:56+03:00") {
        println(t as i64)
    }
    if let t = isoParse("2026-01-15T12:34:56-05:30") {
        println(t as i64)
    }
}
)--", "1768469696\n1768500296\n");
}

TEST_F(SelfHostTest, IsoParseDateOnly) {
    expectOutput(R"--(
import std::datetime
func main() {
    if let t = isoParse("2026-01-15") {
        println(t as i64)
    }
}
)--", "1768435200\n");
}

TEST_F(SelfHostTest, IsoParseRejectsBogus) {
    expectOutput(R"--(
import std::datetime
func main() {
    if let t = isoParse("not a date") {
        println("accepted")
    } else {
        println("rejected")
    }
    if let t = isoParse("") {
        println("accepted")
    } else {
        println("rejected")
    }
}
)--", "rejected\nrejected\n");
}

// Regression: generic struct method monomorphization. Earlier the static
// dispatch path for `Stream.from(arr)` on a generic struct silently dropped
// the call, the monomorphized struct's [T] field used `ptr` instead of
// `%DynArray`, and structFieldTypeReprs_ never got populated with the
// substituted type (so `self.source.length` failed to resolve).
// Turbofish (`Name::<T>`) on a struct literal disambiguates the type args
// without relying on field-value inference, which only handles `var x: T`
// direct fields and falls over on `var x: [T]` containers.
TEST_F(SelfHostTest, RandUuidV7HasCorrectShape) {
    // UUID v7 layout (RFC 9562):
    //   xxxxxxxx-xxxx-7xxx-Vxxx-xxxxxxxxxxxx
    //                  ^      ^
    //              version=7  variant ∈ {8,9,a,b}
    expectOutput(R"--(
import std::random
func main() {
    let u: string = randUuidV7()
    println(u.length)
    let dash1: string = u.substring(8 as i64, 1 as i64)
    let dash2: string = u.substring(13 as i64, 1 as i64)
    let dash3: string = u.substring(18 as i64, 1 as i64)
    let dash4: string = u.substring(23 as i64, 1 as i64)
    println(dash1)
    println(dash2)
    println(dash3)
    println(dash4)
    let ver: string = u.substring(14 as i64, 1 as i64)
    println(ver)
}
)--", "36\n-\n-\n-\n-\n7\n");
}

// Two v7 UUIDs minted ~ms apart sort lexicographically by mint time —
// the whole point of v7. Also exercises the new string ordering operators
// (which previously did pointer compare).
TEST_F(SelfHostTest, RandUuidV7IsTimeOrdered) {
    expectOutput(R"--(
import std::random
import std::os
func main() {
    let a: string = randUuidV7()
    sleep(2)
    let b: string = randUuidV7()
    if a < b { println("ordered") } else { println("not ordered") }
}
)--", "ordered\n");
}

TEST_F(SelfHostTest, StringLessGreaterCompareLexicographically) {
    expectOutput(R"--(
func main() {
    if "abc" < "abd" { println("lt") }
    if "abd" > "abc" { println("gt") }
    if "abc" <= "abc" { println("le") }
    if "abc" >= "abc" { println("ge") }
    if !("xyz" < "abc") { println("nlt") }
}
)--", "lt\ngt\nle\nge\nnlt\n");
}

// Plain generic syntax without turbofish: `Stream<i64>{...}` and
// `Stream<i64>.from(...)`. Disambiguation is done via lexer-state
// save/restore + token-level lookahead: when an uppercase identifier
// is followed by `<`, the parser scans forward through balanced angle
// brackets and only commits to a type-arg list if the closing `>` is
// followed by `{` or `.`. Comparisons (`A < B`) restore and fall
// through to the operator parser.
// Regression: `if X < Y { ... }` where Y starts uppercase used to parse
// `Y { ... }` as a struct literal because the struct-literal heuristic
// (uppercase Name + l_brace) fired regardless of context. The if/while/
// for-iter parsers now set suppressStructLit_ around the condition
// expression; nested parens, brackets, and call args reset it so
// `f(Foo { x: 1 })` and `(Foo { x: 1 })` still work.
// Regression: `let g = (P{x:1})` and `let g = (makeP())` (paren-wrapped
// struct values) used to segfault in semantic analysis. visitGroupExpr
// constructed its resolved type via `makePrimitiveType(innerKind)`,
// which only builds the TypeRepr base — for non-primitive inner types
// it sliced away NamedTypeRepr's name field (and similar), and later
// downcasts read garbage. cloneTypeRepr keeps the concrete subclass.
// Regression: println of a bool (literal or comparison result) used to
// emit garbage like "1528307457" because the printf format was "%d" but
// the i1 vararg never got promoted to i32. Now: bool is run through
// liva_bool_to_str ("true"/"false") and printed as %s.
// Regression: hexDecode → base64UrlEncode round-trip on binary bytes
// without embedded NUL. Exercises the full chain that the JWT signing
// flow goes through and was broken in earlier sessions by a separate
// Optional<string> UAF (now fixed in fc57aef). Lock in the current
// behavior so a future regression here is caught immediately.
// [u8] byte-array path: explicit-length binary buffers that survive
// embedded NUL bytes (which liva_hex_decode → strlen would have
// truncated). Exercises strToBytes, bytesToStr, hexEncodeBytes,
// hexDecodeBytes, base64UrlEncodeBytes, base64UrlDecodeBytes.
// Method-level type parameters: `func map<U>(...)` inside an
// `impl Stream<T>` block. Both the struct's T and the method's U are
// inferred from arg types, mangled into the method's name
// (Stream_string_map_i64), and substituted independently. Different
// (T, U) instantiations produce distinct functions in the same program.
// Explicit method type args at the call site, both forms:
//   s.map::<U>(...)  — turbofish, unambiguous
//   s.map<U>(...)    — plain, disambiguated by lookahead (only commits
//                      when the closing `>` is followed by `(`)
// Inference still kicks in when no args are given.
TEST_F(SelfHostTest, ExplicitMethodTypeArgsAtCallSite) {
    expectOutput(R"--(
pub struct Stream<T> {
    var source: [T]
    var index: i64
}
impl Stream<T> {
    pub func from(arr: [T]) -> Stream<T> {
        return Stream<T> { source: arr, index: 0 as i64 }
    }
    pub func map<U>(ref self, f: (T) -> U) -> [U] {
        var result: [U] = []
        var i: i64 = self.index
        let n: i64 = self.source.length
        while i < n {
            let v: T = self.source[i]
            result.push(f(v))
            i = i + (1 as i64)
        }
        return result
    }
}

func main() {
    var arr: [string] = []
    arr.push("a")
    arr.push("bb")
    let s = Stream<string>.from(arr)
    let l1: [i64] = s.map::<i64>(|w: string| -> i64 { return w.length })
    for n in l1 { println(n) }
    let l2: [i64] = s.map<i64>(|w: string| -> i64 { return w.length })
    for n in l2 { println(n) }

    // Inference still works
    let l3: [i64] = s.map(|w: string| -> i64 { return w.length })
    for n in l3 { println(n) }

    // Comparison after member access must still parse
    let p: i64 = 5 as i64
    if p < (10 as i64) { println("less") }
}
)--", "1\n2\n1\n2\n1\n2\nless\n");
}

TEST_F(SelfHostTest, GenericMethodTypeParamMap) {
    expectOutput(R"--(
import stream::stream
func main() {
    var arr: [string] = []
    arr.push("a")
    arr.push("bb")
    arr.push("ccc")
    let s = Stream<string>.from(arr)

    // U = i64
    let lens: [i64] = s.map(|w: string| -> i64 { return w.length })
    for n in lens { println(n) }

    // U = string (uppercase)
    let upper: [string] = s.map(|w: string| -> string { return w.toUpper() })
    for u in upper { println(u) }

    // T = i64, U = string in the same program — separate monomorphization
    var nums: [i64] = []
    nums.push(10 as i64)
    nums.push(20 as i64)
    let ns = Stream<i64>.from(nums)
    let strs: [string] = ns.map(|x: i64| -> string { return "x" })
    for v in strs { println(v) }
}
)--", "1\n2\n3\nA\nBB\nCCC\nx\nx\n");
}

// gzip encode → decode round-trip. Encoder uses RFC 1951 stored
// (uncompressed) blocks wrapped in valid RFC 1952 framing, so output
// is parseable by any conformant gzip tool. Decoder currently handles
// only stored blocks — fixed/dynamic Huffman is a future addition.
TEST_F(SelfHostTest, GzipRoundTrip) {
    expectOutput(R"--(
import std::crypto
func main() {
    let s: string = "Hello, gzip!"
    let bytes: [u8] = strToBytes(s)
    let encoded: [u8] = gzipEncode(bytes)
    // Magic 1F 8B + CM=08 in the first three bytes of every gzip stream.
    println(encoded[0 as i64] as i64)
    println(encoded[1 as i64] as i64)
    println(encoded[2 as i64] as i64)
    if let decoded = gzipDecode(encoded) {
        println(bytesToStr(decoded))
    }
}
)--", "31\n-117\n8\nHello, gzip!\n");
}

TEST_F(SelfHostTest, GzipBinaryWithEmbeddedNul) {
    // Same payload that broke the strlen-based path earlier:
    // bytes { 0x00, 0xAB, 0xFF, 0x00 } survive the round trip because
    // the gzip path tracks length explicitly through [u8] / *out_len.
    expectOutput(R"--(
import std::crypto
func main() {
    var bin: [u8] = []
    bin.push(0x00 as u8)
    bin.push(0xAB as u8)
    bin.push(0xFF as u8)
    bin.push(0x00 as u8)
    let enc: [u8] = gzipEncode(bin)
    if let dec = gzipDecode(enc) {
        println(dec.length)
        for b in dec { println(b as i64) }
    }
}
)--", "4\n0\n171\n255\n0\n");
}

TEST_F(SelfHostTest, GzipRejectsInvalidInput) {
    expectOutput(R"--(
import std::crypto
func main() {
    var bad: [u8] = []
    bad.push(0xFF as u8)
    bad.push(0x00 as u8)
    if let r = gzipDecode(bad) { println("never") }
    else { println("rejected") }
}
)--", "rejected\n");
}

TEST_F(SelfHostTest, WebSocketConnectFailsOnBadUrl) {
    // Connect to a URL with invalid scheme — handle is 0, no socket opened.
    // Also exercises Optional return-type inference for static struct
    // methods: `let r = T.factory(...)` where the factory returns `T?`.
    expectOutput(R"--(
import websocket::websocket
func main() {
    let r = WebSocket.connect("not-a-ws-url")
    if let ws = r {
        println("never")
    } else {
        println("connect failed")
    }
}
)--", "connect failed\n");
}

TEST_F(SelfHostTest, FailableStaticFactoryInferredOptional) {
    // Regression: `let r = Foo.tryNew(...)` where tryNew returns `Foo?` was
    // allocating a plain Foo slot and silently dropping the discriminant,
    // so the subsequent if-let saw a non-Optional alloca and bailed.
    expectOutput(R"--(
pub struct Foo {
    var x: i32
}

impl Foo {
    pub func tryNew(n: i32) -> Foo? {
        if n < 0 { return nil }
        return Foo { x: n }
    }
}

func main() {
    let ok = Foo.tryNew(7)
    if let f = ok { println(f.x) } else { println("nil") }
    let bad = Foo.tryNew(-1)
    if let f = bad { println("never") } else { println("nil") }
}
)--", "7\nnil\n");
}

TEST_F(SelfHostTest, WebSocketConnectFailsOnUnreachableHost) {
    // 127.0.0.1:1 — nothing listening, connect must fail cleanly (no crash).
    expectOutput(R"--(
import websocket::websocket
func main() {
    let r: WebSocket? = WebSocket.connect("ws://127.0.0.1:1/")
    if let ws = r {
        println("never")
    } else {
        println("unreachable")
    }
}
)--", "unreachable\n");
}

TEST_F(SelfHostTest, GzipCompressesRedundantData) {
    // 1000 identical bytes must compress to far less than 1000.
    // Stored-only deflate would emit ~1005 bytes; fixed Huffman + LZ77
    // should produce well under 100.
    expectOutput(R"--(
import std::crypto
func main() {
    var data: [u8] = []
    var i: i64 = 0 as i64
    while i < 1000 as i64 {
        data.push(65 as u8)
        i = i + 1 as i64
    }
    let enc: [u8] = gzipEncode(data)
    if enc.length < 100 as i64 { println("compressed") } else { println("not compressed") }
    if let dec = gzipDecode(enc) {
        println(dec.length)
        println(dec[0 as i64] as i64)
        println(dec[999 as i64] as i64)
    } else {
        println("decode failed")
    }
}
)--", "compressed\n1000\n65\n65\n");
}

TEST_F(SelfHostTest, U8ByteArrayPreservesEmbeddedNul) {
    expectOutput(R"--(
import std::crypto
func main() {
    // 0x00 0xAB 0xFF — embedded NUL would truncate the strlen-based
    // hexDecode result to length 0. The bytes path keeps all three.
    let opt: [u8]? = hexDecodeBytes("00abff")
    if let bytes = opt {
        println(bytes.length)
        for b in bytes { println(b as i64) }
        println(hexEncodeBytes(bytes))
        println(base64UrlEncodeBytes(bytes))
    }
    if let bad = hexDecodeBytes("zz") {
        println("never")
    } else {
        println("rejected")
    }
}
)--", "3\n0\n171\n255\n00abff\nAKv_\nrejected\n");
}

TEST_F(SelfHostTest, U8ArrayBasicOps) {
    // Regression for the [u8] allocation overflow false-positive:
    // `(int64_t)(SIZE_MAX / elem_size)` wrapped to -1 for elem_size 1.
    expectOutput(R"--(
func main() {
    var arr: [u8] = []
    arr.push(0 as u8)
    arr.push(255 as u8)
    arr.push(127 as u8)
    println(arr.length)
    for b in arr { println(b as i64) }
}
)--", "3\n0\n255\n127\n");
}

TEST_F(SelfHostTest, StrToBytesRoundTrip) {
    expectOutput(R"--(
import std::crypto
func main() {
    let s: string = "Hi"
    let bytes: [u8] = strToBytes(s)
    println(bytes.length)
    println(bytes[0 as i64] as i64)
    println(bytes[1 as i64] as i64)
    println(bytesToStr(bytes))
}
)--", "2\n72\n105\nHi\n");
}

TEST_F(SelfHostTest, HexDecodeBase64UrlBinaryRoundTrip) {
    expectOutput(R"--(
import std::crypto
func main() {
    // 0xFB 0xFF — high bytes, no NUL → base64url "-_8"
    let opt: string? = hexDecode("fbff")
    if let s = opt {
        println(base64UrlEncode(s))
    }
    // 0xAB 0xCD 0xEF → base64url "q83v"
    let opt2: string? = hexDecode("abcdef")
    if let s = opt2 {
        println(base64UrlEncode(s))
    }
    // ASCII "Hello" via hex
    let opt3: string? = hexDecode("48656c6c6f")
    if let s = opt3 {
        println(s)
        println(base64UrlEncode(s))
    }
}
)--", "-_8\nq83v\nHello\nSGVsbG8\n");
}

TEST_F(SelfHostTest, PrintBoolLiteralAndComparison) {
    expectOutput(R"--(
func main() {
    let a: i64 = 5 as i64
    let b: i64 = 10 as i64
    let r1: bool = a < b
    println(r1)
    println(a > b)
    println(true)
    println(false)
}
)--", "true\nfalse\ntrue\nfalse\n");
}

TEST_F(SelfHostTest, ParenWrappedStructValueDoesNotCrash) {
    expectOutput(R"--(
struct P {
    var x: i64
}
pub func make() -> P {
    return P { x: 7 as i64 }
}
func main() {
    let a: P = (P { x: 1 as i64 })
    println(a.x)
    let b: P = (make())
    println(b.x)
    let c: P = ((P { x: 5 as i64 }))
    println(c.x)
    println((P { x: 9 as i64 }).x)
}
)--", "1\n7\n5\n9\n");
}

TEST_F(SelfHostTest, IfConditionWithUppercaseRhsCmp) {
    expectOutput(R"--(
func main() {
    let A: i64 = 3 as i64
    let B: i64 = 5 as i64
    if A < B { println("less") }

    var k: i64 = 0 as i64
    while k < B {
        k = k + (1 as i64)
    }
    println(k)
}
)--", "less\n5\n");
}

TEST_F(SelfHostTest, StructLiteralStillWorksOutsideConditions) {
    // Ensure the struct-literal flag is correctly reset inside call args
    // and array element expressions even when the call is itself inside a
    // condition (struct literal nested into the call should be fine).
    expectOutput(R"--(
struct P {
    var x: i64
}
pub func take(p: P) -> i64 {
    return p.x
}
func main() {
    let p1 = P { x: 1 as i64 }
    println(p1.x)
    println(take(P { x: 2 as i64 }))
    var arr: [i64] = []
    arr.push(P { x: 4 as i64 }.x)
    for v in arr { println(v) }
}
)--", "1\n2\n4\n");
}

TEST_F(SelfHostTest, PlainGenericStructLiteralAndStaticCall) {
    expectOutput(R"--(
pub struct Stream<T> {
    var source: [T]
    var index: i64
}
impl Stream<T> {
    pub func from(arr: [T]) -> Stream<T> {
        return Stream<T> { source: arr, index: 0 as i64 }
    }
    pub func count(ref self) -> i64 {
        return self.source.length - self.index
    }
}
func main() {
    var arr: [i64] = []
    arr.push(1 as i64)
    arr.push(2 as i64)
    arr.push(3 as i64)
    let s = Stream<i64>.from(arr)
    println(s.count())

    var arr2: [i64] = []
    arr2.push(99 as i64)
    let s2 = Stream<i64> { source: arr2, index: 0 as i64 }
    println(s2.source.length)
}
)--", "3\n1\n");
}

TEST_F(SelfHostTest, TurbofishStructLiteralExplicitTypeArgs) {
    expectOutput(R"--(
pub struct Box<T> {
    var value: T
}

func main() {
    let bi = Box::<i64> { value: 42 as i64 }
    println(bi.value)
    let bs = Box::<string> { value: "hi" }
    println(bs.value)
}
)--", "42\nhi\n");
}

TEST_F(SelfHostTest, TurbofishStaticMethodCall) {
    expectOutput(R"--(
pub struct Stream<T> {
    var source: [T]
    var index: i64
}
impl Stream<T> {
    pub func from(arr: [T]) -> Stream<T> {
        return Stream { source: arr, index: 0 as i64 }
    }
    pub func count(ref self) -> i64 {
        return self.source.length - self.index
    }
}
func main() {
    var arr: [i64] = []
    arr.push(1 as i64)
    arr.push(2 as i64)
    let s = Stream::<i64>.from(arr)
    println(s.count())
}
)--", "2\n");
}

TEST_F(SelfHostTest, GenericStreamMonomorphizesPerElementType) {
    expectOutput(R"--(
import stream::stream
func main() {
    var nums: [i64] = []
    nums.push(1 as i64)
    nums.push(2 as i64)
    nums.push(3 as i64)
    let s = Stream.from(nums)
    println(s.count())
    let evens: [i64] = s.filter(|x: i64| -> bool { return x % (2 as i64) == (0 as i64) })
    for e in evens { println(e) }

    var words: [string] = []
    words.push("apple")
    words.push("bee")
    let ws = Stream.from(words)
    let lens: [i64] = ws.mapToInt(|w: string| -> i64 { return w.length })
    for n in lens { println(n) }
}
)--", "3\n2\n5\n3\n");
}

TEST_F(SelfHostTest, StringStreamFilterAndMap) {
    expectOutput(R"--(
import stream::stream
func main() {
    var w: [string] = []
    w.push("a")
    w.push("bb")
    w.push("ccc")
    let s = StringStream.from(w)
    let longs: [string] = s.filter(|x: string| -> bool { return x.length > (1 as i64) })
    println(longs.length)
    let lens: [i64] = s.mapToInt(|x: string| -> i64 { return x.length })
    for n in lens { println(n) }
}
)--", "2\n1\n2\n3\n");
}

TEST_F(SelfHostTest, IntStreamReduceSumProductMax) {
    expectOutput(R"--(
import stream::stream
func main() {
    var nums: [i64] = []
    nums.push(1 as i64)
    nums.push(2 as i64)
    nums.push(3 as i64)
    nums.push(4 as i64)
    nums.push(5 as i64)
    let s = IntStream.from(nums)
    println(s.sum())
    let prod: i64 = s.reduce(1 as i64, |a: i64, b: i64| -> i64 { return a * b })
    println(prod)
    let mx: i64 = s.reduce(0 as i64, |a: i64, b: i64| -> i64 {
        if a > b { return a }
        return b
    })
    println(mx)
}
)--", "15\n120\n5\n");
}

TEST_F(SelfHostTest, StreamNextAndCollect) {
    expectOutput(R"--(
import stream::stream
func main() {
    var arr: [string] = []
    arr.push("x")
    arr.push("y")
    arr.push("z")
    var s = StringStream.from(arr)
    if let v = s.next() { println(v) }
    let rest: [string] = s.collect()
    println(rest.length)
    for r in rest { println(r) }
}
)--", "x\n2\ny\nz\n");
}

TEST_F(SelfHostTest, CsvParsePlainRow) {
    expectOutput(R"--(
import csv::csv
func main() {
    let r: [string] = csvParseRow("a,b,c")
    println(r.length)
    for f in r { println(f) }
}
)--", "3\na\nb\nc\n");
}

TEST_F(SelfHostTest, CsvParseQuotedFields) {
    // Quoted field with comma inside; doubled quote → literal "
    expectOutput(R"--(
import csv::csv
func main() {
    let r1: [string] = csvParseRow("x,\"y,z\",w")
    for f in r1 { println(f) }
    let r2: [string] = csvParseRow("\"a\"\"b\",c")
    for f in r2 { println(f) }
}
)--", "x\ny,z\nw\na\"b\nc\n");
}

TEST_F(SelfHostTest, CsvParseEmptyFields) {
    expectOutput(R"--(
import csv::csv
func main() {
    let r: [string] = csvParseRow("a,,b")
    println(r.length)
    for f in r { println("[" + f + "]") }
}
)--", "3\n[a]\n[]\n[b]\n");
}

TEST_F(SelfHostTest, CsvJoinAndRoundTrip) {
    expectOutput(R"--(
import csv::csv
func main() {
    var fields: [string] = []
    fields.push("x")
    fields.push("y, z")
    fields.push("a\"b")
    let line: string = csvJoinRow(fields)
    println(line)
    let back: [string] = csvParseRow(line)
    for f in back { println(f) }
}
)--", "x,\"y, z\",\"a\"\"b\"\nx\ny, z\na\"b\n");
}

TEST_F(SelfHostTest, ConstTimeEqMatchAndMismatch) {
    expectOutput(R"--(
import std::crypto
func main() {
    if constTimeEq("abc", "abc") { println("eq") } else { println("ne") }
    if constTimeEq("abc", "abd") { println("eq") } else { println("ne") }
    if constTimeEq("abc", "abcd") { println("eq") } else { println("ne") }
    if constTimeEq("", "") { println("eq") } else { println("ne") }
}
)--", "eq\nne\nne\neq\n");
}

// Regression: Liva-defined functions could not return [T] DynArrays — the
// return type used `ptr` instead of the struct, so the caller got 8 bytes
// (data) and zeros for length/capacity. Also, returning a local DynArray
// var triggered emitScopeCleanup → liva_array_free, freeing the buffer the
// caller had just received.
TEST_F(SelfHostTest, FunctionReturningDynArray) {
    expectOutput(R"--(
pub func makeArr() -> [string] {
    var arr: [string] = []
    arr.push("a")
    arr.push("b")
    return arr
}
func main() {
    let r: [string] = makeArr()
    println(r.length)
    for f in r { println(f) }
}
)--", "2\na\nb\n");
}

// Regression: a [T] parameter is borrowed — the caller owns the data buffer.
// emitScopeCleanup used to free it inside the callee, corrupting caller
// state on use of the array after the call.
TEST_F(SelfHostTest, FunctionTakingDynArrayParam) {
    expectOutput(R"--(
pub func sumLengths(parts: [string]) -> i64 {
    var total: i64 = 0 as i64
    for p in parts {
        total = total + p.length
    }
    return total
}
func main() {
    var arr: [string] = []
    arr.push("ab")
    arr.push("cde")
    println(sumLengths(arr))
    println(arr.length)
}
)--", "5\n2\n");
}

// Regression: `var s: string = literal` followed by a reassignment to a
// concat result freed the string literal (heap corruption). Initializing a
// mutable string variable now stores a heap copy so reassignments can free
// the old value uniformly.
TEST_F(SelfHostTest, MutableStringVarConcatLoop) {
    expectOutput(R"--(
import std::strings
import std::convert
func main() {
    var s: string = ""
    var i: i32 = 0
    while i < 3 {
        s = s + "x"
        i = i + 1
    }
    println(s)
}
)--", "xxx\n");
}

// Regression: strSplit returned an empty array when bound with an explicit
// `[T]` annotation because the array branch in IRGenDecl ignored non-literal
// initializers and allocated a fresh empty DynArray.
TEST_F(SelfHostTest, StrSplitWithAnnotationReturnsParts) {
    expectOutput(R"--(
import std::strings
func main() {
    let parts: [string] = strSplit("a.b.c", ".")
    println(parts.length)
    for p in parts {
        println(p)
    }
}
)--", "3\na\nb\nc\n");
}

// Regression: returning a struct literal whose field is initialized from a
// tracked-temp string call (e.g. `return Wrapper { s: strToUpper(x) }`)
// emitted free() for the temp *after* the ret terminator, tripping LLVM
// verifier's "Terminator in middle of basic block".
TEST_F(SelfHostTest, StructLiteralWithTrackedTempCompiles) {
    expectOutput(R"--(
import std::strings
struct Wrapper {
    var s: string
}
impl Wrapper {
    pub func make(s: string) -> Wrapper {
        return Wrapper { s: strToUpper(s) }
    }
}
func main() {
    let w = Wrapper.make("hello")
    println(w.s)
}
)--", "HELLO\n");
}

// Regression: `let opt: string? = hexDecode(...)` then `if let b = opt`
// previously produced garbage because the inner ptr was tracked as a temp
// and freed at end of the producing statement, leaving the variable holding
// a dangling pointer (use-after-free).
TEST_F(SelfHostTest, OptionalStringFromBuiltinSurvivesScope) {
    expectOutput(R"--(
import std::crypto
func main() {
    let opt: string? = hexDecode("616263")
    if let b = opt {
        println(b)
    }
}
)--", "abc\n");
}

TEST_F(SelfHostTest, IsoFractionalSeconds) {
    // Accept .fff fractional component (truncated to whole seconds).
    expectOutput(R"--(
import std::datetime
func main() {
    if let t = isoParse("2026-01-15T12:34:56.789Z") {
        println(t as i64)
    }
}
)--", "1768480496\n");
}

TEST_F(SelfHostTest, JwtVerifyRejectsMalformed) {
    expectOutput(R"--(
import jwt::jwt
func main() {
    if let got = Jwt.verifyHS256("not.a.jwt.token", "k") {
        println("accepted")
    } else {
        println("rejected")
    }
    if let got = Jwt.verifyHS256("nodots", "k") {
        println("accepted")
    } else {
        println("rejected")
    }
}
)--", "rejected\nrejected\n");
}

TEST_F(SelfHostTest, TomlMissingKeysReturnNil) {
    expectOutput(R"--(
import toml::toml
func main() {
    var doc = TomlDocument.parse("[a]\nx = 1")
    var hadStr: bool = false
    if let s = doc.getString("a", "missing") {
        hadStr = true
    }
    if !hadStr {
        println("no string")
    }
    var hadInt: bool = false
    if let i = doc.getInt("a", "missing") {
        hadInt = true
    }
    if !hadInt {
        println("no int")
    }
    doc.free()
}
)--", "no string\nno int\n");
}

TEST_F(SelfHostTest, NestedIf) {
    expectOutput(R"--(
func main() {
    let x: i32 = 15
    if x > 10 {
        if x > 20 {
            println("very big")
        } else {
            println("medium")
        }
    } else {
        println("small")
    }
}
)--", "medium\n");
}

// ============================================================
// 4. Functions
// ============================================================

TEST_F(SelfHostTest, SimpleFunction) {
    expectOutput(R"--(
func add(a: i32, b: i32) -> i32 {
    return a + b
}
func main() {
    println(add(3, 7))
}
)--", "10\n");
}

TEST_F(SelfHostTest, Recursion) {
    expectOutput(R"--(
func factorial(n: i32) -> i32 {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}
func main() {
    println(factorial(5))
}
)--", "120\n");
}

TEST_F(SelfHostTest, MultipleReturns) {
    expectOutput(R"--(
func classify(n: i32) -> string {
    if n < 0 {
        return "negative"
    }
    if n == 0 {
        return "zero"
    }
    return "positive"
}
func main() {
    println(classify(-1))
    println(classify(0))
    println(classify(1))
}
)--", "negative\nzero\npositive\n");
}

// ============================================================
// 5. Data Structures
// ============================================================

TEST_F(SelfHostTest, Structs) {
    expectOutput(R"--(
struct Point {
    var x: i32
    var y: i32
}
func main() {
    let p = Point { x: 3, y: 4 }
    println(p.x)
    println(p.y)
}
)--", "3\n4\n");
}

TEST_F(SelfHostTest, Enums) {
    expectOutput(R"--(
enum Shape {
    case Circle(f64)
    case Rectangle(f64, f64)
}
func main() {
    let s = Shape.Circle(3.14)
    println("created")
}
)--", "created\n");
}

TEST_F(SelfHostTest, Arrays) {
    expectOutput(R"--(
func main() {
    var arr: [i32] = [1, 2, 3, 4, 5]
    println(arr.length)
}
)--", "5\n");
}

TEST_F(SelfHostTest, StringOps) {
    expectOutput(R"--(
func main() {
    let a = "Hello"
    let b = ", World!"
    let c = a + b
    println(c)
    println(c.length)
}
)--", "Hello, World!\n13\n");
}

// ============================================================
// 6. Closures & Generics
// ============================================================

TEST_F(SelfHostTest, Closures) {
    expectOutput(R"--(
func transform(x: i32, f: (i32) -> i32) -> i32 {
    return f(x)
}
func main() {
    let add = |x: i32| -> i32 { return x + 100 }
    let result = transform(5, add)
    println(result)
}
)--", "105\n");
}

TEST_F(SelfHostTest, Generics) {
    expectOutput(R"--(
func identity<T>(x: T) -> T {
    return x
}
func main() {
    println(identity(42))
    println(identity("hello"))
}
)--", "42\nhello\n");
}

TEST_F(SelfHostTest, HigherOrder) {
    expectOutput(R"--(
func apply(x: i32, f: (i32) -> i32) -> i32 {
    return f(x)
}
func main() {
    let double = |n: i32| -> i32 { return n * 2 }
    println(apply(5, double))
}
)--", "10\n");
}

// ============================================================
// 7. Compile Error Tests
// ============================================================

TEST_F(SelfHostTest, CompileErrorUndefinedVar) {
    expectCompileFail(R"--(
func main() {
    println(undefined_var)
}
)--");
}

TEST_F(SelfHostTest, CompileErrorTypeMismatch) {
    expectCompileFail(R"--(
func main() {
    let x: i32 = "not a number"
}
)--");
}

// ============================================================
// 8. Additional Integration
// ============================================================

TEST_F(SelfHostTest, StructMethod) {
    expectOutput(R"--(
struct Counter {
    var count: i32
}
impl Counter {
    func increment(ref mut self) {
        self.count = self.count + 1
    }
    func value(ref self) -> i32 {
        return self.count
    }
}
func main() {
    var c = Counter { count: 0 }
    c.increment()
    c.increment()
    c.increment()
    println(c.value())
}
)--", "3\n");
}

TEST_F(SelfHostTest, FibonacciIterative) {
    expectOutput(R"--(
func fib(n: i32) -> i32 {
    if n <= 1 { return n }
    var a: i32 = 0
    var b: i32 = 1
    var i: i32 = 2
    while i <= n {
        let tmp: i32 = a + b
        a = b
        b = tmp
        i = i + 1
    }
    return b
}
func main() {
    println(fib(10))
}
)--", "55\n");
}

TEST_F(SelfHostTest, TernaryExpression) {
    expectOutput(R"--(
func main() {
    let x: i32 = 10
    let result = x > 5 ? "yes" : "no"
    println(result)
}
)--", "yes\n");
}

TEST_F(SelfHostTest, DynProtocolDispatch) {
    expectOutput(R"--(
protocol Shape {
    func area(self) -> f64
}
struct Circle {
    let radius: f64
}
impl Circle: Shape {
    func area(self) -> f64 {
        return self.radius * self.radius * 3.14
    }
}
func printArea(s: dyn Shape) {
    println(s.area())
}
func main() {
    let c = Circle { radius: 5.0 }
    printArea(c)
}
)--", "78.500000\n");
}

TEST_F(SelfHostTest, DynProtocolMultipleTypes) {
    expectOutput(R"--(
protocol Greeter {
    func greet(self) -> i32
}
struct Hello {
    let x: i32
}
struct World {
    let y: i32
}
impl Hello: Greeter {
    func greet(self) -> i32 {
        return self.x
    }
}
impl World: Greeter {
    func greet(self) -> i32 {
        return self.y
    }
}
func showGreet(g: dyn Greeter) {
    println(g.greet())
}
func main() {
    let h = Hello { x: 10 }
    let w = World { y: 20 }
    showGreet(h)
    showGreet(w)
}
)--", "10\n20\n");
}

// ============================================================
// 8b. Devirtualization (compile-time direct call optimization)
// ============================================================

TEST_F(SelfHostTest, DevirtualizationLetBinding) {
    expectOutput(R"--(
protocol Shape {
    func area(self) -> f64
}
struct Circle {
    let radius: f64
}
impl Circle: Shape {
    func area(self) -> f64 {
        return self.radius * self.radius * 3.14
    }
}
func main() {
    let c = Circle { radius: 5.0 }
    let s: dyn Shape = c
    println(s.area())
}
)--", "78.500000\n");
}

TEST_F(SelfHostTest, DevirtualizationMultipleMethods) {
    expectOutput(R"--(
protocol Animal {
    func speak(self) -> i32
    func legs(self) -> i32
}
struct Dog {
    let x: i32
}
impl Dog: Animal {
    func speak(self) -> i32 {
        return 42
    }
    func legs(self) -> i32 {
        return 4
    }
}
func main() {
    let d = Dog { x: 1 }
    let a: dyn Animal = d
    println(a.speak())
    println(a.legs())
}
)--", "42\n4\n");
}

TEST_F(SelfHostTest, DevirtualizationWithParamFallback) {
    expectOutput(R"--(
protocol Shape {
    func area(self) -> f64
}
struct Circle {
    let radius: f64
}
impl Circle: Shape {
    func area(self) -> f64 {
        return self.radius * self.radius * 3.14
    }
}
func printArea(s: dyn Shape) {
    println(s.area())
}
func main() {
    let c = Circle { radius: 5.0 }
    let s: dyn Shape = c
    println(s.area())
    printArea(c)
}
)--", "78.500000\n78.500000\n");
}

// ============================================================
// 9. Separate Compilation (--emit-obj + link)
// ============================================================

TEST_F(SelfHostTest, EmitObjLegacy) {
    // Write a temp .liva file
    std::string livaFile = buildDir_ + "/_emitobj_test.liva";
    std::string objFile = buildDir_ + "/_emitobj_test.o";
    {
        std::ofstream f(livaFile);
        f << "func main() {\n    println(\"emit-obj works\")\n}\n";
    }

    // Use Driver with --emit-obj
    const char *args[] = {"livac", "--emit-obj", livaFile.c_str(), "-o", objFile.c_str()};
    liva::Driver driver;
    ASSERT_TRUE(driver.parseArgs(5, args));
    int ret = driver.execute();
    EXPECT_EQ(ret, 0);

    // Check .o file was created
    std::ifstream check(objFile);
    EXPECT_TRUE(check.is_open()) << "Object file not created: " << objFile;
    check.close();

    // Cleanup
    std::remove(livaFile.c_str());
    std::remove(objFile.c_str());
}

TEST_F(SelfHostTest, EmitObjNoMain) {
    // A file without main() should also emit .o successfully
    std::string livaFile = buildDir_ + "/_emitobj_nomain.liva";
    std::string objFile = buildDir_ + "/_emitobj_nomain.o";
    {
        std::ofstream f(livaFile);
        f << "func helper() -> i32 {\n    return 42\n}\n";
    }

    const char *args[] = {"livac", "--emit-obj", livaFile.c_str(), "-o", objFile.c_str()};
    liva::Driver driver;
    ASSERT_TRUE(driver.parseArgs(5, args));
    int ret = driver.execute();
    EXPECT_EQ(ret, 0);

    std::ifstream check(objFile);
    EXPECT_TRUE(check.is_open()) << "Object file not created: " << objFile;
    check.close();

    std::remove(livaFile.c_str());
    std::remove(objFile.c_str());
}

TEST_F(SelfHostTest, LinkSubcommand) {
    // First, compile a .liva to .o
    std::string livaFile = buildDir_ + "/_link_test.liva";
    std::string objFile = buildDir_ + "/_link_test.o";
    std::string exeFile = buildDir_ + "/_link_test";
#ifdef _WIN32
    exeFile += ".exe";
#endif
    {
        std::ofstream f(livaFile);
        f << "func main() {\n    println(\"linked ok\")\n}\n";
    }

    // Step 1: --emit-obj
    {
        const char *args[] = {"livac", "--emit-obj", livaFile.c_str(), "-o", objFile.c_str()};
        liva::Driver driver;
        ASSERT_TRUE(driver.parseArgs(5, args));
        ASSERT_EQ(driver.execute(), 0);
    }

    // Step 2: link
    {
        const char *args[] = {"livac", "link", objFile.c_str(), "-o", exeFile.c_str()};
        liva::Driver driver;
        ASSERT_TRUE(driver.parseArgs(5, args));
        int ret = driver.execute();
        EXPECT_EQ(ret, 0);

        std::ifstream check(exeFile);
        EXPECT_TRUE(check.is_open()) << "Executable not created: " << exeFile;
    }

    // Step 3: Run and verify output
    {
        std::string cmd = "\"" + exeFile + "\"" + " > " + "\"" + tmpOut_ + "\"" + " 2>&1";
#ifdef _WIN32
        cmd = "\"" + cmd + "\"";
#endif
        std::system(cmd.c_str());

        std::ifstream ifs(tmpOut_);
        std::stringstream ss;
        ss << ifs.rdbuf();
        EXPECT_EQ(ss.str(), "linked ok\n");
    }

    std::remove(livaFile.c_str());
    std::remove(objFile.c_str());
    std::remove(exeFile.c_str());
}

TEST_F(SelfHostTest, LinkSubcommandParseError) {
    // link with no files should fail
    const char *args[] = {"livac", "link"};
    liva::Driver driver;
    ASSERT_TRUE(driver.parseArgs(2, args));
    int ret = driver.execute();
    EXPECT_NE(ret, 0);
}

// Helper: capture stderr during a lambda execution
static std::string captureStderr(std::function<void()> fn) {
    // Create a temp file for capturing stderr
    std::string tmpPath;
#ifdef _WIN32
    char tmpBuf[L_tmpnam];
    tmpnam(tmpBuf);
    tmpPath = tmpBuf;
#else
    tmpPath = "/tmp/_liva_stderr_capture.txt";
#endif

    fflush(stderr);
#ifdef _WIN32
    int origFd = _dup(_fileno(stderr));
    FILE *tmpFile = fopen(tmpPath.c_str(), "w");
    if (tmpFile) {
        _dup2(_fileno(tmpFile), _fileno(stderr));
        fclose(tmpFile);
    }
#else
    int origFd = dup(fileno(stderr));
    FILE *tmpFile = fopen(tmpPath.c_str(), "w");
    if (tmpFile) {
        dup2(fileno(tmpFile), fileno(stderr));
        fclose(tmpFile);
    }
#endif

    fn();

    fflush(stderr);
#ifdef _WIN32
    _dup2(origFd, _fileno(stderr));
    _close(origFd);
#else
    dup2(origFd, fileno(stderr));
    close(origFd);
#endif

    std::ifstream ifs(tmpPath);
    std::string result;
    if (ifs.is_open()) {
        std::stringstream ss;
        ss << ifs.rdbuf();
        result = ss.str();
    }
    std::remove(tmpPath.c_str());
    return result;
}

TEST_F(SelfHostTest, DumpTimingsOutput) {
    std::string captured;
    bool compiled = false;
    captured = captureStderr([&]() {
        liva::CompilerInstance compiler;
        compiler.setSource("_timing_test.liva", R"--(
func main() {
    println("hello")
}
)--");
#ifdef _WIN32
        compiler.setExecutablePath(buildDir_ + "/livac.exe");
#else
        compiler.setExecutablePath(buildDir_ + "/livac");
#endif
        compiler.setDumpTimings(true);
        compiled = compiler.compile(tmpExe_);
    });
    ASSERT_TRUE(compiled) << "Compilation failed";
    EXPECT_NE(captured.find("Compilation Timings"), std::string::npos)
        << "Missing 'Compilation Timings' in:\n" << captured;
    EXPECT_NE(captured.find("Parse:"), std::string::npos);
    EXPECT_NE(captured.find("Sema:"), std::string::npos);
    EXPECT_NE(captured.find("IRGen:"), std::string::npos);
    EXPECT_NE(captured.find("Total:"), std::string::npos);
}

TEST_F(SelfHostTest, MonomorphizationStatsOutput) {
    std::string captured;
    bool compiled = false;
    captured = captureStderr([&]() {
        liva::CompilerInstance compiler;
        compiler.setSource("_mono_test.liva", R"--(
struct Box<T> {
    let value: T
}
func identity<T>(x: T) -> T {
    return x
}
func main() {
    let a = identity(42)
    let b = identity(3.14)
    let box1 = Box { value: 10 }
    let box2 = Box { value: "hi" }
    println(a)
}
)--");
#ifdef _WIN32
        compiler.setExecutablePath(buildDir_ + "/livac.exe");
#else
        compiler.setExecutablePath(buildDir_ + "/livac");
#endif
        compiler.setDumpTimings(true);
        compiled = compiler.compile(tmpExe_);
    });
    ASSERT_TRUE(compiled) << "Compilation failed";
    EXPECT_NE(captured.find("Monomorphization:"), std::string::npos)
        << "Missing 'Monomorphization:' in:\n" << captured;
    EXPECT_NE(captured.find("Functions:"), std::string::npos);
}

// === Slice Bounds Check Tests ===

TEST_F(SelfHostTest, SliceNegativeStartPanics) {
    std::string output = compileAndRun(R"--(
        func main() {
            var arr: [I32] = [1, 2, 3]
            var s = arr[-1..2]
        }
    )--");
    EXPECT_NE(output.find("slice start index out of bounds"), std::string::npos)
        << "Expected panic for negative slice start, got: " << output;
}

TEST_F(SelfHostTest, SliceEndBeforeStartPanics) {
    std::string output = compileAndRun(R"--(
        func main() {
            var arr: [I32] = [1, 2, 3]
            var s = arr[2..1]
        }
    )--");
    EXPECT_NE(output.find("slice end index less than start"), std::string::npos)
        << "Expected panic for end < start, got: " << output;
}

TEST_F(SelfHostTest, SliceEndBeyondLenPanics) {
    std::string output = compileAndRun(R"--(
        func main() {
            var arr: [I32] = [1, 2, 3]
            var s = arr[0..10]
        }
    )--");
    EXPECT_NE(output.find("slice end index out of bounds"), std::string::npos)
        << "Expected panic for end > len, got: " << output;
}

extern "C" int8_t liva_str_parse_i64(const char *, int64_t *);
extern "C" int8_t liva_str_parse_f64(const char *, double *);

TEST_F(SelfHostTest, ParseI64OverflowReturnsZero) {
    // Test runtime function directly — overflow must return 0 (failure)
    int64_t val = -1;
    EXPECT_EQ(liva_str_parse_i64("99999999999999999999", &val), 0)
        << "i64 overflow should return 0";
    EXPECT_EQ(liva_str_parse_i64("-99999999999999999999", &val), 0)
        << "i64 underflow should return 0";
    EXPECT_EQ(liva_str_parse_i64("42", &val), 1);
    EXPECT_EQ(val, 42);

    double dval = -1.0;
    EXPECT_EQ(liva_str_parse_f64("1e999", &dval), 0)
        << "f64 overflow should return 0";
    EXPECT_EQ(liva_str_parse_f64("3.14", &dval), 1);
    EXPECT_DOUBLE_EQ(dval, 3.14);
}

// Include runtime header for LivaTask struct and function declarations
#include "../../stdlib/runtime/runtime.h"

// === Async Runtime: CV Wakeup, Child Cancel, Select, WithTimeout ===

TEST_F(SelfHostTest, ChannelCVWakeupRuntime) {
    // Test that channel CV wakeup functions link correctly
    // (direct C++ runtime test — no Liva compilation)
    int64_t ch = liva_channel_create(2);
    ASSERT_NE(ch, 0);
    liva_channel_send(ch, 10);
    liva_channel_send(ch, 20);
    int8_t ok1 = 0, ok2 = 0;
    int64_t v1 = liva_channel_receive(ch, &ok1);
    int64_t v2 = liva_channel_receive(ch, &ok2);
    EXPECT_EQ(ok1, 1);
    EXPECT_EQ(ok2, 1);
    EXPECT_EQ(v1, 10);
    EXPECT_EQ(v2, 20);
    liva_channel_close(ch);
    liva_channel_free(ch);
}

TEST_F(SelfHostTest, ChildCancellationPropagation) {
    // Test that cancellation propagates to children (direct runtime test)
    LivaTask *parent = liva_task_create(nullptr);
    LivaTask *child1 = liva_task_create(nullptr);
    LivaTask *child2 = liva_task_create(nullptr);
    liva_task_set_parent(child1, parent);
    liva_task_set_parent(child2, parent);
    EXPECT_EQ(parent->child_count, 2);
    EXPECT_EQ(child1->cancelled, 0);
    EXPECT_EQ(child2->cancelled, 0);
    // Cancel parent — should propagate to children
    liva_task_cancel(parent);
    EXPECT_EQ(parent->cancelled, 1);
    EXPECT_EQ(child1->cancelled, 1);
    EXPECT_EQ(child2->cancelled, 1);
    liva_task_destroy(child1);
    liva_task_destroy(child2);
    liva_task_destroy(parent);
}

TEST_F(SelfHostTest, TaskSelectReturnsFirstDone) {
    // Test taskSelect returns index of first completed task
    LivaTask *t1 = liva_task_create(nullptr);
    LivaTask *t2 = liva_task_create(nullptr);
    t1->done = 0;
    t2->done = 1;  // t2 is already done
    LivaTask *tasks[2] = {t1, t2};
    int64_t idx = liva_task_select(tasks, 2);
    EXPECT_EQ(idx, 1);  // t2 was done
    liva_task_destroy(t1);
    liva_task_destroy(t2);
}

TEST_F(SelfHostTest, WithTimeoutCompletedTask) {
    // Test withTimeout on an already-completed task returns 1
    LivaTask *t = liva_task_create(nullptr);
    t->done = 1;
    int8_t result = liva_task_with_timeout(t, 100);
    EXPECT_EQ(result, 1);
    liva_task_destroy(t);
}

// === Thread Pool & Async I/O Runtime Tests ===

TEST_F(SelfHostTest, ThreadPoolInitShutdown) {
    liva_scheduler_init(2);
    EXPECT_EQ(liva_scheduler_worker_count(), 2);
    liva_scheduler_shutdown();
    EXPECT_EQ(liva_scheduler_worker_count(), 0);
}

TEST_F(SelfHostTest, ThreadPoolAutoDetect) {
    liva_scheduler_init(0);  // auto-detect
    EXPECT_GE(liva_scheduler_worker_count(), 1);
    liva_scheduler_shutdown();
}

TEST_F(SelfHostTest, AsyncFileReadWrite) {
    // Write a temp file, then read it back
    std::string tmpFile = buildDir_ + "/_async_test.txt";
    int8_t writeOk = liva_async_file_write(tmpFile.c_str(), "hello async");
    EXPECT_EQ(writeOk, 1);
    char *content = liva_async_file_read(tmpFile.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "hello async");
    free(content);
    std::remove(tmpFile.c_str());
}

TEST_F(SelfHostTest, AsyncFileReadNonExistent) {
    char *content = liva_async_file_read("__nonexistent_file_12345__");
    EXPECT_EQ(content, nullptr);
}

#endif // LIVA_HAS_LLVM

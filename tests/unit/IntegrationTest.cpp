#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Driver/Driver.h"
#include "liva/Driver/ProjectConfig.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#ifdef LIVA_HAS_LLVM
#include "liva/CodeGen/TargetInfo.h"
#include "liva/Driver/CompilerInstance.h"
#include "liva/JIT/JITEngine.h"
#endif
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace liva;

// ============================================================
// Integration Test Infrastructure
//
// Tests parse .liva files from tests/integration/ and tests/error/
// through the full pipeline (Lexer → Parser → Sema) to verify
// that valid programs pass and error programs produce diagnostics.
// ============================================================

namespace {

// Read file contents into a string
static bool readFile(const std::string &path, std::string &out) {
    std::ifstream ifs(path);
    if (!ifs.is_open())
        return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

// Extract expected error substring from "// Expected error: ..." comment
static std::string extractExpectedError(const std::string &content) {
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        size_t pos = line.find("// Expected error:");
        if (pos != std::string::npos) {
            std::string err = line.substr(pos + 18);
            size_t start = err.find_first_not_of(" \t");
            if (start != std::string::npos)
                return err.substr(start);
        }
    }
    return "";
}

// Run the full frontend pipeline (Lex → Parse → Sema) and return diagnostics
struct PipelineResult {
    bool parseSuccess = false;
    bool semaSuccess = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

static PipelineResult runPipeline(const std::string &filename,
                                  const std::string &source) {
    PipelineResult result;

    SourceManager sm(filename, source);
    DiagnosticsEngine diag(&sm);

    Lexer lexer(sm, diag);
    Parser parser(lexer, diag);
    auto tu = parser.parseTranslationUnit();

    result.parseSuccess = !diag.hasErrors() && (tu != nullptr);

    if (result.parseSuccess && tu) {
        Sema sema(diag, nullptr);
        sema.analyze(*tu);
        result.semaSuccess = !diag.hasErrors();
    }

    for (const auto &d : diag.getDiagnostics()) {
        if (d.level == DiagLevel::Error)
            result.errors.push_back(d.message);
        else if (d.level == DiagLevel::Warning)
            result.warnings.push_back(d.message);
    }

    return result;
}

static std::string projectRoot() {
#ifdef LIVA_PROJECT_ROOT
    return LIVA_PROJECT_ROOT;
#else
    // Fallback: search upward from CWD
    std::vector<std::string> candidates = {".", "..", "../..", "../../.."};
    for (const auto &base : candidates) {
        std::string path = base + "/tests/integration/hello.liva";
        std::ifstream ifs(path);
        if (ifs.is_open()) return base;
    }
    return ".";
#endif
}

} // namespace

// ============================================================
// Integration Tests — Valid Programs
// These should all pass Lexer → Parser → Sema without errors.
// ============================================================

class IntegrationTest : public ::testing::Test {};

TEST_F(IntegrationTest, HelloWorld) {
    std::string source;
    std::string path = projectRoot() + "/tests/integration/hello.liva";
    if (!readFile(path, source))
        FAIL() << "Required test fixture missing: " << path
               << " — fixture files are part of the suite; a missing file is a broken run, not a skip.";

    auto result = runPipeline("hello.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for hello.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for hello.liva";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, Arithmetic) {
    std::string source;
    std::string path = projectRoot() + "/tests/integration/arithmetic.liva";
    if (!readFile(path, source))
        FAIL() << "Required test fixture missing: " << path
               << " — fixture files are part of the suite; a missing file is a broken run, not a skip.";

    auto result = runPipeline("arithmetic.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for arithmetic.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for arithmetic.liva";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, Variables) {
    std::string source;
    std::string path = projectRoot() + "/tests/integration/variables.liva";
    if (!readFile(path, source))
        FAIL() << "Required test fixture missing: " << path
               << " — fixture files are part of the suite; a missing file is a broken run, not a skip.";

    auto result = runPipeline("variables.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for variables.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for variables.liva";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, IfElse) {
    // Note: the integration file defines 'abs' which conflicts with built-in.
    // Use inline version that avoids the conflict.
    std::string source = R"(
func myAbs(x: i32) -> i32 {
    if x < 0 {
        return 0 - x
    }
    return x
}

func main() {
    println(myAbs(0 - 42))
}
)";
    auto result = runPipeline("if_else.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, WhileLoop) {
    std::string source;
    std::string path = projectRoot() + "/tests/integration/while_loop.liva";
    if (!readFile(path, source))
        FAIL() << "Required test fixture missing: " << path
               << " — fixture files are part of the suite; a missing file is a broken run, not a skip.";

    auto result = runPipeline("while_loop.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for while_loop.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for while_loop.liva";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, Functions) {
    std::string source;
    std::string path = projectRoot() + "/tests/integration/functions.liva";
    if (!readFile(path, source))
        FAIL() << "Required test fixture missing: " << path
               << " — fixture files are part of the suite; a missing file is a broken run, not a skip.";

    auto result = runPipeline("functions.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for functions.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for functions.liva";
    EXPECT_TRUE(result.errors.empty());
}

// ============================================================
// Error Tests — Programs that SHOULD produce errors
// ============================================================

TEST_F(IntegrationTest, ErrorBreakOutsideLoop) {
    std::string source;
    std::string path = projectRoot() + "/tests/error/break_outside_loop.liva";
    if (!readFile(path, source))
        FAIL() << "Required test fixture missing: " << path
               << " — fixture files are part of the suite; a missing file is a broken run, not a skip.";

    std::string expectedError = extractExpectedError(source);
    auto result = runPipeline("break_outside_loop.liva", source);

    // Should have errors
    EXPECT_FALSE(result.errors.empty()) << "Expected errors for break_outside_loop.liva";

    // Check that the expected error message is somewhere in the diagnostics
    if (!expectedError.empty()) {
        bool found = false;
        for (const auto &err : result.errors) {
            if (err.find(expectedError) != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected error containing '" << expectedError
                           << "' not found in diagnostics";
    }
}

TEST_F(IntegrationTest, ErrorImmutableAssign) {
    std::string source;
    std::string path = projectRoot() + "/tests/error/immutable_assign.liva";
    if (!readFile(path, source))
        FAIL() << "Required test fixture missing: " << path
               << " — fixture files are part of the suite; a missing file is a broken run, not a skip.";

    std::string expectedError = extractExpectedError(source);
    auto result = runPipeline("immutable_assign.liva", source);

    EXPECT_FALSE(result.errors.empty()) << "Expected errors for immutable_assign.liva";

    if (!expectedError.empty()) {
        bool found = false;
        for (const auto &err : result.errors) {
            if (err.find(expectedError) != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected error containing '" << expectedError
                           << "' not found in diagnostics";
    }
}

TEST_F(IntegrationTest, ErrorUndefinedVar) {
    std::string source;
    std::string path = projectRoot() + "/tests/error/undefined_var.liva";
    if (!readFile(path, source))
        FAIL() << "Required test fixture missing: " << path
               << " — fixture files are part of the suite; a missing file is a broken run, not a skip.";

    std::string expectedError = extractExpectedError(source);
    auto result = runPipeline("undefined_var.liva", source);

    EXPECT_FALSE(result.errors.empty()) << "Expected errors for undefined_var.liva";

    if (!expectedError.empty()) {
        bool found = false;
        for (const auto &err : result.errors) {
            if (err.find(expectedError) != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected error containing '" << expectedError
                           << "' not found in diagnostics";
    }
}

TEST_F(IntegrationTest, ErrorUseAfterMove) {
    // The ownership checker may not catch all use-after-move scenarios
    // in integration mode. This test verifies the file at least parses.
    std::string source;
    std::string path = projectRoot() + "/tests/error/use_after_move.liva";
    if (!readFile(path, source))
        FAIL() << "Required test fixture missing: " << path
               << " — fixture files are part of the suite; a missing file is a broken run, not a skip.";

    auto result = runPipeline("use_after_move.liva", source);
    // The file should at least parse correctly
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for use_after_move.liva";
    // Ownership errors may or may not be caught depending on analysis mode
    // Just verify the pipeline doesn't crash
}

// ============================================================
// Inline Integration Tests — Source embedded directly
// Tests full pipeline without requiring file access
// ============================================================

TEST_F(IntegrationTest, InlineHelloWorld) {
    std::string source = R"(
func main() {
    println("Hello, World!")
}
)";
    auto result = runPipeline("inline_hello.liva", source);
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
}

TEST_F(IntegrationTest, InlineFibonacci) {
    std::string source = R"(
func fibonacci(n: i32) -> i32 {
    if n <= 1 {
        return n
    }
    return fibonacci(n - 1) + fibonacci(n - 2)
}

func main() {
    let result = fibonacci(10)
    println(result)
}
)";
    auto result = runPipeline("inline_fibonacci.liva", source);
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
}

TEST_F(IntegrationTest, InlineStructAndImpl) {
    std::string source = R"(
struct Point {
    var x: f64
    var y: f64
}

impl Point {
    func new(x: f64, y: f64) -> Point {
        return Point { x: x, y: y }
    }
}

func main() {
    let p = Point.new(3.0, 4.0)
    println(p.x)
}
)";
    auto result = runPipeline("inline_struct.liva", source);
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
}

TEST_F(IntegrationTest, InlineEnumMatch) {
    std::string source = R"(
enum Color {
    case Red
    case Green
    case Blue
}

func main() {
    let c: Color = Color.Red
    match c {
        Red => println("red")
        Green => println("green")
        Blue => println("blue")
    }
}
)";
    auto result = runPipeline("inline_enum.liva", source);
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
}

TEST_F(IntegrationTest, InlineArrayOperations) {
    std::string source = R"(
func main() {
    let arr = [1, 2, 3, 4, 5]
    println(len(arr))
    println(arr[0])
    println(arr[4])
}
)";
    auto result = runPipeline("inline_array.liva", source);
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
}

TEST_F(IntegrationTest, InlineOptionalType) {
    std::string source = R"(
func find(target: i32) -> i32? {
    if target > 0 {
        return target
    }
    return nil
}

func main() {
    let val = find(42)
    let missing = find(-1)
}
)";
    auto result = runPipeline("inline_optional.liva", source);
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
}

TEST_F(IntegrationTest, InlineHigherOrder) {
    std::string source = R"(
func apply(x: i32, f: (i32) -> i32) -> i32 {
    return f(x)
}

func double_it(x: i32) -> i32 {
    return x * 2
}

func main() {
    let result = apply(5, double_it)
    println(result)
}
)";
    auto result = runPipeline("inline_higher_order.liva", source);
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
}

TEST_F(IntegrationTest, InlineWhileAndBreak) {
    std::string source = R"(
func main() {
    var i: i32 = 0
    while true {
        if i >= 10 {
            break
        }
        i = i + 1
    }
    println(i)
}
)";
    auto result = runPipeline("inline_while_break.liva", source);
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
}

TEST_F(IntegrationTest, InlineGenericFunction) {
    std::string source = R"(
func identity<T>(x: T) -> T {
    return x
}

func main() {
    let a = identity(42)
    let b = identity("hello")
    println(a)
    println(b)
}
)";
    auto result = runPipeline("inline_generic.liva", source);
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
}

TEST_F(IntegrationTest, InlineErrorUndeclared) {
    std::string source = R"(
func main() {
    println(xyz)
}
)";
    auto result = runPipeline("inline_error.liva", source);
    EXPECT_FALSE(result.errors.empty());
}

TEST_F(IntegrationTest, InlineErrorRedefinition) {
    std::string source = R"(
func main() {
    let x: i32 = 42
    let x: i32 = 10
}
)";
    auto result = runPipeline("inline_redef_error.liva", source);
    EXPECT_FALSE(result.errors.empty());
}

// ============================================================
// Extended Inline Integration Tests
// Covers closures, generics, pattern matching, ownership,
// control flow, types, strings, protocols, and error scenarios.
// ============================================================

// --- Closure Tests ---

TEST_F(IntegrationTest, ClosureWithCapture) {
    std::string source = R"(
func main() {
    let x: i32 = 10
    let add_x: (i32) -> i32 = |y: i32| -> i32 { return x + y }
    let result = add_x(5)
    println(result)
}
)";
    auto result = runPipeline("closure_capture.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClosureAsArgument) {
    std::string source = R"(
func apply(x: i32, f: (i32) -> i32) -> i32 {
    return f(x)
}

func main() {
    let result = apply(5, |x: i32| -> i32 { return x * 3 })
    println(result)
}
)";
    auto result = runPipeline("closure_argument.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, TrailingClosureSyntax) {
    std::string source = R"(
func doTwice(x: i32, f: (i32) -> i32) -> i32 {
    return f(f(x))
}

func main() {
    let result = doTwice(2) |x: i32| -> i32 { return x * 2 }
    println(result)
}
)";
    auto result = runPipeline("trailing_closure.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

// --- Generics Tests ---

TEST_F(IntegrationTest, GenericStruct) {
    std::string source = R"(
struct Pair<T> {
    let first: T
    let second: T
}

func main() {
    let p = Pair { first: 1, second: 2 }
    println(p.first)
}
)";
    auto result = runPipeline("generic_struct.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, GenericMultipleParams) {
    std::string source = R"(
struct Container<T, U> {
    let key: T
    let value: U
}

func main() {
    let c = Container { key: 42, value: "hello" }
    println(c.key)
    println(c.value)
}
)";
    auto result = runPipeline("generic_multi_param.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, GenericFunctionSwap) {
    std::string source = R"(
func second<T>(a: T, b: T) -> T {
    return b
}

func main() {
    let r = second(1, 2)
    println(r)
}
)";
    auto result = runPipeline("generic_swap.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

// --- Pattern Matching Tests ---

TEST_F(IntegrationTest, MatchEnumAssociatedValues) {
    std::string source = R"(
enum Shape {
    case Circle(f64)
    case Rectangle(f64, f64)
}

func main() {
    let s = Shape.Circle(5.0)
    match s {
        Shape.Circle(r) => println(r)
        Shape.Rectangle(w, h) => println(w)
    }
}
)";
    auto result = runPipeline("match_assoc_values.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, MatchWithWildcard) {
    std::string source = R"(
enum Color {
    case Red
    case Green
    case Blue
}

func main() {
    let c: Color = Color.Red
    match c {
        Red => println("is red")
        _ => println("not red")
    }
}
)";
    auto result = runPipeline("match_wildcard.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, MatchOptionEnum) {
    std::string source = R"(
enum MyOption {
    case Some(i32)
    case None
}

func main() {
    let x = MyOption.Some(42)
    let val = match x {
        MyOption.Some(v) => v
        MyOption.None => 0
    }
    println(val)
}
)";
    auto result = runPipeline("match_option.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

// --- Ownership & Borrowing Tests ---

TEST_F(IntegrationTest, RefMutParameter) {
    std::string source = R"--(
func increment(x: ref mut i32) {
    x = x + 1
}

func main() {
    var n: i32 = 5
    increment(ref mut n)
    println(n)
}
)--";
    auto result = runPipeline("ref_mut_param.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, MultipleImmutableBorrows) {
    std::string source = R"--(
func sum(a: ref i32, b: ref i32) -> i32 {
    return a + b
}

func main() {
    let x: i32 = 3
    let result = sum(ref x, ref x)
    println(result)
}
)--";
    auto result = runPipeline("multi_borrow.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, StructMethodsWithSelf) {
    std::string source = R"(
struct Counter {
    var count: i32
}

impl Counter {
    func new() -> Counter {
        return Counter { count: 0 }
    }

    func value(self) -> i32 {
        return self.count
    }

    func increment(mut self) {
        self.count = self.count + 1
    }
}

func main() {
    var c = Counter.new()
    c.increment()
    c.increment()
    println(c.value())
}
)";
    auto result = runPipeline("struct_methods_self.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

// --- Control Flow Tests ---

TEST_F(IntegrationTest, ForInRange) {
    std::string source = R"(
func main() {
    var sum: i32 = 0
    for i in 0..10 {
        sum = sum + i
    }
    println(sum)
}
)";
    auto result = runPipeline("for_in_range.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, NestedLoops) {
    std::string source = R"(
func main() {
    var total: i32 = 0
    for i in 0..3 {
        for j in 0..3 {
            total = total + 1
        }
    }
    println(total)
}
)";
    auto result = runPipeline("nested_loops.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, WhileWithContinue) {
    std::string source = R"(
func main() {
    var i: i32 = 0
    var sum: i32 = 0
    while i < 10 {
        i = i + 1
        if i == 5 {
            continue
        }
        sum = sum + i
    }
    println(sum)
}
)";
    auto result = runPipeline("while_continue.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

// --- Type Features Tests ---

TEST_F(IntegrationTest, TupleReturnAndAccess) {
    std::string source = R"--(
func swap(a: i32, b: i32) -> (i32, i32) {
    return (b, a)
}

func main() {
    let result = swap(1, 2)
    println(result.0)
    println(result.1)
}
)--";
    auto result = runPipeline("tuple_return.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, TypeAlias) {
    std::string source = R"--(
type ID = i32

func getID() -> ID {
    return 42
}

func main() {
    let id: ID = getID()
    println(id)
}
)--";
    auto result = runPipeline("type_alias.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, CastExpression) {
    std::string source = R"--(
func main() {
    let x: i32 = 42
    let y = x as i64
    println(y)
}
)--";
    auto result = runPipeline("cast_expr.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

// --- String Operations Tests ---

TEST_F(IntegrationTest, StringPrintMultiple) {
    std::string source = R"(
func main() {
    let name = "World"
    println("Hello!")
    println(name)
}
)";
    auto result = runPipeline("string_print.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, StringConcatenation) {
    std::string source = R"(
func main() {
    let a = "Hello"
    let b = " World"
    let c = a + b
    println(c)
}
)";
    auto result = runPipeline("string_concat.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, UTF8StringOperations) {
    std::string source = R"(
func main() {
    let s = "hello"
    let len = s.length
    let blen = s.byteLength
    println(len)
    println(blen)
}
)";
    auto result = runPipeline("utf8_string.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

// --- Error Scenario Tests ---

TEST_F(IntegrationTest, ErrorNilWithoutOptional) {
    std::string source = R"(
func main() {
    let x: i32 = nil
}
)";
    auto result = runPipeline("error_nil_no_optional.liva", source);
    EXPECT_FALSE(result.errors.empty()) << "Expected nil-without-optional error";
}

TEST_F(IntegrationTest, ErrorContinueOutsideLoop) {
    std::string source = R"(
func main() {
    continue
}
)";
    auto result = runPipeline("error_continue_outside.liva", source);
    EXPECT_FALSE(result.errors.empty()) << "Expected continue outside loop error";
}

TEST_F(IntegrationTest, ErrorDuplicateFunction) {
    std::string source = R"(
func foo() {}
func foo() {}

func main() {}
)";
    auto result = runPipeline("error_dup_func.liva", source);
    EXPECT_FALSE(result.errors.empty()) << "Expected duplicate function error";
}

// --- Protocol / Trait Tests ---

TEST_F(IntegrationTest, ProtocolConformance) {
    std::string source = R"(
protocol Greetable {
    func greet(self) -> string
}

struct Person {
    let name: string
}

impl Person: Greetable {
    func greet(self) -> string {
        return self.name
    }
}

func main() {
    let p = Person { name: "Alice" }
    println(p.greet())
}
)";
    auto result = runPipeline("protocol_conformance.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, DefaultArguments) {
    std::string source = R"--(
func greet(name: string, greeting: string = "Hello") -> string {
    return greeting
}

func main() {
    println(greet("World"))
    println(greet("World", "Hi"))
}
)--";
    auto result = runPipeline("default_args.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

// --- Additional Feature Tests ---

TEST_F(IntegrationTest, TernaryExpression) {
    std::string source = R"--(
func main() {
    let x: i32 = 10
    let y = x > 5 ? "big" : "small"
    println(y)
}
)--";
    auto result = runPipeline("ternary_expr.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, OptionalChaining) {
    std::string source = R"--(
struct Point {
    x: i32
    y: i32
}

func main() {
    var p: Point? = nil
    let val = p?.x ?? 0
    println(val)
}
)--";
    auto result = runPipeline("optional_chaining.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, AsyncAwaitBasic) {
    std::string source = R"--(
async func fetchData() -> i32 {
    return 42
}

async func process() -> i32 {
    let x: i32 = await fetchData()
    return x
}

func main() {
    println(process())
}
)--";
    auto result = runPipeline("async_await.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, VariadicFunction) {
    std::string source = R"--(
func sumAll(values: i32...) {
    for v in values {
        println(v)
    }
}

func main() {
    sumAll(1, 2, 3, 4, 5)
}
)--";
    auto result = runPipeline("variadic_func.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, MatchWithGuardClause) {
    std::string source = R"--(
func main() {
    let x: i32 = 5
    match x {
        _ where x > 10 => println("big")
        _ where x > 0 => println("positive")
        _ => println("non-positive")
    }
}
)--";
    auto result = runPipeline("match_guard.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, TupleDestructuring) {
    std::string source = R"--(
func main() {
    let (a, b) = (1, "hi")
    println(a)
    println(b)
}
)--";
    auto result = runPipeline("tuple_destructure.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClosureCaptureMultiple) {
    std::string source = R"(
func main() {
    let a: i32 = 10
    let b: i32 = 20
    let sum_ab: () -> i32 = || -> i32 { return a + b }
    println(sum_ab())
}
)";
    auto result = runPipeline("closure_multi_capture.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ErrorBreakOutsideLoopInline) {
    std::string source = R"(
func main() {
    break
}
)";
    auto result = runPipeline("error_break_outside.liva", source);
    EXPECT_FALSE(result.errors.empty()) << "Expected break outside loop error";
}

TEST_F(IntegrationTest, ErrorAssignToImmutable) {
    std::string source = R"(
func main() {
    let x: i32 = 42
    x = 10
}
)";
    auto result = runPipeline("error_assign_immutable.liva", source);
    EXPECT_FALSE(result.errors.empty()) << "Expected immutable assignment error";
}

TEST_F(IntegrationTest, GenericStructWithMethod) {
    std::string source = R"(
struct Box<T> {
    let data: T
}

impl Box {
    func get(self) -> T {
        return self.data
    }
}

func main() {
    let b = Box { data: 99 }
    println(b.get())
}
)";
    auto result = runPipeline("generic_struct_method.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ForInArray) {
    std::string source = R"(
func main() {
    let arr = [10, 20, 30]
    var sum: i32 = 0
    for item in arr {
        sum = sum + item
    }
    println(sum)
}
)";
    auto result = runPipeline("for_in_array.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, NestedIfElse) {
    std::string source = R"(
func classify(x: i32) -> string {
    if x > 0 {
        if x > 100 {
            return "large"
        } else {
            return "small"
        }
    } else {
        return "non-positive"
    }
}

func main() {
    println(classify(50))
    println(classify(200))
    println(classify(0 - 5))
}
)";
    auto result = runPipeline("nested_if_else.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, RecursiveDataStructure) {
    std::string source = R"(
func factorial(n: i32) -> i32 {
    if n <= 1 {
        return 1
    }
    return n * factorial(n - 1)
}

func main() {
    println(factorial(5))
    println(factorial(10))
}
)";
    auto result = runPipeline("recursive_factorial.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

// ============================================================
// Sema Error Detection Integration Tests
// Verifies that the semantic analysis phase catches type errors,
// argument count mismatches, and other semantic violations.
// ============================================================

TEST_F(IntegrationTest, ErrorTypeMismatchAssignment) {
    std::string source = R"(
func main() {
    let x: i32 = "hello"
}
)";
    auto result = runPipeline("error_type_mismatch_assign.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_FALSE(result.semaSuccess) << "Sema should fail with type mismatch";
    EXPECT_FALSE(result.errors.empty()) << "Expected type mismatch error";
}

TEST_F(IntegrationTest, ErrorWrongArgCount) {
    std::string source = R"(
func add(a: i32, b: i32) -> i32 {
    return a + b
}

func main() {
    let r = add(1, 2, 3)
    println(r)
}
)";
    auto result = runPipeline("error_wrong_arg_count.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_FALSE(result.semaSuccess) << "Sema should fail with wrong arg count";
    EXPECT_FALSE(result.errors.empty()) << "Expected wrong arg count error";
}

TEST_F(IntegrationTest, ErrorWrongArgCountTooFew) {
    std::string source = R"(
func add(a: i32, b: i32) -> i32 {
    return a + b
}

func main() {
    let r = add(1)
    println(r)
}
)";
    auto result = runPipeline("error_wrong_arg_count_few.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_FALSE(result.semaSuccess) << "Sema should fail with wrong arg count";
    EXPECT_FALSE(result.errors.empty()) << "Expected wrong arg count error";
}

TEST_F(IntegrationTest, ErrorReturnTypeMismatch) {
    std::string source = R"(
func getNumber() -> i32 {
    return "not a number"
}

func main() {
    println(getNumber())
}
)";
    auto result = runPipeline("error_return_type_mismatch.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_FALSE(result.semaSuccess) << "Sema should fail with return type mismatch";
    EXPECT_FALSE(result.errors.empty()) << "Expected return type mismatch error";
}

TEST_F(IntegrationTest, ErrorConditionNotBool) {
    std::string source = R"(
func main() {
    if 42 {
        println("hello")
    }
}
)";
    auto result = runPipeline("error_condition_not_bool.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_FALSE(result.semaSuccess) << "Sema should fail with condition not bool";
    EXPECT_FALSE(result.errors.empty()) << "Expected condition not bool error";
}

TEST_F(IntegrationTest, ErrorVoidVariable) {
    // Declaring a variable with void type.
    // The DiagnosticKinds.def defines err_void_variable but the TypeChecker
    // does not currently emit it. Note: 'void' may not even parse as a type
    // annotation in Liva, so this may fail at parse time instead.
    std::string source = R"(
func nothing() {}

func main() {
    let x = nothing()
}
)";
    auto result = runPipeline("error_void_variable.liva", source);
    // Pipeline should not crash — this tests that assigning a void return
    // value does not cause undefined behavior in the Sema pipeline.
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
}

TEST_F(IntegrationTest, ErrorUndefinedMember) {
    // Accessing a non-existent member on a struct.
    // The DiagnosticKinds.def defines err_undefined_member but the TypeChecker
    // does not currently validate struct field accesses.
    std::string source = R"(
struct Point {
    var x: i32
    var y: i32
}

func main() {
    let p = Point { x: 1, y: 2 }
    println(p.nonexistent)
}
)";
    auto result = runPipeline("error_undefined_member.liva", source);
    // Pipeline should not crash
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    // Note: Currently the type checker does not emit err_undefined_member
    // for struct field accesses. When member validation is implemented,
    // change to:
    // EXPECT_FALSE(result.errors.empty()) << "Expected undefined member error";
}

TEST_F(IntegrationTest, ErrorNonExhaustiveMatch) {
    // Match expression that does not cover all enum cases and has no wildcard.
    // The TypeChecker DOES emit err_nonexhaustive_match for dotted enum patterns.
    std::string source = R"(
enum Color {
    case Red
    case Green
    case Blue
}

func main() {
    let c: Color = Color.Red
    match c {
        Color.Red => println("red")
        Color.Green => println("green")
    }
}
)";
    auto result = runPipeline("error_nonexhaustive_match.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_FALSE(result.errors.empty()) << "Expected non-exhaustive match error";
    // Verify the error message mentions the missing case
    bool foundMissing = false;
    for (const auto &err : result.errors) {
        if (err.find("not exhaustive") != std::string::npos ||
            err.find("missing case") != std::string::npos ||
            err.find("Blue") != std::string::npos) {
            foundMissing = true;
            break;
        }
    }
    EXPECT_TRUE(foundMissing) << "Expected error about missing 'Blue' case";
}

TEST_F(IntegrationTest, ErrorCircularRedefinition) {
    // Redeclaring the same variable in the same scope.
    // The TypeChecker DOES emit err_redefinition for duplicate declarations.
    // (Similar to InlineErrorRedefinition but more explicit about the check.)
    std::string source = R"(
func main() {
    let x: i32 = 1
    let x: string = "two"
}
)";
    auto result = runPipeline("error_circular_redef.liva", source);
    EXPECT_FALSE(result.errors.empty()) << "Expected redefinition error";
    bool foundRedef = false;
    for (const auto &err : result.errors) {
        if (err.find("redefinition") != std::string::npos) {
            foundRedef = true;
            break;
        }
    }
    EXPECT_TRUE(foundRedef) << "Expected error containing 'redefinition'";
}

TEST_F(IntegrationTest, ErrorNonExhaustiveMatchSingleCase) {
    // Match expression with only one case out of three, no wildcard.
    std::string source = R"(
enum Direction {
    case North
    case South
    case East
    case West
}

func main() {
    let d: Direction = Direction.North
    match d {
        Direction.North => println("north")
    }
}
)";
    auto result = runPipeline("error_nonexhaustive_single.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_FALSE(result.errors.empty()) << "Expected non-exhaustive match errors";
}

TEST_F(IntegrationTest, ErrorUndeclaredInExpression) {
    // Using an undeclared variable in a binary expression.
    // The TypeChecker DOES emit err_undeclared_identifier.
    std::string source = R"(
func main() {
    let x = 1 + unknown_var
}
)";
    auto result = runPipeline("error_undeclared_in_expr.liva", source);
    EXPECT_FALSE(result.errors.empty()) << "Expected undeclared identifier error";
    bool foundUndeclared = false;
    for (const auto &err : result.errors) {
        if (err.find("undeclared") != std::string::npos &&
            err.find("unknown_var") != std::string::npos) {
            foundUndeclared = true;
            break;
        }
    }
    EXPECT_TRUE(foundUndeclared) << "Expected error about undeclared 'unknown_var'";
}

TEST_F(IntegrationTest, ErrorDuplicateStruct) {
    // Defining the same struct name twice.
    // The TypeChecker DOES emit err_redefinition for duplicate struct declarations.
    std::string source = R"(
struct Point {
    var x: i32
}

struct Point {
    var y: i32
}

func main() {}
)";
    auto result = runPipeline("error_dup_struct.liva", source);
    EXPECT_FALSE(result.errors.empty()) << "Expected redefinition error for struct";
}

TEST_F(IntegrationTest, ErrorDuplicateEnum) {
    // Defining the same enum name twice.
    // The TypeChecker DOES emit err_redefinition for duplicate enum declarations.
    std::string source = R"(
enum Color {
    case Red
}

enum Color {
    case Blue
}

func main() {}
)";
    auto result = runPipeline("error_dup_enum.liva", source);
    EXPECT_FALSE(result.errors.empty()) << "Expected redefinition error for enum";
}

TEST_F(IntegrationTest, ErrorNilAssignToNonOptional) {
    // Assigning nil to a non-optional typed variable.
    // The TypeChecker DOES emit err_nil_without_optional.
    std::string source = R"(
func main() {
    let x: i32 = nil
}
)";
    auto result = runPipeline("error_nil_non_optional.liva", source);
    EXPECT_FALSE(result.errors.empty()) << "Expected nil without optional error";
    bool foundNilErr = false;
    for (const auto &err : result.errors) {
        if (err.find("nil") != std::string::npos &&
            err.find("optional") != std::string::npos) {
            foundNilErr = true;
            break;
        }
    }
    EXPECT_TRUE(foundNilErr) << "Expected error about nil with non-optional type";
}

TEST_F(IntegrationTest, ErrorAwaitOutsideAsync) {
    // Using await outside of an async function.
    // The TypeChecker DOES emit err_await_outside_async.
    std::string source = R"--(
async func fetchData() -> i32 {
    return 42
}

func main() {
    let x: i32 = await fetchData()
    println(x)
}
)--";
    auto result = runPipeline("error_await_outside_async.liva", source);
    EXPECT_FALSE(result.errors.empty()) << "Expected await outside async error";
    bool foundAwaitErr = false;
    for (const auto &err : result.errors) {
        if (err.find("await") != std::string::npos) {
            foundAwaitErr = true;
            break;
        }
    }
    EXPECT_TRUE(foundAwaitErr) << "Expected error about 'await' outside async function";
}

TEST_F(IntegrationTest, StringMemoryCleanup) {
    std::string source = R"--(
func main() {
    var s = "hello" + " world"
    s = s + "!"
    println(s)
}
)--";
    auto result = runPipeline("string_cleanup.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for string_cleanup.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for string_cleanup.liva";
    for (const auto &e : result.errors) { EXPECT_TRUE(false) << "Error: " << e; }
}

TEST_F(IntegrationTest, StringTempInLoop) {
    std::string source = R"--(
func main() {
    for i in 0..5 {
        let msg = "item " + toString(i)
        println(msg)
    }
}
)--";
    auto result = runPipeline("string_temp_loop.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for string_temp_loop.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for string_temp_loop.liva";
    for (const auto &e : result.errors) { EXPECT_TRUE(false) << "Error: " << e; }
}

TEST_F(IntegrationTest, AsyncSleep) {
    std::string source = R"--(
async func delayedValue() -> i32 {
    sleep(100)
    return 42
}

async func main() {
    let result: i32 = await delayedValue()
    println(result)
}
)--";
    auto result = runPipeline("async_sleep.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for async_sleep.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for async_sleep.liva";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, AsyncMultipleSleeps) {
    std::string source = R"--(
async func taskA() -> i32 {
    sleep(50)
    return 1
}

async func taskB() -> i32 {
    sleep(100)
    return 2
}

async func main() {
    let a: i32 = await taskA()
    let b: i32 = await taskB()
    println(a)
    println(b)
}
)--";
    auto result = runPipeline("async_multi_sleep.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for async_multi_sleep.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for async_multi_sleep.liva";
    EXPECT_TRUE(result.errors.empty());
}

// === K4: Async Error Propagation + Cancellation Integration Tests ===

TEST_F(IntegrationTest, AsyncResultReturn) {
    std::string source = R"--(
async func fetch() -> Result<i32, string> {
    return Result.ok(42)
}
async func main() {
    let r = await fetch()
    println(r)
}
)--";
    auto result = runPipeline("async_result_return.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for async_result_return.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for async_result_return.liva";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, AsyncTryAwait) {
    std::string source = R"--(
async func fetch() -> Result<i32, string> {
    return Result.ok(100)
}
async func process() -> Result<i32, string> {
    let val = try await fetch()
    return Result.ok(val)
}
func main() {
    println(0)
}
)--";
    auto result = runPipeline("async_try_await.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for async_try_await.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for async_try_await.liva";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, AsyncDeepChain) {
    std::string source = R"--(
async func f5() -> i32 { return 5 }
async func f4() -> i32 { return await f5() }
async func f3() -> i32 { return await f4() }
async func f2() -> i32 { return await f3() }
async func f1() -> i32 { return await f2() }
async func f0() -> i32 { return await f1() }
async func main() {
    let val = await f0()
    println(val)
}
)--";
    auto result = runPipeline("async_deep_chain.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for async_deep_chain.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for async_deep_chain.liva";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, AsyncIsCancelledValid) {
    std::string source = R"--(
async func work() -> i32 {
    if isCancelled() {
        return -1
    }
    return 42
}
async func main() {
    let r = await work()
    println(r)
}
)--";
    auto result = runPipeline("async_is_cancelled.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for async_is_cancelled.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for async_is_cancelled.liva";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ErrorIsCancelledOutsideAsync) {
    std::string source = R"--(
func main() {
    let c = isCancelled()
    println(c)
}
)--";
    auto result = runPipeline("error_is_cancelled.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for error_is_cancelled.liva";
    EXPECT_FALSE(result.semaSuccess) << "Sema should fail for isCancelled outside async";
    EXPECT_FALSE(result.errors.empty());
}

// === Channel & TaskGroup Integration Tests ===

TEST_F(IntegrationTest, Channel_CreateSendReceive) {
    std::string source = R"--(
func main() {
    let ch: i64 = channelCreate(10)
    channelSend(ch, 42)
    let v: i64? = channelReceive(ch)
    channelFree(ch)
}
)--";
    auto result = runPipeline("channel_basic.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, Channel_CloseAndReceiveNil) {
    std::string source = R"--(
func main() {
    let ch: i64 = channelCreate(5)
    channelClose(ch)
    channelFree(ch)
}
)--";
    auto result = runPipeline("channel_close.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, Channel_LenQuery) {
    std::string source = R"--(
func main() {
    let ch: i64 = channelCreate(10)
    channelSend(ch, 1)
    channelSend(ch, 2)
    let n: i64 = channelLen(ch)
    channelFree(ch)
}
)--";
    auto result = runPipeline("channel_len.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, Channel_Multiple) {
    std::string source = R"--(
func main() {
    let ch1: i64 = channelCreate(5)
    let ch2: i64 = channelCreate(5)
    channelSend(ch1, 10)
    channelSend(ch2, 20)
    let v1: i64? = channelReceive(ch1)
    let v2: i64? = channelReceive(ch2)
    channelFree(ch1)
    channelFree(ch2)
}
)--";
    auto result = runPipeline("channel_multi.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, TaskGroup_Basic) {
    std::string source = R"--(
async func worker() -> i32 {
    return 42
}
async func main() {
    let g: i64 = taskGroupCreate()
    taskGroupSpawn(g, worker())
    taskGroupAwaitAll(g)
    taskGroupFree(g)
}
)--";
    auto result = runPipeline("taskgroup_basic.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, TaskGroup_Cancellation) {
    std::string source = R"--(
async func worker() -> i32 {
    return 1
}
async func main() {
    let g: i64 = taskGroupCreate()
    taskGroupSpawn(g, worker())
    taskGroupCancelAll(g)
    taskGroupFree(g)
}
)--";
    auto result = runPipeline("taskgroup_cancel.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, TaskGroup_Count) {
    std::string source = R"--(
async func worker() -> i32 {
    return 1
}
async func main() {
    let g: i64 = taskGroupCreate()
    taskGroupSpawn(g, worker())
    let n: i64 = taskGroupCount(g)
    taskGroupFree(g)
}
)--";
    auto result = runPipeline("taskgroup_count.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, Channel_TaskGroup_Combined) {
    std::string source = R"--(
async func producer(ch: i64) -> i32 {
    channelSend(ch, 42)
    return 0
}
async func main() {
    let ch: i64 = channelCreate(10)
    let g: i64 = taskGroupCreate()
    taskGroupSpawn(g, producer(ch))
    taskGroupAwaitAll(g)
    let v: i64? = channelReceive(ch)
    channelClose(ch)
    channelFree(ch)
    taskGroupFree(g)
}
)--";
    auto result = runPipeline("channel_taskgroup.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, StringInterpolationCleanup) {
    std::string source = R"--(
func main() {
    let name = "world"
    let greeting = format("Hello, {}!", name)
    println(greeting)
}
)--";
    auto result = runPipeline("string_interp_cleanup.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for string_interp_cleanup.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for string_interp_cleanup.liva";
    for (const auto &e : result.errors) { EXPECT_TRUE(false) << "Error: " << e; }
}

// ============================================================
// Formatter Tests — formatLivaSource()
// ============================================================

TEST_F(IntegrationTest, FmtBasicIndentation) {
    std::string input = "func foo() {\nreturn 42\n}";
    std::string expected = "func foo() {\n    return 42\n}";
    EXPECT_EQ(liva::formatLivaSource(input), expected);
}

TEST_F(IntegrationTest, FmtNestedBlocks) {
    std::string input = "func foo() {\nif true {\nwhile x {\nbar()\n}\n}\n}";
    std::string expected =
        "func foo() {\n"
        "    if true {\n"
        "        while x {\n"
        "            bar()\n"
        "        }\n"
        "    }\n"
        "}";
    EXPECT_EQ(liva::formatLivaSource(input), expected);
}

TEST_F(IntegrationTest, FmtAlreadyFormatted) {
    std::string input =
        "func main() {\n"
        "    println(42)\n"
        "}";
    EXPECT_EQ(liva::formatLivaSource(input), input);
}

TEST_F(IntegrationTest, FmtEmptyLines) {
    std::string input = "func foo() {\n\nreturn 1\n\n}";
    std::string expected = "func foo() {\n\n    return 1\n\n}";
    EXPECT_EQ(liva::formatLivaSource(input), expected);
}

TEST_F(IntegrationTest, FmtMultipleClosingBraces) {
    std::string input = "func foo() {\nif true {\nbar()\n}\n}";
    std::string expected =
        "func foo() {\n"
        "    if true {\n"
        "        bar()\n"
        "    }\n"
        "}";
    EXPECT_EQ(liva::formatLivaSource(input), expected);
}

TEST_F(IntegrationTest, FmtTrailingWhitespace) {
    std::string input = "func foo() {   \n    return 1   \n}  ";
    std::string expected = "func foo() {\n    return 1\n}";
    EXPECT_EQ(liva::formatLivaSource(input), expected);
}

TEST_F(IntegrationTest, FmtEmptyInput) {
    EXPECT_EQ(liva::formatLivaSource(""), "");
}

TEST_F(IntegrationTest, FmtLeadingCloseBrace) {
    std::string input = "func foo() {\nreturn 1\n}\nfunc bar() {\nreturn 2\n}";
    std::string expected =
        "func foo() {\n"
        "    return 1\n"
        "}\n"
        "func bar() {\n"
        "    return 2\n"
        "}";
    EXPECT_EQ(liva::formatLivaSource(input), expected);
}

// ============================================================
// Lint Tests — lintLivaSource()
// ============================================================

namespace {

// Helper: parse source and run linter, return warnings
static std::vector<std::string> runLint(const std::string &source) {
    SourceManager sm("lint_test.liva", source);
    DiagnosticsEngine diag(&sm);
    Lexer lexer(sm, diag);
    Parser parser(lexer, diag);
    auto tu = parser.parseTranslationUnit();
    std::vector<std::string> warnings;
    if (!tu || diag.hasErrors())
        return warnings;
    liva::lintLivaSource(*tu, diag);
    for (const auto &d : diag.getDiagnostics()) {
        if (d.level == DiagLevel::Warning)
            warnings.push_back(d.message);
    }
    return warnings;
}

} // namespace

TEST_F(IntegrationTest, LintTypeNamingStruct) {
    auto warnings = runLint("struct my_point { var x: i32 }\nfunc main() {}");
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("PascalCase"), std::string::npos);
}

TEST_F(IntegrationTest, LintTypeNamingEnum) {
    auto warnings = runLint("enum my_color { case Red }\nfunc main() {}");
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("PascalCase"), std::string::npos);
}

TEST_F(IntegrationTest, LintTypeNamingCorrect) {
    auto warnings = runLint("struct MyPoint { var x: i32 }\nfunc main() {}");
    for (const auto &w : warnings) {
        EXPECT_EQ(w.find("PascalCase"), std::string::npos)
            << "Unexpected PascalCase warning: " << w;
    }
}

TEST_F(IntegrationTest, LintBoolComparisonEqTrue) {
    auto warnings = runLint("func main() {\n    let x = true\n    let y = x == true\n}");
    bool found = false;
    for (const auto &w : warnings) {
        if (w.find("boolean literal") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found) << "Expected bool comparison warning";
}

TEST_F(IntegrationTest, LintBoolComparisonEqFalse) {
    auto warnings = runLint("func main() {\n    let x = true\n    let y = x == false\n}");
    bool found = false;
    for (const auto &w : warnings) {
        if (w.find("boolean literal") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found) << "Expected bool comparison warning";
}

TEST_F(IntegrationTest, LintBoolComparisonClean) {
    auto warnings = runLint("func main() {\n    let x = true\n    if x { }\n}");
    for (const auto &w : warnings) {
        EXPECT_EQ(w.find("boolean literal"), std::string::npos)
            << "Unexpected bool comparison warning: " << w;
    }
}

TEST_F(IntegrationTest, LintEmptyFuncBody) {
    auto warnings = runLint("func foo() {}\nfunc main() {}");
    bool found = false;
    for (const auto &w : warnings) {
        if (w.find("empty body") != std::string::npos && w.find("foo") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found) << "Expected empty function body warning for 'foo'";
}

TEST_F(IntegrationTest, LintNonEmptyFuncBody) {
    auto warnings = runLint("func foo() -> i32 { return 1 }\nfunc main() { println(1) }");
    for (const auto &w : warnings) {
        EXPECT_EQ(w.find("empty body"), std::string::npos)
            << "Unexpected empty body warning: " << w;
    }
}

TEST_F(IntegrationTest, LintMissingDocOnPublic) {
    auto warnings = runLint("pub func foo() -> i32 { return 1 }\nfunc main() {}");
    bool found = false;
    for (const auto &w : warnings) {
        if (w.find("doc comment") != std::string::npos && w.find("foo") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found) << "Expected missing doc comment warning";
}

TEST_F(IntegrationTest, LintDocPresent) {
    auto warnings = runLint("/// A documented function\npub func foo() -> i32 { return 1 }\nfunc main() {}");
    for (const auto &w : warnings) {
        if (w.find("doc comment") != std::string::npos && w.find("foo") != std::string::npos) {
            EXPECT_TRUE(false) << "Unexpected doc comment warning: " << w;
        }
    }
}

TEST_F(IntegrationTest, LintDefaultParamOrder) {
    auto warnings = runLint("func f(x: i32 = 1, y: i32) -> i32 { return y }\nfunc main() {}");
    bool found = false;
    for (const auto &w : warnings) {
        if (w.find("default value") != std::string::npos && w.find("'y'") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found) << "Expected default param order warning";
}

TEST_F(IntegrationTest, LintSelfComparison) {
    auto warnings = runLint("func main() {\n    let x: i32 = 5\n    let y = x == x\n}");
    bool found = false;
    for (const auto &w : warnings) {
        if (w.find("comparing") != std::string::npos && w.find("to itself") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found) << "Expected self-comparison warning";
}

// ============================================================
// K3: Memory Management Integration Tests
// ============================================================

TEST_F(IntegrationTest, DropProtocolBasic) {
    std::string source = R"--(
        protocol Drop {
            func drop(mut self)
        }
        struct Resource {
            var id: i32
        }
        impl Resource: Drop {
            func drop(mut self) {
                println(self.id)
            }
        }
        func main() {
            var r: Resource = Resource { id: 42 }
            println(r.id)
        }
    )--";
    auto result = runPipeline("drop_basic.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
}

TEST_F(IntegrationTest, StructWithHeapFields) {
    std::string source = R"--(
        struct Container {
            var items: [i32]
            var name: string
        }
        func main() {
            var c: Container = Container { items: [1, 2, 3], name: "test" }
            println(c.name)
        }
    )--";
    auto result = runPipeline("struct_heap_fields.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
}

TEST_F(IntegrationTest, MethodBodyCleanup) {
    std::string source = R"--(
        struct Data {
            var value: i32
        }
        impl Data {
            func process(self) {
                var items: [i32] = [1, 2, 3]
                println(items[0])
            }
        }
        func main() {
            var d: Data = Data { value: 1 }
            d.process()
        }
    )--";
    auto result = runPipeline("method_body_cleanup.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
}

TEST_F(IntegrationTest, GenericFunctionCleanup) {
    std::string source = R"--(
        func identity<T>(x: T) -> T {
            return x
        }
        func main() {
            let a = identity(42)
            println(a)
        }
    )--";
    auto result = runPipeline("generic_func_cleanup.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
}

TEST_F(IntegrationTest, NestedScopeCleanup) {
    std::string source = R"--(
        func main() {
            var outer: [i32] = [1, 2]
            {
                var inner: [i32] = [3, 4]
                println(inner[0])
            }
            println(outer[0])
        }
    )--";
    auto result = runPipeline("nested_scope_cleanup.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
}

// =============================================================================
// Class System Integration Tests (Full Pipeline)
// =============================================================================

TEST_F(IntegrationTest, ClassDecl_FullPipeline) {
    std::string source = R"--(
        class Foo {
            var x: i32
            var y: i32
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_basic.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_WithInit_FullPipeline) {
    std::string source = R"--(
        class Counter {
            var count: i32
            init(n: i32) {
                self.count = n
            }
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_init.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_WithDeinit_FullPipeline) {
    std::string source = R"--(
        class Resource {
            var handle: i32
            deinit() {
                println(self.handle)
            }
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_deinit.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_WithMethods_FullPipeline) {
    std::string source = R"--(
        class Calculator {
            var value: i32
            func add(n: i32) -> i32 {
                return self.value + n
            }
            func reset() {
                self.value = 0
            }
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_methods.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_Inheritance_FullPipeline) {
    std::string source = R"--(
        class Animal {
            var name: string
            func speak() {
                println(0)
            }
        }
        class Dog : Animal {
            var breed: string
            override func speak() {
                println(1)
            }
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_inheritance.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_Override_FullPipeline) {
    std::string source = R"--(
        class Shape {
            func area() -> i32 {
                return 0
            }
        }
        class Rectangle : Shape {
            var width: i32
            var height: i32
            override func area() -> i32 {
                return self.width * self.height
            }
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_override.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_PrivateAccess_FullPipeline) {
    std::string source = R"--(
        class Account {
            private var balance: i32
            var name: string
            func getBalance() -> i32 {
                return self.balance
            }
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_private.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_DeepInheritance_FullPipeline) {
    std::string source = R"--(
        class A {
            var x: i32
        }
        class B : A {
            var y: i32
        }
        class C : B {
            var z: i32
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_deep_inheritance.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_InitAndDeinit_FullPipeline) {
    std::string source = R"--(
        class ManagedResource {
            var id: i32
            var active: bool
            init(id: i32) {
                self.id = id
                self.active = true
            }
            deinit() {
                self.active = false
            }
            func getId() -> i32 {
                return self.id
            }
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_init_deinit.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_Empty_FullPipeline) {
    std::string source = R"--(
        class Empty {}
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_empty.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_Generic_FullPipeline) {
    std::string source = R"--(
        class Box<T> {
            var value: T
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_generic.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_ExampleFile_FullPipeline) {
    std::string source;
    std::string path = projectRoot() + "/examples/classes.liva";
    if (!readFile(path, source))
        FAIL() << "Required test fixture missing: " << path
               << " — fixture files are part of the suite; a missing file is a broken run, not a skip.";

    auto result = runPipeline("classes.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed for classes.liva";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed for classes.liva";
    for (const auto &err : result.errors) {
        ADD_FAILURE() << "Error: " << err;
    }
}

TEST_F(IntegrationTest, ClassDecl_ProtocolConformance_FullPipeline) {
    std::string source = R"--(
        protocol Greetable {
            func greet(self) -> string
        }
        class Person : Greetable {
            var name: string
            func greet() -> string {
                return self.name
            }
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_proto.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_InheritanceWithProtocol_FullPipeline) {
    std::string source = R"--(
        protocol Speakable {
            func speak(self) -> string
        }
        class Animal {
            var name: string
        }
        class Dog : Animal, Speakable {
            var breed: string
            func speak() -> string {
                return self.name
            }
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_inherit_proto.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_MultipleProtocols_FullPipeline) {
    std::string source = R"--(
        protocol Printable {
            func toString(self) -> string
        }
        protocol Hashable {
            func hash(self) -> i32
        }
        class Item : Printable, Hashable {
            var id: i32
            func toString() -> string {
                return "item"
            }
            func hash() -> i32 {
                return self.id
            }
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_multi_proto.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_MissingProtocolMethod_FullPipeline) {
    std::string source = R"--(
        protocol Printable {
            func toString(self) -> string
        }
        class Broken : Printable {
            var x: i32
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_missing_proto.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_FALSE(result.semaSuccess) << "Sema should fail: missing protocol method";
    EXPECT_FALSE(result.errors.empty());
}

TEST_F(IntegrationTest, ClassDecl_ErrorParentNotFound_FullPipeline) {
    std::string source = R"--(
        class Dog : NonExistent {
            var name: string
        }
        func main() {
            println(0)
        }
    )--";
    auto result = runPipeline("class_error_parent.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_FALSE(result.semaSuccess) << "Sema should fail for unknown parent";
    EXPECT_FALSE(result.errors.empty());
}

// ===== Y3: Generics Edge Cases Integration Tests =====

TEST_F(IntegrationTest, ObjectSafety_GenericMethodReject) {
    std::string source = R"--(
        protocol Converter {
            func convert<T>(self) -> T
        }
        struct Foo { var x: i32 }
        impl Foo: Converter {
            func convert<T>(self) -> T { return self.x }
        }
        func use(c: dyn Converter) {
            println(0)
        }
        func main() { println(0) }
    )--";
    auto result = runPipeline("object_safety.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_FALSE(result.semaSuccess) << "Sema should reject dyn with generic method";
    EXPECT_FALSE(result.errors.empty());
    bool found = false;
    for (auto &e : result.errors) {
        if (e.find("cannot be used as a trait object") != std::string::npos) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found) << "Should have object safety error";
}

TEST_F(IntegrationTest, AssocTypeConstraint_E2E) {
    std::string source = R"--(
        protocol Container {
            type Item
            func get(self) -> i32
        }
        struct StrBox { var val: i32 }
        impl StrBox: Container {
            type Item = string
            func get(self) -> i32 { return self.val }
        }
        func process<T: Container>(c: T) where T.Item == i32 {
            println(c.get())
        }
        func main() {
            let b = StrBox { val: 1 }
            process(b)
        }
    )--";
    auto result = runPipeline("assoc_constraint.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_FALSE(result.semaSuccess) << "Sema should fail: Item is string, not i32";
    EXPECT_FALSE(result.errors.empty());
}

TEST_F(IntegrationTest, AssocTypeRef_BasicE2E) {
    std::string source = R"--(
        protocol Container {
            type Item
            func get(self) -> i32
        }
        struct IntBox { var val: i32 }
        impl IntBox: Container {
            type Item = i32
            func get(self) -> i32 { return self.val }
        }
        func first<T: Container>(c: T) -> T.Item {
            return c.get()
        }
        func main() {
            let b = IntBox { val: 42 }
            let x = first(b)
            println(x)
        }
    )--";
    auto result = runPipeline("assoc_type_ref.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed";
    EXPECT_TRUE(result.semaSuccess) << "Sema should pass with T.Item resolved";
}

// === Y4: Postfix ? E2E ===

TEST_F(IntegrationTest, PostfixQuestion_E2E) {
    std::string source = R"--(
func parseValue(s: string) -> Result<i32, string> {
    return Result.ok(42)
}
func process() -> Result<i32, string> {
    let x = parseValue("42")?
    let y = parseValue("10")?
    return Result.ok(x + y)
}
func main() {
    let r = process()
    println(0)
}
)--";
    auto result = runPipeline("postfix_question.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed for postfix ?";
    EXPECT_TRUE(result.semaSuccess) << "Sema should pass for postfix ? in Result func";
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, PostfixQuestion_TernaryCoexist) {
    std::string source = R"--(
func tryParse() -> Result<i32, string> {
    return Result.ok(10)
}
func compute() -> Result<i32, string> {
    let x = tryParse()?
    let cond = true
    let y = cond ? x : 0
    return Result.ok(y)
}
func main() {
    let r = compute()
    println(0)
}
)--";
    auto result = runPipeline("postfix_q_ternary.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse should succeed with both ? usages";
    EXPECT_TRUE(result.semaSuccess) << "Sema should pass with ? and ternary coexisting";
    EXPECT_TRUE(result.errors.empty());
}

// === Struct Closure Field Tests ===

TEST_F(IntegrationTest, StructWithClosureField) {
    std::string source = R"--(
struct Handler {
    var id: i32
    var callback: (i32) -> void
}

func main() {
    let h = Handler { id: 1, callback: |x: i32| { println(x) } }
}
)--";
    auto result = runPipeline("struct_closure_field.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed: " <<
        (result.errors.empty() ? "" : result.errors[0]);
}

TEST_F(IntegrationTest, StructClosureFieldCall) {
    std::string source = R"--(
struct Handler {
    var id: i32
    var callback: (i32) -> void
}

impl Handler {
    func invoke(ref self) {
        self.callback(self.id)
    }
}

func main() {
    let h = Handler { id: 42, callback: |x: i32| { println(x) } }
    h.invoke()
}
)--";
    auto result = runPipeline("struct_closure_field_call.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed: " <<
        (result.errors.empty() ? "" : result.errors[0]);
}

TEST_F(IntegrationTest, StructClosureFieldCapture) {
    std::string source = R"--(
struct Runner {
    var action: () -> void
}

func main() {
    var count = 0
    let r = Runner { action: || { count = count + 1 } }
}
)--";
    auto result = runPipeline("struct_closure_capture.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed: " <<
        (result.errors.empty() ? "" : result.errors[0]);
}

TEST_F(IntegrationTest, StructClosureFieldNoOp) {
    std::string source = R"--(
struct Widget {
    var name: string
    var onClick: (i32) -> void
}

func main() {
    let w = Widget { name: "btn", onClick: |id: i32| { } }
}
)--";
    auto result = runPipeline("struct_closure_noop.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed: " <<
        (result.errors.empty() ? "" : result.errors[0]);
}

TEST_F(IntegrationTest, StructClosureTrailingFactory) {
    std::string source = R"--(
struct Button {
    var text: string
    var onClick: (i32) -> void
}

impl Button {
    func withClick(text: string, cb: (i32) -> void) -> Button {
        return Button { text: text, onClick: cb }
    }
}

func main() {
    let btn = Button.withClick("OK") |id: i32| { println(id) }
}
)--";
    auto result = runPipeline("struct_closure_trailing.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed: " <<
        (result.errors.empty() ? "" : result.errors[0]);
}

TEST_F(IntegrationTest, StructClosureSetterMethod) {
    std::string source = R"--(
struct Handler {
    var id: i32
    var onEvent: (i32) -> void
}

impl Handler {
    func setHandler(ref mut self, cb: (i32) -> void) {
        self.onEvent = cb
    }
}

func main() {
    var h = Handler { id: 1, onEvent: |x: i32| { } }
    h.setHandler() |x: i32| { println(x) }
}
)--";
    auto result = runPipeline("struct_closure_setter.liva", source);
    EXPECT_TRUE(result.parseSuccess) << "Parse failed";
    EXPECT_TRUE(result.semaSuccess) << "Sema failed: " <<
        (result.errors.empty() ? "" : result.errors[0]);
}

// === Cross-Compilation Target Tests ===

// TargetInfo and CompilerInstance tests require LLVM (liva_codegen)
#ifdef LIVA_HAS_LLVM

TEST_F(IntegrationTest, CrossTarget_TargetInfoFromTriple) {
    auto info = TargetInfo::fromTriple("aarch64-unknown-linux-gnu");
    EXPECT_EQ(info.triple, "aarch64-unknown-linux-gnu");
    EXPECT_EQ(info.cpu, "generic");
    EXPECT_TRUE(info.features.empty());
}

TEST_F(IntegrationTest, CrossTarget_TargetInfoHost) {
    auto info = TargetInfo::getHostTarget();
    EXPECT_FALSE(info.triple.empty());
}

TEST_F(IntegrationTest, CrossTarget_IsCrossCompilingFalseForHost) {
    auto host = TargetInfo::getHostTarget();
    EXPECT_FALSE(host.isCrossCompiling());
}

TEST_F(IntegrationTest, CrossTarget_IsCrossCompilingTrue) {
    auto info = TargetInfo::fromTriple("aarch64-unknown-linux-gnu");
    // Only true if host is NOT aarch64-unknown-linux-gnu
    auto host = TargetInfo::getHostTarget();
    if (host.triple != "aarch64-unknown-linux-gnu") {
        EXPECT_TRUE(info.isCrossCompiling());
    }
}

TEST_F(IntegrationTest, CrossTarget_TargetInfoCpuFeatures) {
    auto info = TargetInfo::fromTriple("aarch64-unknown-linux-gnu", "cortex-a72", "+neon");
    EXPECT_EQ(info.triple, "aarch64-unknown-linux-gnu");
    EXPECT_EQ(info.cpu, "cortex-a72");
    EXPECT_EQ(info.features, "+neon");
}

TEST_F(IntegrationTest, CrossTarget_CompilerInstanceDefault) {
    CompilerInstance compiler;
    // Should work without setting target — uses host target
    compiler.setSource("test.liva", "func main() { println(0) }");
    bool ok = compiler.checkOnly();
    EXPECT_TRUE(ok);
}

TEST_F(IntegrationTest, CrossTarget_ParseSemaTargetAgnostic) {
    // Parse+sema pipeline should produce same result regardless of target
    std::string source = "func main() {\n    let x: i32 = 42\n    println(x)\n}\n";

    // Run with default (host) target
    CompilerInstance compiler1;
    compiler1.setSource("test1.liva", source);
    bool ok1 = compiler1.checkOnly();

    // Run with cross target
    CompilerInstance compiler2;
    compiler2.setTargetTriple("aarch64-unknown-linux-gnu");
    compiler2.setSource("test2.liva", source);
    bool ok2 = compiler2.checkOnly();

    EXPECT_EQ(ok1, ok2);
}

TEST_F(IntegrationTest, CrossTarget_TargetInfoDefaultCpu) {
    auto info = TargetInfo::fromTriple("arm64-apple-macosx");
    EXPECT_EQ(info.cpu, "generic");
    EXPECT_TRUE(info.features.empty());
}

#endif // LIVA_HAS_LLVM

// These tests don't need LLVM — pure struct field tests
TEST_F(IntegrationTest, CrossTarget_DriverOptionsTarget) {
    DriverOptions opts;
    EXPECT_TRUE(opts.targetTriple.empty());
    EXPECT_FALSE(opts.hasTargetOverride);
    opts.targetTriple = "riscv64-unknown-linux-gnu";
    opts.hasTargetOverride = true;
    EXPECT_EQ(opts.targetTriple, "riscv64-unknown-linux-gnu");
    EXPECT_TRUE(opts.hasTargetOverride);
}

TEST_F(IntegrationTest, CrossTarget_ProjectConfigTarget) {
    ProjectConfig cfg;
    EXPECT_TRUE(cfg.target.empty());
    cfg.target = "wasm32-unknown-wasi";
    EXPECT_EQ(cfg.target, "wasm32-unknown-wasi");
}

// === FFI Tests ===

TEST_F(IntegrationTest, FFI_ExternFuncDecl) {
    auto result = runPipeline("test.liva", R"--(
        extern "C" func c_abs(x: i32) -> i32
        func main() {
            println(0)
        }
    )--");
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, FFI_ExternCVarargs) {
    auto result = runPipeline("test.liva", R"--(
        extern "C" func printf(fmt: string, ...) -> i32
        func main() {
            println(0)
        }
    )--");
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, FFI_ExternBlockForm) {
    auto result = runPipeline("test.liva", R"--(
        extern "C" {
            func malloc(size: u64) -> ref i8
            func free(ptr: ref i8)
            func strlen(str: ref i8) -> u64
        }
        func main() {
            println(0)
        }
    )--");
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, FFI_ExternCallFromMain) {
    auto result = runPipeline("test.liva", R"--(
        extern "C" func c_abs(x: i32) -> i32
        func main() {
            let y: i32 = c_abs(-5)
            println(y)
        }
    )--");
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, FFI_ExternRefParams) {
    auto result = runPipeline("test.liva", R"--(
        extern "C" func memcpy(dst: ref i8, src: ref i8, n: u64) -> ref i8
        func main() {
            println(0)
        }
    )--");
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, FFI_ExternVoidReturn) {
    auto result = runPipeline("test.liva", R"--(
        extern "C" func free(ptr: ref i8)
        func main() {
            println(0)
        }
    )--");
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
    EXPECT_TRUE(result.errors.empty());
}

// ============================================================
// WASM Backend Tests
// ============================================================

TEST_F(IntegrationTest, WASM_PipelineParseAndSema) {
    auto result = runPipeline("test.liva", R"--(
        func add(a: i32, b: i32) -> i32 {
            return a + b
        }
        func main() {
            let x: i32 = add(2, 3)
            println(x)
        }
    )--");
    EXPECT_TRUE(result.parseSuccess);
    EXPECT_TRUE(result.semaSuccess);
    EXPECT_TRUE(result.errors.empty());
}

TEST_F(IntegrationTest, WASM_OutputExtension) {
    // Verify that .wasm extension is used for wasm targets
    std::string output = "myapp";
    std::string target = "wasm32-unknown-wasi";
    if (target.find("wasm32") == 0 || target.find("wasm64") == 0) {
        output += ".wasm";
    }
    EXPECT_EQ(output, "myapp.wasm");

    // Non-wasm target should not get .wasm
    std::string output2 = "myapp";
    std::string target2 = "x86_64-pc-linux-gnu";
    if (target2.find("wasm32") == 0 || target2.find("wasm64") == 0) {
        output2 += ".wasm";
    }
    EXPECT_EQ(output2, "myapp");
}

#ifdef LIVA_HAS_LLVM
TEST_F(IntegrationTest, WASM_TargetInfoIsWasm32) {
    auto ti = liva::TargetInfo::fromTriple("wasm32-unknown-wasi");
    EXPECT_TRUE(ti.isWasm());
}

TEST_F(IntegrationTest, WASM_TargetInfoIsWasm64) {
    auto ti = liva::TargetInfo::fromTriple("wasm64-unknown-unknown");
    EXPECT_TRUE(ti.isWasm());
}

TEST_F(IntegrationTest, WASM_TargetInfoNonWasm) {
    auto ti = liva::TargetInfo::fromTriple("x86_64-pc-windows-msvc");
    EXPECT_FALSE(ti.isWasm());
}

TEST_F(IntegrationTest, WASM_TargetInfoCrossCompiling) {
    auto ti = liva::TargetInfo::fromTriple("wasm32-unknown-wasi");
    EXPECT_TRUE(ti.isCrossCompiling());
}

TEST_F(IntegrationTest, WASM_ProjectConfigWasm) {
    auto ti = liva::TargetInfo::fromTriple("wasm32-unknown-wasi");
    EXPECT_EQ(ti.triple, "wasm32-unknown-wasi");
    EXPECT_TRUE(ti.isWasm());
    EXPECT_TRUE(ti.isCrossCompiling());
}

TEST_F(IntegrationTest, WASM_CrossCompileTarget) {
    auto ti = liva::TargetInfo::fromTriple("wasm32-unknown-emscripten");
    EXPECT_TRUE(ti.isWasm());
    EXPECT_TRUE(ti.isCrossCompiling());
    EXPECT_EQ(ti.triple, "wasm32-unknown-emscripten");
}
#endif // LIVA_HAS_LLVM

// ========== O6: Benchmark Subcommand Enum ==========

TEST(BenchCommandTest, BenchSubcommandEnumExists) {
    // Verify Bench enum value exists and is distinct
    liva::Subcommand bench = liva::Subcommand::Bench;
    EXPECT_NE(bench, liva::Subcommand::None);
    EXPECT_NE(bench, liva::Subcommand::Build);
    EXPECT_NE(bench, liva::Subcommand::Run);
}

TEST(BenchCommandTest, BenchDriverOptionsDefault) {
    liva::DriverOptions opts;
    EXPECT_EQ(opts.subcommand, liva::Subcommand::None);
    // Verify Bench can be assigned
    opts.subcommand = liva::Subcommand::Bench;
    EXPECT_EQ(opts.subcommand, liva::Subcommand::Bench);
}

TEST(BenchCommandTest, BenchDriverOptionsWithFile) {
    liva::DriverOptions opts;
    opts.subcommand = liva::Subcommand::Bench;
    opts.inputFile = "mybench.liva";
    EXPECT_EQ(opts.subcommand, liva::Subcommand::Bench);
    EXPECT_EQ(opts.inputFile, "mybench.liva");
}

// ========== O3: Test Framework ==========

TEST(TestCommandTest, TestSubcommandEnumExists) {
    liva::Subcommand test = liva::Subcommand::Test;
    EXPECT_NE(test, liva::Subcommand::None);
    EXPECT_NE(test, liva::Subcommand::Build);
    EXPECT_NE(test, liva::Subcommand::Bench);
}

TEST(TestCommandTest, TestDriverOptionsDefault) {
    liva::DriverOptions opts;
    EXPECT_EQ(opts.subcommand, liva::Subcommand::None);
    opts.subcommand = liva::Subcommand::Test;
    EXPECT_EQ(opts.subcommand, liva::Subcommand::Test);
}

TEST(TestCommandTest, TestDriverOptionsWithFile) {
    liva::DriverOptions opts;
    opts.subcommand = liva::Subcommand::Test;
    opts.inputFile = "mytest.liva";
    EXPECT_EQ(opts.subcommand, liva::Subcommand::Test);
    EXPECT_EQ(opts.inputFile, "mytest.liva");
}

// ========== D1: JIT Compilation ==========

#ifdef LIVA_HAS_LLVM

TEST(JITTest, JITEngineCreate) {
    auto jit = JITEngine::create();
    EXPECT_NE(jit, nullptr);
}

TEST(JITTest, JITEngineEvalSimple) {
    auto jit = JITEngine::create();
    ASSERT_NE(jit, nullptr);

    CompilerInstance compiler;
    compiler.setSource("jit_simple.liva", "func main() { println(\"hello\") }");
    auto ir = compiler.compileToIR();
    ASSERT_TRUE(ir.has_value());

    std::string err;
    int code = jit->evaluate(std::move(ir->context), std::move(ir->module), err);
    EXPECT_EQ(code, 0) << "JIT error: " << err;
}

TEST(JITTest, JITEngineEvalArithmetic) {
    auto jit = JITEngine::create();
    ASSERT_NE(jit, nullptr);

    CompilerInstance compiler;
    compiler.setSource("jit_arith.liva",
        "func main() {\n    let x = 2 + 3\n    println(x)\n}");
    auto ir = compiler.compileToIR();
    ASSERT_TRUE(ir.has_value());

    std::string err;
    int code = jit->evaluate(std::move(ir->context), std::move(ir->module), err);
    EXPECT_EQ(code, 0) << "JIT error: " << err;
}

TEST(JITTest, JITEngineEvalMultiple) {
    auto jit = JITEngine::create();
    ASSERT_NE(jit, nullptr);

    // First evaluation
    {
        CompilerInstance compiler;
        compiler.setSource("jit_multi1.liva", "func main() { println(1) }");
        auto ir = compiler.compileToIR();
        ASSERT_TRUE(ir.has_value());

        std::string err;
        int code = jit->evaluate(std::move(ir->context), std::move(ir->module), err);
        EXPECT_EQ(code, 0) << "First eval error: " << err;
    }

    // Second evaluation on same engine
    {
        CompilerInstance compiler;
        compiler.setSource("jit_multi2.liva", "func main() { println(2) }");
        auto ir = compiler.compileToIR();
        ASSERT_TRUE(ir.has_value());

        std::string err;
        int code = jit->evaluate(std::move(ir->context), std::move(ir->module), err);
        EXPECT_EQ(code, 0) << "Second eval error: " << err;
    }
}

TEST(JITTest, JITEngineEvalMissedMain) {
    auto jit = JITEngine::create();
    ASSERT_NE(jit, nullptr);

    // Module without main — compileToIR should fail because IRGen requires main by default
    CompilerInstance compiler;
    compiler.setSource("jit_nomain.liva", "func helper() -> i32 { return 42 }");
    auto ir = compiler.compileToIR();
    // IRGen requires main(), so compileToIR should return nullopt
    EXPECT_FALSE(ir.has_value());
}

TEST(JITTest, JITEngineEvalBadIR) {
    auto jit = JITEngine::create();
    ASSERT_NE(jit, nullptr);

    // Syntax error — compileToIR should fail
    CompilerInstance compiler;
    compiler.setSource("jit_bad.liva", "func main( { }");
    auto ir = compiler.compileToIR();
    EXPECT_FALSE(ir.has_value());
}

TEST(JITTest, CompileToIRSimple) {
    CompilerInstance compiler;
    compiler.setSource("ir_simple.liva", "func main() { println(0) }");
    auto ir = compiler.compileToIR();
    ASSERT_TRUE(ir.has_value());
    EXPECT_NE(ir->context, nullptr);
    EXPECT_NE(ir->module, nullptr);
}

TEST(JITTest, CompileToIRError) {
    CompilerInstance compiler;
    compiler.setSource("ir_err.liva", "func main( { }");
    auto ir = compiler.compileToIR();
    EXPECT_FALSE(ir.has_value());
}

TEST(JITTest, CompileToIRPreservesFunction) {
    CompilerInstance compiler;
    compiler.setSource("ir_func.liva", "func main() { println(42) }");
    auto ir = compiler.compileToIR();
    ASSERT_TRUE(ir.has_value());
    // Module should contain a "main" function
    auto *mainFn = ir->module->getFunction("main");
    EXPECT_NE(mainFn, nullptr);
}

TEST(JITTest, JITEngineEvalWithRuntime) {
    auto jit = JITEngine::create();
    ASSERT_NE(jit, nullptr);

    CompilerInstance compiler;
    compiler.setSource("jit_runtime.liva",
        "func main() {\n    assert(true)\n    println(\"runtime ok\")\n}");
    auto ir = compiler.compileToIR();
    ASSERT_TRUE(ir.has_value());

    std::string err;
    int code = jit->evaluate(std::move(ir->context), std::move(ir->module), err);
    EXPECT_EQ(code, 0) << "JIT error: " << err;
}

// A generic struct literal whose type parameter cannot be inferred used to
// lower the field as an opaque pointer array and silently miscompile. It is
// now rejected with a diagnostic that names the escape hatch.
namespace {
bool hasIRGenDiag(CompilerInstance &compiler, DiagID id) {
    for (const auto &d : compiler.getDiag().getDiagnostics())
        if (d.id == id)
            return true;
    return false;
}

std::string irGenDiagMessage(CompilerInstance &compiler, DiagID id) {
    for (const auto &d : compiler.getDiag().getDiagnostics())
        if (d.id == id)
            return d.message;
    return {};
}
} // namespace

TEST(IRGenDiagTest, GenericStructTypeArgsUninferrable) {
    CompilerInstance compiler;
    compiler.setSource("generic_uninferrable.liva",
        "struct Box<T> {\n"
        "    var v: [T]\n"
        "}\n"
        "func main() {\n"
        "    var b = Box { v: [] }\n"
        "    println(b.v.length)\n"
        "}\n");
    auto ir = compiler.compileToIR();
    EXPECT_FALSE(ir.has_value());
    EXPECT_TRUE(hasIRGenDiag(compiler,
                             DiagID::err_generic_struct_type_args_uninferred));
}

TEST(IRGenDiagTest, GenericStructTypeArgsSuggestionMatchesArity) {
    // A two-parameter struct must not be told to write a one-argument list.
    CompilerInstance compiler;
    compiler.setSource("generic_arity.liva",
        "struct Pair<A, B> {\n"
        "    var xs: [A]\n"
        "    var ys: [B]\n"
        "}\n"
        "func main() {\n"
        "    var p = Pair { xs: [], ys: [] }\n"
        "    println(p.xs.length)\n"
        "}\n");
    auto ir = compiler.compileToIR();
    EXPECT_FALSE(ir.has_value());
    const std::string msg = irGenDiagMessage(
        compiler, DiagID::err_generic_struct_type_args_uninferred);
    EXPECT_NE(msg.find("Pair<i32, i32>"), std::string::npos)
        << "message: " << msg;
}

TEST(IRGenDiagTest, GenericStructTypeArgsExplicitAccepted) {
    // Same literal with the type argument written out compiles cleanly.
    CompilerInstance compiler;
    compiler.setSource("generic_explicit.liva",
        "struct Box<T> {\n"
        "    var v: [T]\n"
        "}\n"
        "func main() {\n"
        "    var b = Box<i32> { v: [] }\n"
        "    println(b.v.length)\n"
        "}\n");
    auto ir = compiler.compileToIR();
    EXPECT_TRUE(ir.has_value());
    EXPECT_FALSE(hasIRGenDiag(compiler,
                              DiagID::err_generic_struct_type_args_uninferred));
}

TEST(IRGenDiagTest, GenericStructTypeArgsInferredFromArrayLiteral) {
    // Inference from the array literal's first element — no diagnostic.
    CompilerInstance compiler;
    compiler.setSource("generic_inferred.liva",
        "struct Box<T> {\n"
        "    var v: [T]\n"
        "}\n"
        "func main() {\n"
        "    let b = Box { v: [1, 2, 3] }\n"
        "    println(b.v.length)\n"
        "}\n");
    auto ir = compiler.compileToIR();
    EXPECT_TRUE(ir.has_value());
    EXPECT_FALSE(hasIRGenDiag(compiler,
                              DiagID::err_generic_struct_type_args_uninferred));
}

#endif // LIVA_HAS_LLVM


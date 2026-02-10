#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/ModuleLoader.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>

using namespace liva;

class SemaTest : public ::testing::Test {
protected:
    struct CheckResult {
        std::unique_ptr<SourceManager> sm;
        DiagnosticsEngine diag;
        std::unique_ptr<TranslationUnit> tu;
        bool passed;
    };

    CheckResult check(const std::string &source) {
        CheckResult result;
        result.sm = std::make_unique<SourceManager>("test.liva", source);
        result.diag.setSourceManager(result.sm.get());
        Lexer lexer(*result.sm, result.diag);
        Parser parser(lexer, result.diag);
        result.tu = parser.parseTranslationUnit();

        if (result.diag.hasErrors()) {
            result.passed = false;
            return result;
        }

        Sema sema(result.diag);
        result.passed = sema.analyze(*result.tu);
        return result;
    }

    bool hasDiag(const CheckResult &result, DiagID id) {
        for (auto &d : result.diag.getDiagnostics()) {
            if (d.id == id)
                return true;
        }
        return false;
    }

    CheckResult checkWithModules(
        const std::string &mainSource,
        std::initializer_list<std::pair<std::string, std::string>> modules) {
        CheckResult result;
        result.sm = std::make_unique<SourceManager>("test.liva", mainSource);
        result.diag.setSourceManager(result.sm.get());
        Lexer lexer(*result.sm, result.diag);
        Parser parser(lexer, result.diag);
        result.tu = parser.parseTranslationUnit();
        if (result.diag.hasErrors()) {
            result.passed = false;
            return result;
        }
        ModuleLoader loader;
        for (auto &[name, src] : modules)
            loader.registerSource(name, src);
        Sema sema(result.diag, &loader);
        result.passed = sema.analyze(*result.tu);
        return result;
    }
};

TEST_F(SemaTest, ValidSimpleFunction) {
    auto result = check(R"(
        func main() {
            let x: i32 = 42
            println(x)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, UndeclaredVariable) {
    auto result = check(R"(
        func main() {
            println(undefined_var)
        }
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_undeclared_identifier));
}

TEST_F(SemaTest, Redefinition) {
    auto result = check(R"(
        func foo() {}
        func foo() {}
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_redefinition));
}

TEST_F(SemaTest, AssignToImmutable) {
    auto result = check(R"(
        func main() {
            let x: i32 = 42
            x = 10
        }
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_assign_to_immutable));
}

TEST_F(SemaTest, AssignToMutable) {
    auto result = check(R"(
        func main() {
            var x: i32 = 42
            x = 10
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, BreakOutsideLoop) {
    auto result = check(R"(
        func main() {
            break
        }
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_break_outside_loop));
}

TEST_F(SemaTest, ContinueOutsideLoop) {
    auto result = check(R"(
        func main() {
            continue
        }
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_continue_outside_loop));
}

TEST_F(SemaTest, BreakInsideLoop) {
    auto result = check(R"(
        func main() {
            var i: i32 = 0
            while i < 10 {
                break
            }
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StructDeclaration) {
    auto result = check(R"(
        struct Point {
            var x: f64
            var y: f64
        }

        func main() {
            let p = Point { x: 1.0, y: 2.0 }
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, UndefinedStruct) {
    auto result = check(R"(
        func main() {
            let p = UnknownType { x: 1 }
        }
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_undefined_type));
}

TEST_F(SemaTest, FunctionWithIfElse) {
    auto result = check(R"(
        func abs(x: i32) -> i32 {
            if x < 0 {
                return -x
            }
            return x
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, EnumAndMatch) {
    auto result = check(R"(
        enum Color {
            case Red
            case Green
            case Blue
        }

        func main() {
            let c = Color.Green
            match c {
                Color.Red => println(0)
                Color.Green => println(1)
                _ => println(2)
            }
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, EnumWithAssociatedValues) {
    auto result = check(R"(
        enum Shape {
            case Circle(f64)
            case Rectangle(f64, f64)
            case Empty
        }

        func main() {
            let s = Shape.Circle(3.14)
            match s {
                Shape.Circle(r) => println(r)
                Shape.Rectangle(w, h) => println(w)
                _ => println(0)
            }
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, BreakInWhileLoop) {
    auto result = check(R"(
        func main() {
            var i: i32 = 0
            while i < 10 {
                if i == 5 { break }
                i = i + 1
            }
            println(i)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ContinueInForLoop) {
    auto result = check(R"(
        func main() {
            var sum: i32 = 0
            for i in 0..10 {
                if i == 3 { continue }
                sum = sum + i
            }
            println(sum)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ForLoop) {
    auto result = check(R"(
        func main() {
            for i in items {
                println(i)
            }
        }
    )");
    // 'items' is undefined but for-loop variable 'i' should be in scope
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_undeclared_identifier));
}

TEST_F(SemaTest, ArrayLiteral) {
    auto result = check(R"(
        func main() {
            let arr = [1, 2, 3]
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ArrayIndexAccess) {
    auto result = check(R"(
        func main() {
            let arr = [10, 20, 30]
            let x = arr[0]
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MutableArrayIndexAssign) {
    auto result = check(R"(
        func main() {
            var arr = [1, 2, 3]
            arr[1] = 42
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ArrayWithPrintln) {
    auto result = check(R"(
        func main() {
            let arr = [10, 20, 30]
            println(arr[0])
        }
    )");
    EXPECT_TRUE(result.passed);
}

// === Match Exhaustiveness Tests ===

TEST_F(SemaTest, MatchExhaustiveAllCases) {
    auto result = check(R"(
        enum Color {
            case Red
            case Green
            case Blue
        }

        func main() {
            let c = Color.Red
            match c {
                Color.Red => println(0)
                Color.Green => println(1)
                Color.Blue => println(2)
            }
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MatchExhaustiveWithWildcard) {
    auto result = check(R"(
        enum Color {
            case Red
            case Green
            case Blue
        }

        func main() {
            let c = Color.Red
            match c {
                Color.Red => println(0)
                _ => println(1)
            }
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MatchNonExhaustive) {
    auto result = check(R"(
        enum Color {
            case Red
            case Green
            case Blue
        }

        func main() {
            let c = Color.Red
            match c {
                Color.Red => println(0)
                Color.Green => println(1)
            }
        }
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_nonexhaustive_match));
}

TEST_F(SemaTest, MatchNonExhaustiveSingleCase) {
    auto result = check(R"(
        enum Color {
            case Red
            case Green
            case Blue
        }

        func main() {
            let c = Color.Red
            match c {
                Color.Red => println(0)
            }
        }
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_nonexhaustive_match));
}

TEST_F(SemaTest, MatchDuplicateArm) {
    auto result = check(R"(
        enum Color {
            case Red
            case Green
            case Blue
        }

        func main() {
            let c = Color.Red
            match c {
                Color.Red => println(0)
                Color.Red => println(1)
                Color.Green => println(2)
                Color.Blue => println(3)
            }
        }
    )");
    EXPECT_TRUE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::warn_unreachable_match_arm));
}

TEST_F(SemaTest, MatchExhaustiveAssociatedValues) {
    auto result = check(R"(
        enum Shape {
            case Circle(f64)
            case Rectangle(f64, f64)
            case Empty
        }

        func main() {
            let s = Shape.Circle(3.14)
            match s {
                Shape.Circle(r) => println(r)
                Shape.Rectangle(w, h) => println(w)
                Shape.Empty => println(0)
            }
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MatchNonExhaustiveAssociatedValues) {
    auto result = check(R"(
        enum Shape {
            case Circle(f64)
            case Rectangle(f64, f64)
            case Empty
        }

        func main() {
            let s = Shape.Circle(3.14)
            match s {
                Shape.Circle(r) => println(r)
                Shape.Rectangle(w, h) => println(w)
            }
        }
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_nonexhaustive_match));
}

TEST_F(SemaTest, MatchIntegerNoExhaustivenessRequired) {
    auto result = check(R"(
        func main() {
            let x: i32 = 5
            match x {
                1 => println(1)
                2 => println(2)
            }
        }
    )");
    EXPECT_TRUE(result.passed);
}

// === String Operations Tests ===

TEST_F(SemaTest, StringConcat) {
    auto result = check(R"(
        func main() {
            let a = "hello"
            let b = " world"
            let c = a + b
            println(c)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringComparison) {
    auto result = check(R"(
        func main() {
            let a = "hello"
            let b = "hello"
            if a == b {
                println(1)
            }
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringLen) {
    auto result = check(R"(
        func main() {
            let n = len("hello")
            println(n)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringLengthProperty) {
    auto result = check(R"(
        func main() {
            let s = "hello"
            let n = s.length
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringInterpolation) {
    auto result = check(R"--(
        func main() {
            let name = "world"
            let msg = "hello \(name)"
            println(msg)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringConcatChain) {
    auto result = check(R"(
        func main() {
            let a = "a" + "b" + "c"
            println(a)
        }
    )");
    EXPECT_TRUE(result.passed);
}

// === Generic Function Tests ===

TEST_F(SemaTest, GenericFunctionDecl) {
    auto result = check(R"(
        func identity<T>(x: T) -> T { return x }
        func main() {}
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GenericFunctionCallI32) {
    auto result = check(R"(
        func identity<T>(x: T) -> T { return x }
        func main() {
            let a = identity(42)
            println(a)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GenericFunctionCallF64) {
    auto result = check(R"(
        func identity<T>(x: T) -> T { return x }
        func main() {
            let b = identity(3.14)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GenericFunctionCallString) {
    auto result = check(R"(
        func identity<T>(x: T) -> T { return x }
        func main() {
            let c = identity("hello")
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GenericFunctionMultiTypeParams) {
    auto result = check(R"(
        func first<T, U>(a: T, b: U) -> T { return a }
        func main() {
            let x = first(42, 3.14)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GenericStructDecl) {
    auto result = check(R"(
        struct Box<T> { let data: T }
        func main() {}
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GenericStructLiteralI32) {
    auto result = check(R"(
        struct Box<T> { let data: T }
        func main() {
            let b = Box { data: 42 }
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GenericStructLiteralF64) {
    auto result = check(R"(
        struct Box<T> { let data: T }
        func main() {
            let b = Box { data: 3.14 }
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GenericStructPair) {
    auto result = check(R"(
        struct Pair<T, U> {
            let first: T
            let second: U
        }
        func main() {
            let p = Pair { first: 42, second: 3.14 }
        }
    )");
    EXPECT_TRUE(result.passed);
}

// === Dynamic Array Tests ===

TEST_F(SemaTest, DynArrayEmpty) {
    auto result = check(R"(
        func main() {
            var arr: [i32] = []
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayInit) {
    auto result = check(R"(
        func main() {
            var arr: [i32] = [1, 2, 3]
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayPush) {
    auto result = check(R"(
        func main() {
            var arr: [i32] = [1, 2, 3]
            arr.push(42)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayPop) {
    auto result = check(R"(
        func main() {
            var arr: [i32] = [1, 2, 3]
            arr.pop()
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayLength) {
    auto result = check(R"(
        func main() {
            var arr: [i32] = [1, 2, 3]
            let l = arr.length
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayIndex) {
    auto result = check(R"(
        func main() {
            var arr: [i32] = [1, 2, 3]
            let x = arr[0]
            arr[1] = 99
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GenericImplDecl) {
    auto result = check(R"--(
        struct Box<T> { let data: T }
        impl Box<T> {
            func get(self) -> T { return self.data }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GenericImplMethodCall) {
    auto result = check(R"--(
        struct Box<T> { let data: T }
        impl Box<T> {
            func get(self) -> T { return self.data }
        }
        func main() {
            let b = Box { data: 42 }
            let x = b.get()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GenericImplMultiTypeParams) {
    auto result = check(R"--(
        struct Pair<T, U> { let first: T  let second: U }
        impl Pair<T, U> {
            func getFirst(self) -> T { return self.first }
        }
        func main() {
            let p = Pair { first: 1  second: 3.14 }
            let x = p.getFirst()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OptionalNilAssignment) {
    auto result = check(R"--(
        func main() {
            let x: i32? = nil
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OptionalValueAssignment) {
    auto result = check(R"--(
        func main() {
            let x: i32? = 42
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OptionalForceUnwrap) {
    auto result = check(R"--(
        func main() {
            let x: i32? = 42
            let y = x!
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OptionalMutableReassign) {
    auto result = check(R"--(
        func main() {
            var x: i32? = nil
            x = 42
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, NilWithoutOptionalType) {
    auto result = check(R"--(
        func main() {
            let x: i32 = nil
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_nil_without_optional));
}

TEST_F(SemaTest, ClosureAssignToVar) {
    auto result = check(R"--(
        func main() {
            let f: (i32) -> i32 = |x: i32| -> i32 { return x }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ClosureAsArgument) {
    auto result = check(R"--(
        func apply(f: (i32) -> i32, x: i32) -> i32 {
            return f(x)
        }
        func main() {
            let r = apply(|x: i32| -> i32 { return x + 1 }, 5)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ClosureNoParamsVoid) {
    auto result = check(R"--(
        func main() {
            let f = || { let x = 1 }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Protocol Tests ===

TEST_F(SemaTest, ProtocolDeclaration) {
    auto result = check(R"--(
        protocol Printable {
            func toString(self) -> string
        }
        func main() {}
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ProtocolConformanceValid) {
    auto result = check(R"--(
        protocol Greetable {
            func greet(self) -> string
        }
        struct Person { let name: string }
        impl Person: Greetable {
            func greet(self) -> string { return self.name }
        }
        func main() {}
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ProtocolConformanceMissingMethod) {
    auto result = check(R"--(
        protocol Greetable {
            func greet(self) -> string
        }
        struct Person { let name: string }
        impl Person: Greetable {
        }
        func main() {}
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_missing_protocol_method));
}

TEST_F(SemaTest, ProtocolUndefined) {
    auto result = check(R"--(
        struct Foo { let x: i32 }
        impl Foo: NonExistent {
        }
        func main() {}
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_undefined_protocol));
}

TEST_F(SemaTest, ProtocolMultipleMethods) {
    auto result = check(R"--(
        protocol Shape {
            func area(self) -> f64
            func name(self) -> string
        }
        struct Circle { let radius: f64 }
        impl Circle: Shape {
            func area(self) -> f64 { return self.radius }
            func name(self) -> string { return "circle" }
        }
        func main() {}
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ProtocolTraitObjectVarDecl) {
    auto result = check(R"--(
        protocol Shape {
            func area(self) -> f64
        }
        struct Circle { let radius: f64 }
        impl Circle: Shape {
            func area(self) -> f64 { return self.radius }
        }
        func main() {
            let c = Circle { radius: 3.0 }
            let s: ref Shape = c
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ProtocolTraitObjectNoConformance) {
    auto result = check(R"--(
        protocol Shape {
            func area(self) -> f64
        }
        struct Foo { let x: i32 }
        func main() {
            let f = Foo { x: 1 }
            let s: ref Shape = f
        }
    )--");
    // For now passes (conformance not checked at assignment in sema)
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ProtocolMethodCall) {
    auto result = check(R"--(
        protocol Greetable {
            func greet(self) -> i32
        }
        struct Dog { let x: i32 }
        impl Dog: Greetable {
            func greet(self) -> i32 { return self.x }
        }
        func main() {
            let d = Dog { x: 42 }
            let g: ref Greetable = d
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, IfLetBindingInScope) {
    auto result = check(R"--(
        func main() {
            let x: i32? = 42
            if let val = x {
                println(val)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, IfLetWithElse) {
    auto result = check(R"--(
        func main() {
            let x: i32? = nil
            if let val = x {
                println(val)
            } else {
                println(0)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, NilCoalesceBasic) {
    auto result = check(R"--(
        func main() {
            let x: i32? = nil
            let y = x ?? 0
            println(y)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, NilCoalesceWithValue) {
    auto result = check(R"--(
        func main() {
            let x: i32? = 42
            let y = x ?? 0
            println(y)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ResultTypeOk) {
    auto result = check(R"--(
        func main() {
            let r: Result<i32, string> = Result.ok(42)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ResultTypeErr) {
    auto result = check(R"--(
        func main() {
            let r: Result<i32, string> = Result.err("error")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ResultMatchExhaustive) {
    auto result = check(R"--(
        func main() {
            let r: Result<i32, string> = Result.ok(42)
            let x = match r {
                Result.Ok(v) => v
                Result.Err(e) => 0
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ResultMatchNonExhaustive) {
    auto result = check(R"--(
        func main() {
            let r: Result<i32, string> = Result.ok(42)
            let x = match r {
                Result.Ok(v) => v
            }
        }
    )--");
    EXPECT_FALSE(result.passed);
}

TEST_F(SemaTest, TryInResultFunction) {
    auto result = check(R"--(
        func parseNum() -> Result<i32, string> {
            return Result.ok(42)
        }
        func main() {
            let x = try parseNum()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Closure Capture Tests ===

TEST_F(SemaTest, ClosureCaptureOuterVar) {
    auto result = check(R"--(
        func main() {
            let n: i32 = 10
            let f: (i32) -> i32 = |x: i32| -> i32 { return x + n }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ClosureCapturePassedToFunction) {
    auto result = check(R"--(
        func apply(f: (i32) -> i32, x: i32) -> i32 {
            return f(x)
        }
        func main() {
            let n: i32 = 10
            let r = apply(|x: i32| -> i32 { return x + n }, 5)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ClosureCaptureMultipleVars) {
    auto result = check(R"--(
        func main() {
            let a: i32 = 1
            let b: i32 = 2
            let f: (i32) -> i32 = |x: i32| -> i32 { return x + a + b }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === M16b: Closure Type Inference ===

TEST_F(SemaTest, ClosureTypeInferenceFromVar) {
    auto result = check(R"--(
        func main() {
            let f: (i32) -> i32 = |x| -> i32 { return x + 1 }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ClosureTypeInferenceFromParam) {
    auto result = check(R"--(
        func apply(f: (i32) -> i32, x: i32) -> i32 {
            return f(x)
        }
        func main() {
            let r = apply(|x| -> i32 { return x + 1 }, 5)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ClosureTypeInferenceMultiParam) {
    auto result = check(R"--(
        func main() {
            let f: (i32, i32) -> i32 = |x, y| -> i32 { return x + y }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === M16c: Trailing Closure Syntax ===

TEST_F(SemaTest, TrailingClosureSema) {
    auto result = check(R"--(
        func apply(x: i32, f: (i32) -> i32) -> i32 {
            return f(x)
        }
        func main() {
            let r = apply(5) |x: i32| -> i32 { return x + 1 }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TrailingClosureWithInference) {
    auto result = check(R"--(
        func apply(x: i32, f: (i32) -> i32) -> i32 {
            return f(x)
        }
        func main() {
            let r = apply(5) |x| -> i32 { return x + 1 }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ProtocolDefaultImplValid) {
    auto result = check(R"--(
        protocol Greetable {
            func greet(self) -> string
            func shout(self) -> string {
                return self.greet()
            }
        }
        struct Person { name: string }
        impl Person: Greetable {
            func greet(self) -> string { return self.name }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ProtocolDefaultImplOverride) {
    auto result = check(R"--(
        protocol Greetable {
            func greet(self) -> string
            func shout(self) -> string {
                return self.greet()
            }
        }
        struct Person { name: string }
        impl Person: Greetable {
            func greet(self) -> string { return self.name }
            func shout(self) -> string { return self.name }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ProtocolDefaultImplMissingRequired) {
    auto result = check(R"--(
        protocol Greetable {
            func greet(self) -> string
            func shout(self) -> string {
                return self.greet()
            }
        }
        struct Person { name: string }
        impl Person: Greetable {
            func shout(self) -> string { return self.name }
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_missing_protocol_method));
}

TEST_F(SemaTest, ProtocolDefaultImplMixed) {
    auto result = check(R"--(
        protocol Describable {
            func name(self) -> string
            func id(self) -> i32
            func describe(self) -> string {
                return self.name()
            }
        }
        struct Item { label: string }
        impl Item: Describable {
            func name(self) -> string { return self.label }
            func id(self) -> i32 { return 1 }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === M17: Module System Tests ===

TEST_F(SemaTest, ModuleImportPubFunc) {
    auto result = checkWithModules(R"--(
        import math
        func main() { let x = add(1, 2) }
    )--", {{"math", "pub func add(a: i32, b: i32) -> i32 { return a + b }"}});
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ModuleImportNonPubFunc) {
    auto result = checkWithModules(R"--(
        import math
        func main() { let x = helper() }
    )--", {{"math", R"--(
        pub func add(a: i32, b: i32) -> i32 { return a + b }
        func helper() -> i32 { return 0 }
    )--"}});
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_undeclared_identifier));
}

TEST_F(SemaTest, ModuleNotFound) {
    auto result = checkWithModules(R"--(
        import nonexistent
        func main() {}
    )--", {});
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_module_not_found));
}

TEST_F(SemaTest, ModuleCircularImport) {
    auto result = checkWithModules(R"--(
        import b
        pub func mainFunc() -> i32 { return 1 }
        func main() {}
    )--", {{"b", "import test"}});
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_circular_import) ||
                hasDiag(result, DiagID::err_module_not_found));
}

TEST_F(SemaTest, ModuleImportPubStruct) {
    auto result = checkWithModules(R"--(
        import types
        func main() { let p = Point { x: 1, y: 2 } }
    )--", {{"types", R"--(
        pub struct Point {
            x: i32
            y: i32
        }
    )--"}});
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ModuleMultipleImports) {
    auto result = checkWithModules(R"--(
        import math
        import types
        func main() {
            let x = add(1, 2)
            let p = Point { x: 1, y: 2 }
        }
    )--", {
        {"math", "pub func add(a: i32, b: i32) -> i32 { return a + b }"},
        {"types", R"--(
            pub struct Point {
                x: i32
                y: i32
            }
        )--"}
    });
    EXPECT_TRUE(result.passed);
}

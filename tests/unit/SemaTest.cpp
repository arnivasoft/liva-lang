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
        func my_abs(x: i32) -> i32 {
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

// --- Trait Bound Tests ---

TEST_F(SemaTest, TraitBoundFuncValid) {
    auto result = check(R"--(
        protocol Printable {
            func toString(self) -> string
        }
        struct Person {
            name: string
        }
        impl Person: Printable {
            func toString(self) -> string { return self.name }
        }
        func show<T: Printable>(item: T) -> string { return item.toString() }
        func main() {
            let p = Person { name: "Alice" }
            let s = show(p)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TraitBoundFuncViolation) {
    auto result = check(R"--(
        protocol Printable {
            func toString(self) -> string
        }
        struct Foo {
            x: i32
        }
        func show<T: Printable>(item: T) -> string { return item.toString() }
        func main() {
            let f = Foo { x: 1 }
            let s = show(f)
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_no_conformance));
}

TEST_F(SemaTest, TraitBoundPrimitiveViolation) {
    auto result = check(R"--(
        protocol Printable {
            func toString(self) -> string
        }
        func show<T: Printable>(item: T) -> string { return item.toString() }
        func main() {
            let s = show(42)
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_no_conformance));
}

TEST_F(SemaTest, TraitBoundStructValid) {
    auto result = check(R"--(
        protocol Printable {
            func toString(self) -> string
        }
        struct Person {
            name: string
        }
        impl Person: Printable {
            func toString(self) -> string { return self.name }
        }
        struct Wrapper<T: Printable> {
            value: T
        }
        func main() {
            let p = Person { name: "Alice" }
            let w = Wrapper { value: p }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TraitBoundStructViolation) {
    auto result = check(R"--(
        protocol Printable {
            func toString(self) -> string
        }
        struct Foo {
            x: i32
        }
        struct Wrapper<T: Printable> {
            value: T
        }
        func main() {
            let f = Foo { x: 1 }
            let w = Wrapper { value: f }
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_no_conformance));
}

TEST_F(SemaTest, TraitBoundMixedParams) {
    auto result = check(R"--(
        protocol Printable {
            func toString(self) -> string
        }
        struct Person {
            name: string
        }
        impl Person: Printable {
            func toString(self) -> string { return self.name }
        }
        func combine<T: Printable, U>(a: T, b: U) -> string { return a.toString() }
        func main() {
            let p = Person { name: "Alice" }
            let s = combine(p, 42)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TraitBoundUndefinedProtocol) {
    auto result = check(R"--(
        func show<T: NonExistent>(item: T) -> string { return item.toString() }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_undefined_protocol));
}

TEST_F(SemaTest, TraitBoundNoBoundBackwardCompat) {
    auto result = check(R"--(
        func identity<T>(x: T) -> T { return x }
        func main() {
            let a = identity(42)
            let b = identity("hello")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, WhereClauseValid) {
    auto result = check(R"--(
        protocol Printable {
            func toString(self) -> string
        }
        struct Person {
            name: string
        }
        impl Person: Printable {
            func toString(self) -> string { return self.name }
        }
        func show<T>(item: T) -> string where T: Printable { return item.toString() }
        func main() {
            let p = Person { name: "Alice" }
            let s = show(p)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, WhereClauseViolation) {
    auto result = check(R"--(
        protocol Printable {
            func toString(self) -> string
        }
        struct Foo {
            x: i32
        }
        func show<T>(item: T) -> string where T: Printable { return item.toString() }
        func main() {
            let f = Foo { x: 1 }
            let s = show(f)
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_no_conformance));
}

TEST_F(SemaTest, WhereClauseUndefinedProtocol) {
    auto result = check(R"--(
        func show<T>(item: T) -> string where T: NonExistent { return item.toString() }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_undefined_protocol));
}

TEST_F(SemaTest, RefExprTypeResolution) {
    auto result = check(R"--(
        func main() {
            let x: i32 = 42
            let r = ref x
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, RefParamFunction) {
    auto result = check(R"--(
        func read_val(x: ref i32) -> i32 {
            return x
        }
        func main() {
            var a: i32 = 10
            let b = read_val(ref a)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, RefMutParamFunction) {
    auto result = check(R"--(
        func increment(x: ref mut i32) {
            x = x + 1
        }
        func main() {
            var a: i32 = 10
            increment(ref mut a)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, RefPassThrough) {
    auto result = check(R"--(
        func read_val(x: ref i32) -> i32 {
            return x
        }
        func pass_ref(x: ref i32) -> i32 {
            return read_val(ref x)
        }
        func main() {
            var a: i32 = 42
            let b = pass_ref(ref a)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OptionalChainingOnOptionalVar) {
    auto result = check(R"--(
        struct Point {
            x: i32
            y: i32
        }
        func main() {
            var p: Point? = nil
            let val = p?.x
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OptionalChainingWithNilCoalescing) {
    auto result = check(R"--(
        struct Point {
            x: i32
            y: i32
        }
        func main() {
            var p: Point? = nil
            let val = p?.x ?? 0
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// --- Math built-in tests ---

TEST_F(SemaTest, MathAbsI32) {
    auto result = check(R"--(
        func main() {
            let x: i32 = -5
            let y = abs(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathMinI32) {
    auto result = check(R"--(
        func main() {
            let a: i32 = 3
            let b: i32 = 7
            let c = min(a, b)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathMaxI32) {
    auto result = check(R"--(
        func main() {
            let a: i32 = 3
            let b: i32 = 7
            let c = max(a, b)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathSqrt) {
    auto result = check(R"--(
        func main() {
            let x: f64 = 9.0
            let y = sqrt(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathPow) {
    auto result = check(R"--(
        func main() {
            let x: f64 = 2.0
            let y: f64 = 3.0
            let z = pow(x, y)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathFloor) {
    auto result = check(R"--(
        func main() {
            let x: f64 = 3.7
            let y = floor(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathCeil) {
    auto result = check(R"--(
        func main() {
            let x: f64 = 3.2
            let y = ceil(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathLog) {
    auto result = check(R"--(
        func main() {
            let x: f64 = 2.718
            let y = log(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathLog10) {
    auto result = check(R"--(
        func main() {
            let x: f64 = 100.0
            let y = log10(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathSin) {
    auto result = check(R"--(
        func main() {
            let x: f64 = 1.57
            let y = sin(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathCos) {
    auto result = check(R"--(
        func main() {
            let x: f64 = 0.0
            let y = cos(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathTan) {
    auto result = check(R"--(
        func main() {
            let x: f64 = 0.785
            let y = tan(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathRound) {
    auto result = check(R"--(
        func main() {
            let x: f64 = 10.44
            let y = round(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MathRoundWithDigits) {
    auto result = check(R"--(
        func main() {
            let x: f64 = 126.46415
            let a = round(x, 2)
            let b = round(x, 0)
            let c: i32 = -2
            let d = round(x, c)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ===== Map/Set Tests =====

TEST_F(SemaTest, MapDeclare) {
    auto result = check(R"--(
        func main() {
            var m: Map<string, i32>
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MapInsert) {
    auto result = check(R"--(
        func main() {
            var m: Map<string, i32>
            m.insert("key", 42)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MapGet) {
    auto result = check(R"--(
        func main() {
            var m: Map<string, i32>
            m.insert("key", 42)
            let v = m.get("key")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MapContains) {
    auto result = check(R"--(
        func main() {
            var m: Map<string, i32>
            m.insert("key", 42)
            let has = m.contains("key")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MapRemove) {
    auto result = check(R"--(
        func main() {
            var m: Map<string, i32>
            m.insert("key", 42)
            m.remove("key")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MapSize) {
    auto result = check(R"--(
        func main() {
            var m: Map<string, i32>
            let n = m.size
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, SetDeclare) {
    auto result = check(R"--(
        func main() {
            var s: Set<i32>
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, SetInsertContains) {
    auto result = check(R"--(
        func main() {
            var s: Set<i32>
            s.insert(42)
            let has = s.contains(42)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === M20b: I/O Tests ===

TEST_F(SemaTest, ReadLineFunction) {
    auto result = check(R"--(
        func main() {
            let s = readLine()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, FormatFunction) {
    auto result = check(R"--(
        func main() {
            let s = format("{}", 42)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, FormatMultiArgs) {
    auto result = check(R"--(
        func main() {
            let s = format("{} {}", 1, 2)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, FileOpenType) {
    auto result = check(R"--(
        func main() {
            let f = File.open("a.txt", "r")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, FileReadLine) {
    auto result = check(R"--(
        func main() {
            let f = File.open("a.txt", "r")
            if let file = f {
                let line = file.readLine()
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, FileReadAll) {
    auto result = check(R"--(
        func main() {
            let f = File.open("a.txt", "r")
            if let file = f {
                let all = file.readAll()
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, FileWrite) {
    auto result = check(R"--(
        func main() {
            let f = File.open("a.txt", "w")
            if let file = f {
                file.write("hello")
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, FileWriteLine) {
    auto result = check(R"--(
        func main() {
            let f = File.open("a.txt", "w")
            if let file = f {
                file.writeLine("hello")
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, FileClose) {
    auto result = check(R"--(
        func main() {
            let f = File.open("a.txt", "r")
            if let file = f {
                file.close()
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, FileIfLetPipeline) {
    auto result = check(R"--(
        func main() {
            let f = File.open("a.txt", "r")
            if let file = f {
                let line = file.readLine()
                file.close()
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === For-in Collection Tests ===

TEST_F(SemaTest, ForInDynArray) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            for item in arr {
                let x = item
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ForInDynArrayType) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [10, 20, 30]
            for item in arr {
                let x: i32 = item
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ForInMapKey) {
    auto result = check(R"--(
        func main() {
            var m: Map<string, i32>
            for key in m {
                let k = key
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ForInMapTuple) {
    auto result = check(R"--(
        func main() {
            var m: Map<string, i32>
            for (k, v) in m {
                let key = k
                let val = v
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ForInSet) {
    auto result = check(R"--(
        func main() {
            var s: Set<i32>
            for item in s {
                let x = item
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ForInTupleNonMap) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            for (k, v) in arr {
                let x = k
            }
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_tuple_for_requires_map));
}

TEST_F(SemaTest, ForInMapTupleType) {
    auto result = check(R"--(
        func main() {
            var m: Map<string, i32>
            for (k, v) in m {
                let key: string = k
                let val: i32 = v
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ForInBreakContinue) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            for item in arr {
                if item == 2 {
                    break
                }
                continue
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === String Method Tests ===

TEST_F(SemaTest, StringContains) {
    auto result = check(R"--(
        func main() {
            let s: string = "hello world"
            let b: bool = s.contains("world")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringStartsWith) {
    auto result = check(R"--(
        func main() {
            let s: string = "hello"
            let b: bool = s.startsWith("hel")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringEndsWith) {
    auto result = check(R"--(
        func main() {
            let s: string = "hello"
            let b: bool = s.endsWith("llo")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringIndexOf) {
    auto result = check(R"--(
        func main() {
            let s: string = "hello"
            let i: i64 = s.indexOf("ll")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringSubstring) {
    auto result = check(R"--(
        func main() {
            let s: string = "hello world"
            let sub: string = s.substring(0, 5)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringTrim) {
    auto result = check(R"--(
        func main() {
            let s: string = "  hello  "
            let t: string = s.trim()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringToUpperLower) {
    auto result = check(R"--(
        func main() {
            let s: string = "Hello"
            let u: string = s.toUpper()
            let l: string = s.toLower()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringReplace) {
    auto result = check(R"--(
        func main() {
            let s: string = "hello world"
            let r: string = s.replace("world", "liva")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Type Conversion Tests ===

TEST_F(SemaTest, ParseInt) {
    auto result = check(R"--(
        func main() {
            let x: i32? = parseInt("42")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ParseInt64) {
    auto result = check(R"--(
        func main() {
            let x: i64? = parseInt64("123456789")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ParseFloat) {
    auto result = check(R"--(
        func main() {
            let x: f64? = parseFloat("3.14")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ParseIntIfLet) {
    auto result = check(R"--(
        func main() {
            let result = parseInt("42")
            if let val = result {
                println(val)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ===== M25a: String.split() =====

TEST_F(SemaTest, StringSplit) {
    auto result = check(R"--(
        func main() {
            let s: string = "a,b,c"
            let parts = s.split(",")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ===== M25b: DynArray Methods =====

TEST_F(SemaTest, DynArrayContains) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            let found: bool = arr.contains(2)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayIndexOf) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [10, 20, 30]
            let idx: i64 = arr.indexOf(20)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayReverse) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            arr.reverse()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ===== M25c: DynArray Properties =====

TEST_F(SemaTest, DynArrayLengthType) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            let n: i64 = arr.length
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayIsEmptyType) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            let empty: bool = arr.isEmpty
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ===== M26: Higher-Order Array Methods =====

TEST_F(SemaTest, DynArrayForEach) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            arr.forEach(|x: i32| {
                println(x)
            })
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayForEachInferred) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            arr.forEach(|x| {
                println(x)
            })
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayMap) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            let doubled = arr.map(|x| -> i32 { return x * 2 })
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayFilter) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3, 4, 5]
            let evens = arr.filter(|x| -> bool { return x > 2 })
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayFilterInferred) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3, 4, 5]
            arr.filter(|x: i32| -> bool { return x > 2 })
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayReduce) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3, 4, 5]
            let sum = arr.reduce(0, |acc: i32, x: i32| -> i32 { return acc + x })
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArrayReduceInferred) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3, 4, 5]
            let sum = arr.reduce(0, |acc: i32, x| -> i32 { return acc + x })
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, EnumMethodBasic) {
    auto result = check(R"--(
        enum Color {
            case Red
            case Green
            case Blue
        }
        impl Color {
            func isRed(self) -> bool {
                return true
            }
        }
        func main() {
            let c = Color.Red
            let r = c.isRed()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, EnumMethodWithParams) {
    auto result = check(R"--(
        enum Direction {
            case North
            case South
            case East
            case West
        }
        impl Direction {
            func opposite(self) -> i32 {
                return 0
            }
            func name(self) -> string {
                return "dir"
            }
        }
        func main() {
            let d = Direction.North
            let n = d.name()
            let o = d.opposite()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, WhileLetBasic) {
    auto result = check(R"--(
        func main() {
            var x: i32? = 42
            while let val = x {
                println(val)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, WhileLetBreak) {
    auto result = check(R"--(
        func main() {
            var x: i32? = 10
            while let val = x {
                break
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ===== M28: String Indexing =====

TEST_F(SemaTest, StringIndexing) {
    auto result = check(R"--(
        func main() {
            let s: string = "hello"
            let ch = s[0]
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, StringIndexingType) {
    auto result = check(R"--(
        func main() {
            let s: string = "hello"
            let ch = s[1]
            println(ch)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ===== M28: Multi-arg println =====

TEST_F(SemaTest, PrintlnMultiArg) {
    auto result = check(R"--(
        func main() {
            let x: i32 = 10
            let y: i32 = 20
            println(x, y)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, PrintlnMultiArgMixed) {
    auto result = check(R"--(
        func main() {
            let name: string = "world"
            let age: i32 = 25
            let pi: f64 = 3.14
            println(name, age, pi)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ===== M29: Array/String Slicing =====

TEST_F(SemaTest, StringSlicing) {
    auto result = check(R"--(
        func main() {
            let s: string = "hello world"
            let sub = s[0..5]
            println(sub)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DynArraySlicing) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [10, 20, 30, 40, 50]
            let sub = arr[1..3]
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ===== M29: Default Function Arguments =====

TEST_F(SemaTest, DefaultArgBasic) {
    auto result = check(R"--(
        func greet(name: string = "World") {
            println(name)
        }
        func main() {
            greet("Alice")
            greet()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DefaultArgMultiple) {
    auto result = check(R"--(
        func add(a: i32, b: i32 = 10) -> i32 {
            return a + b
        }
        func main() {
            let x = add(5, 3)
            let y = add(5)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// ===== M30: Ternary Expression =====

TEST_F(SemaTest, TernaryBasic) {
    auto result = check(R"--(
        func main() {
            let x: i32 = 10
            let y = x > 5 ? 1 : 0
            println(y)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TernaryString) {
    auto result = check(R"--(
        func main() {
            let flag: bool = true
            let msg = flag ? "yes" : "no"
            println(msg)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === M30: Type Aliases ===

TEST_F(SemaTest, TypeAliasBasic) {
    auto result = check(R"--(
        type Int = i32
        func main() {
            let x: Int = 42
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TypeAliasStruct) {
    auto result = check(R"--(
        struct Point {
            x: i32
            y: i32
        }
        type Pos = Point
        func main() {
            let p = Pos { x: 1, y: 2 }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TypeAliasUndefinedTarget) {
    auto result = check(R"--(
        type Foo = NonExistent
    )--");
    EXPECT_FALSE(result.passed);
}

TEST_F(SemaTest, TypeAliasRedefinition) {
    auto result = check(R"--(
        type Int = i32
        type Int = i64
    )--");
    EXPECT_FALSE(result.passed);
}

// --- Tuple Tests ---

TEST_F(SemaTest, TupleLiteralType) {
    auto result = check(R"--(
        func main() {
            let x: (i32, string) = (42, "hello")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TupleReturn) {
    auto result = check(R"--(
        func divmod(a: i32, b: i32) -> (i32, i32) {
            return (a, b)
        }
        func main() {
            let r = divmod(10, 3)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TupleElementAccess) {
    auto result = check(R"--(
        func main() {
            let pair = (42, "hello")
            let x = pair.0
            let y = pair.1
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TupleDestructure) {
    auto result = check(R"--(
        func main() {
            let (a, b) = (1, "hi")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TupleArityMismatch) {
    auto result = check(R"--(
        func main() {
            let (a, b, c) = (1, 2)
        }
    )--");
    EXPECT_FALSE(result.passed);
}

TEST_F(SemaTest, TupleIndexOutOfRange) {
    auto result = check(R"--(
        func main() {
            let pair = (1, 2)
            let x = pair.5
        }
    )--");
    EXPECT_FALSE(result.passed);
}

TEST_F(SemaTest, TupleInFunction) {
    auto result = check(R"--(
        func swap(a: i32, b: i32) -> (i32, i32) {
            return (b, a)
        }
        func main() {
            let (x, y) = swap(1, 2)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, TupleNested) {
    auto result = check(R"--(
        func main() {
            let t = (1, ("hello", true))
            let inner = t.1
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === M32: Closure Capture by Reference ===

TEST_F(SemaTest, ClosureCaptureByRefVar) {
    auto result = check(R"--(
        func apply(f: () -> void) {
            f()
        }
        func main() {
            var count: i32 = 0
            apply(| | -> void { count += 1 })
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ClosureCaptureImmutableMutate) {
    auto result = check(R"--(
        func apply(f: () -> void) {
            f()
        }
        func main() {
            let count: i32 = 0
            apply(| | -> void { count += 1 })
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_assign_to_immutable));
}

TEST_F(SemaTest, ClosureCaptureByRefMultiple) {
    auto result = check(R"--(
        func apply(f: () -> void) {
            f()
        }
        func main() {
            var a: i32 = 0
            var b: i32 = 0
            apply(| | -> void {
                a += 1
                b += 2
            })
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ClosureCaptureByRefReadOnly) {
    auto result = check(R"--(
        func apply(f: () -> i32) -> i32 {
            return f()
        }
        func main() {
            var x: i32 = 42
            let result: i32 = apply(| | -> i32 { return x })
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === M33: Ownership + IRGen Integration ===

TEST_F(SemaTest, OwnershipDynArrayScopeCleanup) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            arr.push(4)
            println(arr[0])
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OwnershipMapScopeCleanup) {
    auto result = check(R"--(
        func main() {
            var m: Map<string, i32>
            m.insert("a", 1)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OwnershipSetScopeCleanup) {
    auto result = check(R"--(
        func main() {
            var s: Set<i32>
            s.insert(42)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OwnershipMultipleCollections) {
    auto result = check(R"--(
        func main() {
            var arr: [i32] = [1, 2, 3]
            var m: Map<string, i32>
            var s: Set<i32>
            arr.push(4)
            m.insert("x", 10)
            s.insert(1)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Associated Types Tests ===

TEST_F(SemaTest, ProtocolWithAssociatedType) {
    auto result = check(R"--(
        protocol Container {
            type Item
            func get(self) -> i32
        }

        struct IntBox {
            value: i32
        }

        impl IntBox: Container {
            type Item = i32
            func get(self) -> i32 {
                return self.value
            }
        }

        func main() {
            let b = IntBox { value: 42 }
            println(b.get())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MissingAssociatedType) {
    auto result = check(R"--(
        protocol Container {
            type Item
            func get(self) -> i32
        }

        struct IntBox {
            value: i32
        }

        impl IntBox: Container {
            func get(self) -> i32 {
                return self.value
            }
        }

        func main() {
            let b = IntBox { value: 42 }
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_missing_associated_type));
}

TEST_F(SemaTest, ProtocolWithMultipleAssociatedTypes) {
    auto result = check(R"--(
        protocol Converter {
            type Input
            type Output
            func convert(self) -> i32
        }

        struct StrToInt {
            data: string
        }

        impl StrToInt: Converter {
            type Input = string
            type Output = i32
            func convert(self) -> i32 {
                return 0
            }
        }

        func main() {
            let c = StrToInt { data: "hello" }
            println(c.convert())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ProtocolAssociatedTypePartialMissing) {
    auto result = check(R"--(
        protocol Converter {
            type Input
            type Output
            func convert(self) -> i32
        }

        struct StrToInt {
            data: string
        }

        impl StrToInt: Converter {
            type Input = string
            func convert(self) -> i32 {
                return 0
            }
        }

        func main() {
            let c = StrToInt { data: "hello" }
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_missing_associated_type));
}

TEST_F(SemaTest, ProtocolNoAssociatedTypesStillWorks) {
    auto result = check(R"--(
        protocol Greetable {
            func greet(self) -> string
        }

        struct Person {
            name: string
        }

        impl Person: Greetable {
            func greet(self) -> string {
                return "hello"
            }
        }

        func main() {
            let p = Person { name: "Alice" }
            println(p.greet())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Async/Await Tests ===

TEST_F(SemaTest, AsyncFuncValid) {
    auto result = check(R"--(
        async func fetchData() -> i32 {
            return 42
        }
        func main() {
            println(fetchData())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncFuncWithAwait) {
    auto result = check(R"--(
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
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AwaitOutsideAsync) {
    auto result = check(R"--(
        async func fetchData() -> i32 {
            return 42
        }
        func main() {
            let x: i32 = await fetchData()
            println(x)
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_await_outside_async));
}

TEST_F(SemaTest, AsyncMainValid) {
    auto result = check(R"--(
        async func main() {
            println(42)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncMainWithReturn) {
    auto result = check(R"--(
        async func main() -> i32 {
            return 0
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncAwaitVoidFunc) {
    auto result = check(R"--(
        async func doWork() {
            println(1)
        }
        async func process() {
            await doWork()
        }
        func main() {
            println(0)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncMultipleAwait) {
    auto result = check(R"--(
        async func fetchA() -> i32 {
            return 1
        }
        async func fetchB() -> i32 {
            return 2
        }
        async func process() -> i32 {
            let a: i32 = await fetchA()
            let b: i32 = await fetchB()
            return a + b
        }
        func main() {
            println(process())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncNestedAwait) {
    auto result = check(R"--(
        async func inner() -> i32 {
            return 42
        }
        async func outer() -> i32 {
            return await inner()
        }
        async func top() -> i32 {
            return await outer()
        }
        func main() {
            println(top())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncAwaitInIf) {
    auto result = check(R"--(
        async func getValue() -> i32 {
            return 10
        }
        async func process(flag: bool) -> i32 {
            if flag {
                let v: i32 = await getValue()
                return v
            }
            return 0
        }
        func main() {
            println(process(true))
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncAwaitInWhile) {
    auto result = check(R"--(
        async func step() -> i32 {
            return 1
        }
        async func loop_func() -> i32 {
            var sum: i32 = 0
            var i: i32 = 0
            while i < 3 {
                sum = sum + await step()
                i = i + 1
            }
            return sum
        }
        func main() {
            println(loop_func())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncAwaitInForLoop) {
    auto result = check(R"--(
        async func compute(x: i32) -> i32 {
            return x * 2
        }
        async func sum_loop() -> i32 {
            var total: i32 = 0
            for i in 0..3 {
                total = total + await compute(i)
            }
            return total
        }
        func main() {
            println(sum_loop())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncFuncReturnsTask) {
    auto result = check(R"--(
        async func fetchData() -> i32 {
            return 42
        }
        func main() {
            let task = fetchData()
            println(task)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncAwaitNonAsyncCall) {
    auto result = check(R"--(
        func syncFunc() -> i32 {
            return 42
        }
        async func process() -> i32 {
            let v: i32 = syncFunc()
            return v
        }
        func main() {
            println(process())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncVoidFunc) {
    auto result = check(R"--(
        async func doSomething() {
            println(1)
        }
        func main() {
            println(0)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncWithMultipleParams) {
    auto result = check(R"--(
        async func combine(a: i32, b: i32, c: i32) -> i32 {
            return a + b + c
        }
        async func run() -> i32 {
            return await combine(1, 2, 3)
        }
        func main() {
            println(run())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncCallChain) {
    auto result = check(R"--(
        async func f1() -> i32 { return 1 }
        async func f2() -> i32 {
            let v: i32 = await f1()
            return v + 1
        }
        async func f3() -> i32 {
            let v: i32 = await f2()
            return v + 1
        }
        async func f4() -> i32 {
            let v: i32 = await f3()
            return v + 1
        }
        func main() {
            println(f4())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncAwaitArithmetic) {
    auto result = check(R"--(
        async func getA() -> i32 { return 10 }
        async func getB() -> i32 { return 20 }
        async func compute() -> i32 {
            return await getA() + await getB()
        }
        func main() {
            println(compute())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncFuncWithParams) {
    auto result = check(R"--(
        async func add(a: i32, b: i32) -> i32 {
            return a + b
        }
        func main() {
            println(add(1, 2))
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, AsyncFuncChained) {
    auto result = check(R"--(
        async func step1() -> i32 {
            return 10
        }
        async func step2() -> i32 {
            let a: i32 = await step1()
            return a + 5
        }
        async func step3() -> i32 {
            let b: i32 = await step2()
            return b + 3
        }
        func main() {
            println(step3())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, PubAsyncFunc) {
    auto result = check(R"--(
        pub async func fetchData() -> i32 {
            return 42
        }
        func main() {
            println(fetchData())
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === M35: Const Declaration Tests ===

TEST_F(SemaTest, ConstDeclValid) {
    auto result = check(R"--(
        const x: i32 = 42
        func main() {
            println(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ConstDeclWithBinaryExpr) {
    auto result = check(R"--(
        const x = 1 + 2 * 3
        func main() {
            println(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ConstDeclWithUnaryExpr) {
    auto result = check(R"--(
        const x = -42
        func main() {
            println(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ConstDeclWithConstRef) {
    auto result = check(R"--(
        const a = 1
        const b = a + 10
        func main() {
            println(b)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ConstDeclNonConstInit) {
    auto result = check(R"--(
        func main() {
            let x = 42
            const y = x
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_const_init_not_constant));
}

TEST_F(SemaTest, ConstDeclRequiresInit) {
    auto result = check("const x: i32\nfunc main() {}");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_const_requires_init));
}

TEST_F(SemaTest, ConstDeclWithTernary) {
    auto result = check(R"--(
        const x = true ? 1 : 2
        func main() {
            println(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ConstDeclWithCast) {
    auto result = check(R"--(
        const x = 42 as i64
        func main() {
            println(x)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ConstLocalInFunction) {
    auto result = check(R"(
        func main() {
            const x = 42
            println(x)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ConstLocalAssignmentError) {
    auto result = check(R"(
        func main() {
            const x = 1
            x = 2
        }
    )");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_assign_to_immutable));
}

TEST_F(SemaTest, ConstStringLiteral) {
    auto result = check(R"--(
        const greeting = "hello"
        func main() {
            println(greeting)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ConstInNestedScope) {
    auto result = check(R"(
        func main() {
            if true {
                const x = 10
                println(x)
            }
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ConstBoolExpr) {
    auto result = check(R"(
        const flag = true && false
        func main() {
            println(flag)
        }
    )");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MultiBoundValid) {
    auto result = check(R"--(
        protocol P1 {
            func p1(self)
        }
        protocol P2 {
            func p2(self)
        }
        struct S {
            let x: i32
        }
        impl S : P1 {
            func p1(self) {}
        }
        impl S : P2 {
            func p2(self) {}
        }
        func show<T: P1 + P2>(x: T) {}
        func main() {
            let s = S { x: 1 }
            show(s)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, MultiBoundViolation) {
    auto result = check(R"--(
        protocol P1 {
            func p1(self)
        }
        protocol P2 {
            func p2(self)
        }
        struct S {
            let x: i32
        }
        impl S : P1 {
            func p1(self) {}
        }
        func show<T: P1 + P2>(x: T) {}
        func main() {
            let s = S { x: 1 }
            show(s)
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_no_conformance));
}

TEST_F(SemaTest, MultiBoundUndefinedProto) {
    auto result = check(R"--(
        protocol Real {
            func r(self)
        }
        func show<T: Real + Fake>(x: T) {}
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_undefined_protocol));
}

// === Stdlib: Random ===

TEST_F(SemaTest, RandIntType) {
    auto result = check(R"--(
        func main() {
            let x: i32 = randInt(1, 100)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, RandFloatType) {
    auto result = check(R"--(
        func main() {
            let x: f64 = randFloat()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Stdlib: Process/Env ===

TEST_F(SemaTest, EnvType) {
    auto result = check(R"--(
        func main() {
            let path: string? = env("PATH")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ExitType) {
    auto result = check(R"--(
        func main() {
            exit(0)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ArgsType) {
    auto result = check(R"--(
        func main() {
            let a: [string] = args()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Stdlib: Date/Time ===

TEST_F(SemaTest, ClockType) {
    auto result = check(R"--(
        func main() {
            let t: f64 = clock()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, ClockMsType) {
    auto result = check(R"--(
        func main() {
            let t: i64 = clockMs()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, SleepType) {
    auto result = check(R"--(
        func main() {
            sleep(100)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Stdlib: Regex ===

TEST_F(SemaTest, RegexMatchType) {
    auto result = check(R"--(
        func main() {
            let m: bool = regexMatch("hello", "hel.*")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, RegexFindType) {
    auto result = check(R"--(
        func main() {
            let m: string? = regexFind("abc123", "[0-9]+")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, RegexFindAllType) {
    auto result = check(R"--(
        func main() {
            let m: [string] = regexFindAll("a1b2", "[0-9]")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, RegexReplaceType) {
    auto result = check(R"--(
        func main() {
            let s: string = regexReplace("hi", "h", "H")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Stdlib: Networking ===

TEST_F(SemaTest, HttpGetType) {
    auto result = check(R"--(
        func main() {
            let r: string? = httpGet("https://example.com")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, HttpPostType) {
    auto result = check(R"--(
        func main() {
            let r: string? = httpPost("https://example.com", "data")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Drop Protocol ===

TEST_F(SemaTest, DropProtocolDeclaration) {
    auto result = check(R"--(
        protocol Drop {
            func drop(mut self)
        }
        func main() {}
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DropProtocolConformanceValid) {
    auto result = check(R"--(
        protocol Drop {
            func drop(mut self)
        }
        struct FileHandle { var fd: i32 }
        impl FileHandle: Drop {
            func drop(mut self) {
                let x: i32 = self.fd
            }
        }
        func main() {
            var fh = FileHandle { fd: 42 }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DropProtocolConformanceMissingMethod) {
    auto result = check(R"--(
        protocol Drop {
            func drop(mut self)
        }
        struct Res { var id: i32 }
        impl Res: Drop {}
        func main() {}
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_missing_protocol_method));
}

TEST_F(SemaTest, DropProtocolWithScopeUsage) {
    auto result = check(R"--(
        protocol Drop {
            func drop(mut self)
        }
        struct Conn { var active: bool }
        impl Conn: Drop {
            func drop(mut self) {
                let a: bool = self.active
            }
        }
        func main() {
            var c = Conn { active: true }
            let val: bool = c.active
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DropProtocolMultipleTypes) {
    auto result = check(R"--(
        protocol Drop {
            func drop(mut self)
        }
        struct A { var x: i32 }
        struct B { var y: i32 }
        impl A: Drop {
            func drop(mut self) {
                let v: i32 = self.x
            }
        }
        impl B: Drop {
            func drop(mut self) {
                let v: i32 = self.y
            }
        }
        func main() {
            var a = A { x: 1 }
            var b = B { y: 2 }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DropProtocolWithOtherMethods) {
    auto result = check(R"--(
        protocol Drop {
            func drop(mut self)
        }
        struct Svc { var id: i32 }
        impl Svc {
            func getId(self) -> i32 {
                return self.id
            }
        }
        impl Svc: Drop {
            func drop(mut self) {
                let v: i32 = self.id
            }
        }
        func main() {
            var s = Svc { id: 10 }
            let i: i32 = s.getId()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, DropProtocolUndefined) {
    auto result = check(R"--(
        struct Foo { var x: i32 }
        impl Foo: Drop {
            func drop(mut self) {
                let v: i32 = self.x
            }
        }
        func main() {}
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_undefined_protocol));
}

TEST_F(SemaTest, DropProtocolWithMoveSemantics) {
    auto result = check(R"--(
        protocol Drop {
            func drop(mut self)
        }
        struct Handle { var fd: i32 }
        impl Handle: Drop {
            func drop(mut self) {
                let v: i32 = self.fd
            }
        }
        func consume(h: Handle) {
            let v: i32 = h.fd
        }
        func main() {
            var h = Handle { fd: 5 }
            consume(h)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === F3: Guard Clause (where) Tests ===

TEST_F(SemaTest, GuardClauseBasic) {
    auto result = check(R"--(
        enum Color {
            case Red
            case Green
            case Blue
        }
        func main() {
            let c = Color.Red
            match c {
                Color.Red where true => println(0)
                Color.Red => println(1)
                Color.Green => println(2)
                Color.Blue => println(3)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GuardClauseWithBinding) {
    auto result = check(R"--(
        enum Shape {
            case Circle(f64)
            case Rect(f64, f64)
        }
        func main() {
            let s = Shape.Circle(5.0)
            match s {
                Shape.Circle(r) where r > 0.0 => println(1)
                Shape.Circle(r) => println(0)
                Shape.Rect(w, h) => println(2)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GuardClauseMultipleArms) {
    auto result = check(R"--(
        enum Shape {
            case Circle(f64)
            case Rect(f64, f64)
        }
        func main() {
            let s = Shape.Circle(5.0)
            match s {
                Shape.Circle(r) where r > 10.0 => println(2)
                Shape.Circle(r) where r > 0.0 => println(1)
                Shape.Circle(r) => println(0)
                Shape.Rect(w, h) => println(3)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GuardClauseWithWildcard) {
    auto result = check(R"--(
        enum Color {
            case Red
            case Green
            case Blue
        }
        func main() {
            let c = Color.Red
            match c {
                Color.Red where false => println(0)
                _ => println(1)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GuardClauseNonExhaustive) {
    auto result = check(R"--(
        enum Color {
            case Red
            case Green
            case Blue
        }
        func main() {
            let c = Color.Red
            match c {
                Color.Red where true => println(0)
                Color.Green => println(1)
                Color.Blue => println(2)
            }
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_nonexhaustive_match));
}

TEST_F(SemaTest, GuardClauseExhaustiveWithUnguarded) {
    auto result = check(R"--(
        enum Color {
            case Red
            case Green
            case Blue
        }
        func main() {
            let c = Color.Red
            match c {
                Color.Red where true => println(0)
                Color.Red => println(1)
                Color.Green => println(2)
                Color.Blue => println(3)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GuardClauseIntegerMatch) {
    auto result = check(R"--(
        func main() {
            let x: i32 = 5
            match x {
                1 where true => println(1)
                2 => println(2)
                _ => println(0)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, GuardClauseComplexExpr) {
    auto result = check(R"--(
        enum Shape {
            case Circle(f64)
            case Rect(f64, f64)
        }
        func main() {
            let s = Shape.Circle(5.0)
            match s {
                Shape.Circle(r) where r > 0.0 && r < 100.0 => println(1)
                Shape.Circle(r) => println(0)
                Shape.Rect(w, h) => println(2)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Operator Overloading ===

TEST_F(SemaTest, OperatorOverloadAddBasic) {
    auto result = check(R"--(
        protocol Add {
            func add(self, other: Self) -> Self
        }
        struct Vec2 {
            var x: f64
            var y: f64
        }
        impl Vec2: Add {
            func add(self, other: Vec2) -> Vec2 {
                return Vec2 { x: self.x + other.x, y: self.y + other.y }
            }
        }
        func main() {
            let a = Vec2 { x: 1.0, y: 2.0 }
            let b = Vec2 { x: 3.0, y: 4.0 }
            let c = a + b
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OperatorOverloadEq) {
    auto result = check(R"--(
        protocol Eq {
            func eq(self, other: Self) -> bool
        }
        struct Point {
            var x: i32
            var y: i32
        }
        impl Point: Eq {
            func eq(self, other: Point) -> bool {
                return self.x == other.x && self.y == other.y
            }
        }
        func main() {
            let a = Point { x: 1, y: 2 }
            let b = Point { x: 1, y: 2 }
            let r = a == b
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OperatorOverloadNotEq) {
    auto result = check(R"--(
        protocol Eq {
            func eq(self, other: Self) -> bool
        }
        struct Point {
            var x: i32
            var y: i32
        }
        impl Point: Eq {
            func eq(self, other: Point) -> bool {
                return self.x == other.x && self.y == other.y
            }
        }
        func main() {
            let a = Point { x: 1, y: 2 }
            let b = Point { x: 3, y: 4 }
            let r = a != b
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OperatorOverloadLessAndDerived) {
    auto result = check(R"--(
        protocol Less {
            func less(self, other: Self) -> bool
        }
        protocol Eq {
            func eq(self, other: Self) -> bool
        }
        struct Val {
            var n: i32
        }
        impl Val: Less {
            func less(self, other: Val) -> bool {
                return self.n < other.n
            }
        }
        impl Val: Eq {
            func eq(self, other: Val) -> bool {
                return self.n == other.n
            }
        }
        func main() {
            let a = Val { n: 1 }
            let b = Val { n: 2 }
            let r1 = a < b
            let r2 = a <= b
            let r3 = a > b
            let r4 = a >= b
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OperatorOverloadMultipleProtocols) {
    auto result = check(R"--(
        protocol Add {
            func add(self, other: Self) -> Self
        }
        protocol Sub {
            func sub(self, other: Self) -> Self
        }
        protocol Mul {
            func mul(self, other: Self) -> Self
        }
        struct Vec2 {
            var x: f64
            var y: f64
        }
        impl Vec2: Add {
            func add(self, other: Vec2) -> Vec2 {
                return Vec2 { x: self.x + other.x, y: self.y + other.y }
            }
        }
        impl Vec2: Sub {
            func sub(self, other: Vec2) -> Vec2 {
                return Vec2 { x: self.x - other.x, y: self.y - other.y }
            }
        }
        impl Vec2: Mul {
            func mul(self, other: Vec2) -> Vec2 {
                return Vec2 { x: self.x * other.x, y: self.y * other.y }
            }
        }
        func main() {
            let a = Vec2 { x: 1.0, y: 2.0 }
            let b = Vec2 { x: 3.0, y: 4.0 }
            let c = a + b
            let d = a - b
            let e = a * b
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OperatorOverloadMissingConformance) {
    auto result = check(R"--(
        struct Vec2 {
            var x: f64
            var y: f64
        }
        func main() {
            let a = Vec2 { x: 1.0, y: 2.0 }
            let b = Vec2 { x: 3.0, y: 4.0 }
            let c = a + b
        }
    )--");
    EXPECT_FALSE(result.passed);
}

TEST_F(SemaTest, OperatorOverloadPrimitivesUnaffected) {
    auto result = check(R"--(
        func main() {
            let a: i32 = 10
            let b: i32 = 20
            let c = a + b
            let d: f64 = 1.5
            let e: f64 = 2.5
            let f = d + e
            let g = a == b
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OperatorOverloadResultTypeIsStruct) {
    auto result = check(R"--(
        protocol Add {
            func add(self, other: Self) -> Self
        }
        struct Vec2 {
            var x: f64
            var y: f64
        }
        impl Vec2: Add {
            func add(self, other: Vec2) -> Vec2 {
                return Vec2 { x: self.x + other.x, y: self.y + other.y }
            }
        }
        func main() {
            let a = Vec2 { x: 1.0, y: 2.0 }
            let b = Vec2 { x: 3.0, y: 4.0 }
            let c = a + b
            let r = c.x
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, OperatorOverloadDivMod) {
    auto result = check(R"--(
        protocol Div {
            func div(self, other: Self) -> Self
        }
        protocol Mod {
            func mod(self, other: Self) -> Self
        }
        struct Num {
            var v: i32
        }
        impl Num: Div {
            func div(self, other: Num) -> Num {
                return Num { v: self.v / other.v }
            }
        }
        impl Num: Mod {
            func mod(self, other: Num) -> Num {
                return Num { v: self.v % other.v }
            }
        }
        func main() {
            let a = Num { v: 10 }
            let b = Num { v: 3 }
            let c = a / b
            let d = a % b
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === Custom Iterator (Iter Protocol) Tests ===

TEST_F(SemaTest, CustomIterBasicForIn) {
    auto result = check(R"--(
        protocol Iter {
            func next(mut self) -> i32?
        }
        struct Counter {
            var current: i32
            var max: i32
        }
        impl Counter: Iter {
            func next(mut self) -> i32? {
                if self.current < self.max {
                    let val = self.current
                    self.current = self.current + 1
                    return val
                }
                return nil
            }
        }
        func main() {
            var c = Counter { current: 0, max: 5 }
            for item in c {
                let x: i32 = item
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, CustomIterStringElement) {
    auto result = check(R"--(
        protocol Iter {
            func next(mut self) -> string?
        }
        struct Words {
            var index: i32
        }
        impl Words: Iter {
            func next(mut self) -> string? {
                if self.index < 3 {
                    self.index = self.index + 1
                    return "word"
                }
                return nil
            }
        }
        func main() {
            var w = Words { index: 0 }
            for item in w {
                let s: string = item
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, CustomIterF64Element) {
    auto result = check(R"--(
        protocol Iter {
            func next(mut self) -> f64?
        }
        struct Floats {
            var index: i32
        }
        impl Floats: Iter {
            func next(mut self) -> f64? {
                if self.index < 2 {
                    self.index = self.index + 1
                    return 3.14
                }
                return nil
            }
        }
        func main() {
            var f = Floats { index: 0 }
            for item in f {
                let v: f64 = item
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, CustomIterNoConformance) {
    auto result = check(R"--(
        struct Plain {
            var x: i32
        }
        func main() {
            var p = Plain { x: 0 }
            for item in p {
                let v = item
            }
        }
    )--");
    // No Iter conformance — no element type set, but no hard error either
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, CustomIterProtocolDecl) {
    auto result = check(R"--(
        protocol Iter {
            func next(mut self) -> i32?
        }
        struct MyIter {
            var pos: i32
        }
        impl MyIter: Iter {
            func next(mut self) -> i32? {
                return nil
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, CustomIterBodyTypeCheck) {
    auto result = check(R"--(
        protocol Iter {
            func next(mut self) -> i32?
        }
        struct Range5 {
            var i: i32
        }
        impl Range5: Iter {
            func next(mut self) -> i32? {
                if self.i < 5 {
                    let val = self.i
                    self.i = self.i + 1
                    return val
                }
                return nil
            }
        }
        func main() {
            var r = Range5 { i: 0 }
            for item in r {
                let doubled: i32 = item + item
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, CustomIterMultipleTypes) {
    auto result = check(R"--(
        protocol Iter {
            func next(mut self) -> i32?
        }
        struct Counter {
            var current: i32
            var max: i32
        }
        impl Counter: Iter {
            func next(mut self) -> i32? {
                if self.current < self.max {
                    let val = self.current
                    self.current = self.current + 1
                    return val
                }
                return nil
            }
        }
        struct Countdown {
            var n: i32
        }
        impl Countdown: Iter {
            func next(mut self) -> i32? {
                if self.n > 0 {
                    self.n = self.n - 1
                    return self.n
                }
                return nil
            }
        }
        func main() {
            var c = Counter { current: 0, max: 3 }
            for item in c {
                let x: i32 = item
            }
            var d = Countdown { n: 3 }
            for item in d {
                let y: i32 = item
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === F7: Variadic Functions ===

TEST_F(SemaTest, VariadicBasic) {
    auto result = check(R"--(
        func f(args: i32...) {
            for v in args {
                let x: i32 = v
            }
        }
        func main() {
            f(1, 2, 3)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, VariadicWithNormalParams) {
    auto result = check(R"--(
        func f(a: i32, rest: i32...) {
            let x: i32 = a
            for v in rest {
                let y: i32 = v
            }
        }
        func main() {
            f(10, 20, 30)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, VariadicNotLast) {
    auto result = check(R"--(
        func f(a: i32..., b: i32) {
        }
        func main() {}
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_variadic_not_last));
}

TEST_F(SemaTest, VariadicMultiple) {
    auto result = check(R"--(
        func f(a: i32..., b: string...) {
        }
        func main() {}
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_multiple_variadic));
}

TEST_F(SemaTest, VariadicCall) {
    auto result = check(R"--(
        func sum(values: i32...) -> i32 {
            var total: i32 = 0
            for v in values {
                total = total + v
            }
            return total
        }
        func main() {
            let s: i32 = sum(1, 2, 3)
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, VariadicEmpty) {
    auto result = check(R"--(
        func f(args: i32...) {
        }
        func main() {
            f()
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, VariadicStringType) {
    auto result = check(R"--(
        func f(args: string...) {
            for s in args {
                println(s)
            }
        }
        func main() {
            f("hello", "world")
        }
    )--");
    EXPECT_TRUE(result.passed);
}

// === F8: Nested Pattern Matching ===

TEST_F(SemaTest, NestedPatternBasic) {
    auto result = check(R"--(
        enum Inner {
            case Val(i32)
            case None
        }
        enum Outer {
            case Some(Inner)
            case Empty
        }
        func main() {
            let x = Outer.Some(Inner.Val(42))
            match x {
                Outer.Some(Inner.Val(n)) => println(n)
                Outer.Some(Inner.None) => println(0)
                Outer.Empty => println(0)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, NestedPatternMultipleBindings) {
    auto result = check(R"--(
        enum Shape {
            case Circle(f64)
            case Rect(f64, f64)
        }
        enum Wrapper {
            case Wrapped(Shape)
            case Empty
        }
        func main() {
            let w = Wrapper.Wrapped(Shape.Rect(1.0, 2.0))
            match w {
                Wrapper.Wrapped(Shape.Circle(r)) => println(r)
                Wrapper.Wrapped(Shape.Rect(w, h)) => println(w)
                Wrapper.Empty => println(0)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, NestedPatternWithWildcard) {
    auto result = check(R"--(
        enum Inner {
            case A(i32)
            case B
        }
        enum Outer {
            case X(Inner)
            case Y
        }
        func main() {
            let v = Outer.X(Inner.A(10))
            match v {
                Outer.X(Inner.A(n)) => println(n)
                _ => println(0)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, NestedPatternExhaustive) {
    auto result = check(R"--(
        enum Color {
            case Red
            case Blue
        }
        enum Box {
            case Has(Color)
            case Empty
        }
        func main() {
            let b = Box.Has(Color.Red)
            match b {
                Box.Has(c) => println(0)
                Box.Empty => println(1)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

TEST_F(SemaTest, NestedPatternUseBinding) {
    auto result = check(R"--(
        enum Inner {
            case Val(i32)
            case None
        }
        enum Outer {
            case Some(Inner)
            case Empty
        }
        func main() {
            let x = Outer.Some(Inner.Val(42))
            match x {
                Outer.Some(Inner.Val(n)) => println(n)
                _ => println(0)
            }
        }
    )--");
    EXPECT_TRUE(result.passed);
}

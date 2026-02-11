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

TEST_F(SemaTest, AsyncMainForbidden) {
    auto result = check(R"--(
        async func main() {
            println(42)
        }
    )--");
    EXPECT_FALSE(result.passed);
    EXPECT_TRUE(hasDiag(result, DiagID::err_async_main));
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

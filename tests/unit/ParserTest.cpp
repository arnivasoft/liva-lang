#include "liva/AST/ASTPrinter.h"
#include "liva/AST/Decl.h"
#include "liva/AST/Expr.h"
#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include <gtest/gtest.h>
#include <sstream>

using namespace liva;

class ParserTest : public ::testing::Test {
protected:
    struct ParseResult {
        std::unique_ptr<SourceManager> sm;
        DiagnosticsEngine diag;
        std::unique_ptr<TranslationUnit> tu;
        bool hasErrors;
    };

    ParseResult parse(const std::string &source) {
        ParseResult result;
        result.sm = std::make_unique<SourceManager>("test.liva", source);
        result.diag.setSourceManager(result.sm.get());
        Lexer lexer(*result.sm, result.diag);
        Parser parser(lexer, result.diag);
        result.tu = parser.parseTranslationUnit();
        result.hasErrors = result.diag.hasErrors();
        return result;
    }
};

TEST_F(ParserTest, EmptyFile) {
    auto result = parse("");
    ASSERT_FALSE(result.hasErrors);
    EXPECT_TRUE(result.tu->getDeclarations().empty());
}

TEST_F(ParserTest, LetDeclaration) {
    auto result = parse("let x: i32 = 42");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);

    auto *var = dynamic_cast<VarDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->getName(), "x");
    EXPECT_FALSE(var->isMutable());
    EXPECT_TRUE(var->hasInit());
}

TEST_F(ParserTest, VarDeclaration) {
    auto result = parse("var counter: i32 = 0");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);

    auto *var = dynamic_cast<VarDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->getName(), "counter");
    EXPECT_TRUE(var->isMutable());
}

TEST_F(ParserTest, SimpleFunction) {
    auto result = parse(R"(
        func add(a: i32, b: i32) -> i32 {
            return a + b
        }
    )");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);

    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->getName(), "add");
    EXPECT_EQ(func->getParams().size(), 2);
    EXPECT_TRUE(func->hasBody());
}

TEST_F(ParserTest, VoidFunction) {
    auto result = parse(R"(
        func hello() {
            println("hello")
        }
    )");
    ASSERT_FALSE(result.hasErrors);

    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->getName(), "hello");
    EXPECT_EQ(func->getParams().size(), 0);
    EXPECT_TRUE(func->getReturnType()->isVoid());
}

TEST_F(ParserTest, StructDeclaration) {
    auto result = parse(R"(
        struct Point {
            var x: f64
            var y: f64
        }
    )");
    ASSERT_FALSE(result.hasErrors);

    auto *s = dynamic_cast<StructDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->getName(), "Point");
    EXPECT_EQ(s->getFields().size(), 2);
    EXPECT_EQ(s->getFields()[0]->getName(), "x");
    EXPECT_EQ(s->getFields()[1]->getName(), "y");
}

TEST_F(ParserTest, IfElseStatement) {
    auto result = parse(R"(
        func test(x: i32) -> i32 {
            if x > 0 {
                return 1
            } else {
                return -1
            }
        }
    )");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, WhileLoop) {
    auto result = parse(R"(
        func count() {
            var i: i32 = 0
            while i < 10 {
                println(i)
                i = i + 1
            }
        }
    )");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, ForInLoop) {
    auto result = parse(R"(
        func test() {
            for i in items {
                println(i)
            }
        }
    )");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, BinaryExpressions) {
    auto result = parse(R"(
        func test() {
            let a = 1 + 2 * 3
            let b = 4 - 5 / 6
            let c = a == b
            let d = a != b && c
        }
    )");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, FunctionCall) {
    auto result = parse(R"(
        func test() {
            let x = add(1, 2)
            println(x)
        }
    )");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, EnumDeclaration) {
    auto result = parse(R"(
        enum Shape {
            case Circle(f64)
            case Rectangle(f64, f64)
        }
    )");
    ASSERT_FALSE(result.hasErrors);

    auto *e = dynamic_cast<EnumDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->getName(), "Shape");
    EXPECT_EQ(e->getCases().size(), 2);
}

TEST_F(ParserTest, ImplBlock) {
    auto result = parse(R"(
        struct Point {
            var x: f64
            var y: f64
        }

        impl Point {
            func new(x: f64, y: f64) -> Point {
                return Point { x: x, y: y }
            }
        }
    )");
    ASSERT_FALSE(result.hasErrors);
    EXPECT_EQ(result.tu->getDeclarations().size(), 2);
}

TEST_F(ParserTest, ASTPrinterOutput) {
    auto result = parse("let x: i32 = 42");
    ASSERT_FALSE(result.hasErrors);

    std::stringstream ss;
    ASTPrinter printer(ss);
    printer.print(*result.tu);

    std::string output = ss.str();
    EXPECT_NE(output.find("VarDecl"), std::string::npos);
    EXPECT_NE(output.find("'x'"), std::string::npos);
}

TEST_F(ParserTest, UnaryExpression) {
    auto result = parse(R"(
        func test() {
            let a = -42
            let b = !true
        }
    )");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, ArrayLiteral) {
    auto result = parse(R"(
        func test() {
            let arr = [1, 2, 3]
        }
    )");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, ImportDeclaration) {
    auto result = parse("import std::io");
    ASSERT_FALSE(result.hasErrors);

    auto *imp = dynamic_cast<ImportDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(imp, nullptr);
    EXPECT_EQ(imp->getPathString(), "std::io");
}

TEST_F(ParserTest, RefParameter) {
    auto result = parse(R"(
        func read(data: ref Buffer) {
            println(data.size)
        }
    )");
    ASSERT_FALSE(result.hasErrors);

    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->getParams()[0].isRef);
}

TEST_F(ParserTest, SelfParameter) {
    auto result = parse(R"(
        struct Foo {
            var x: i32
        }
        impl Foo {
            func get(ref self) -> i32 {
                return self.x
            }
        }
    )");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, StringInterpolation) {
    auto result = parse(R"--(
        func test() {
            let name = "world"
            let msg = "hello \(name)!"
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

// === Generic Function Tests ===

TEST_F(ParserTest, GenericFunctionSingleParam) {
    auto result = parse(R"(
        func identity<T>(x: T) -> T { return x }
    )");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->isGeneric());
    EXPECT_EQ(func->getTypeParams().size(), 1);
    EXPECT_EQ(func->getTypeParams()[0], "T");
}

TEST_F(ParserTest, GenericFunctionMultipleParams) {
    auto result = parse(R"(
        func first<T, U>(a: T, b: U) -> T { return a }
    )");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->isGeneric());
    EXPECT_EQ(func->getTypeParams().size(), 2);
    EXPECT_EQ(func->getTypeParams()[0], "T");
    EXPECT_EQ(func->getTypeParams()[1], "U");
}

TEST_F(ParserTest, NonGenericFunctionUnchanged) {
    auto result = parse(R"(
        func add(a: i32, b: i32) -> i32 { return a + b }
    )");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_FALSE(func->isGeneric());
    EXPECT_TRUE(func->getTypeParams().empty());
}

TEST_F(ParserTest, GenericStructSingleParam) {
    auto result = parse(R"(
        struct Box<T> { let data: T }
    )");
    ASSERT_FALSE(result.hasErrors);
    auto *s = dynamic_cast<StructDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->isGeneric());
    EXPECT_EQ(s->getTypeParams().size(), 1);
    EXPECT_EQ(s->getTypeParams()[0], "T");
}

TEST_F(ParserTest, GenericStructMultipleParams) {
    auto result = parse(R"(
        struct Pair<T, U> {
            let first: T
            let second: U
        }
    )");
    ASSERT_FALSE(result.hasErrors);
    auto *s = dynamic_cast<StructDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->isGeneric());
    EXPECT_EQ(s->getTypeParams().size(), 2);
    EXPECT_EQ(s->getTypeParams()[0], "T");
    EXPECT_EQ(s->getTypeParams()[1], "U");
}

TEST_F(ParserTest, NonGenericStructUnchanged) {
    auto result = parse(R"(
        struct Point { let x: i32  let y: i32 }
    )");
    ASSERT_FALSE(result.hasErrors);
    auto *s = dynamic_cast<StructDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(s, nullptr);
    EXPECT_FALSE(s->isGeneric());
    EXPECT_TRUE(s->getTypeParams().empty());
}

TEST_F(ParserTest, GenericImplBlock) {
    auto result = parse(R"--(
        struct Box<T> { let data: T }
        impl Box<T> {
            func get(self) -> T { return self.data }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_GE(result.tu->getDeclarations().size(), 2);
    auto *impl = dynamic_cast<ImplDecl *>(result.tu->getDeclarations()[1].get());
    ASSERT_NE(impl, nullptr);
    EXPECT_TRUE(impl->isGeneric());
    ASSERT_EQ(impl->getTypeParams().size(), 1);
    EXPECT_EQ(impl->getTypeParams()[0], "T");
    EXPECT_EQ(impl->getTypeName(), "Box");
    ASSERT_EQ(impl->getMethods().size(), 1);
    EXPECT_EQ(impl->getMethods()[0]->getName(), "get");
}

TEST_F(ParserTest, OptionalTypeAnnotation) {
    auto result = parse("let x: i32? = nil");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *var = dynamic_cast<VarDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->getName(), "x");
    ASSERT_NE(var->getType(), nullptr);
    EXPECT_EQ(var->getType()->getKind(), TypeRepr::Kind::Optional);
    EXPECT_TRUE(var->hasInit());
    EXPECT_EQ(var->getInit()->getKind(), ASTNode::NodeKind::NilLiteralExpr);
}

TEST_F(ParserTest, ForceUnwrap) {
    auto result = parse(R"--(
        func main() {
            let x: i32? = 42
            let y = x!
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, ClosureExprBasic) {
    auto result = parse(R"--(
        func test() {
            let f = |x: i32| -> i32 { return x }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, ClosureExprNoParams) {
    auto result = parse(R"--(
        func test() {
            let f = || { let x = 1 }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, ProtocolConformance) {
    auto result = parse(R"--(
        protocol P {
            func foo(self) -> i32
        }
        struct S { let x: i32 }
        impl S: P {
            func foo(self) -> i32 { return self.x }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_GE(result.tu->getDeclarations().size(), 3);
}

TEST_F(ParserTest, RefProtocolTypeAnnotation) {
    auto result = parse(R"--(
        protocol Shape {
            func area(self) -> f64
        }
        struct Circle { let r: f64 }
        impl Circle: Shape {
            func area(self) -> f64 { return self.r }
        }
        func main() {
            let c = Circle { r: 3.0 }
            let s: ref Shape = c
            println(0)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, IfLetBasic) {
    auto result = parse(R"--(
        func main() {
            let x: i32? = 42
            if let val = x {
                println(val)
            }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, IfLetWithElse) {
    auto result = parse(R"--(
        func main() {
            let x: i32? = nil
            if let val = x {
                println(val)
            } else {
                println(0)
            }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, NilCoalesceOperator) {
    auto result = parse(R"--(
        func main() {
            let x: i32? = nil
            let y = x ?? 0
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, ResultTypeAnnotation) {
    auto result = parse(R"--(
        func main() {
            let r: Result<i32, string> = Result.ok(42)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto &decls = result.tu->getDeclarations();
    ASSERT_EQ(decls.size(), 1);
    auto *func = dynamic_cast<FuncDecl *>(decls[0].get());
    ASSERT_NE(func, nullptr);
    auto &stmts = func->getBody()->getStatements();
    ASSERT_GE(stmts.size(), 1u);
    auto *varDecl = dynamic_cast<VarDecl *>(stmts[0].get());
    ASSERT_NE(varDecl, nullptr);
    ASSERT_TRUE(varDecl->hasTypeAnnotation());
    EXPECT_EQ(varDecl->getType()->getKind(), TypeRepr::Kind::Result);
}

TEST_F(ParserTest, TryExpression) {
    auto result = parse(R"--(
        func foo() -> Result<i32, string> {
            return Result.ok(1)
        }
        func main() {
            let x = try foo()
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, ResultMatchExpression) {
    auto result = parse(R"--(
        func main() {
            let r: Result<i32, string> = Result.ok(42)
            let x = match r {
                Result.Ok(v) => v
                Result.Err(e) => 0
            }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

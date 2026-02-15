#include "liva/AST/ASTPrinter.h"
#include "liva/AST/Decl.h"
#include "liva/AST/Expr.h"
#include "liva/AST/Stmt.h"
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

TEST_F(ParserTest, ForInTuplePattern) {
    auto result = parse(R"(
        func test() {
            for (k, v) in m {
                println(k)
            }
        }
    )");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    auto &stmts = func->getBody()->getStatements();
    ASSERT_GE(stmts.size(), 1u);
    auto *forStmt = dynamic_cast<ForStmt *>(stmts[0].get());
    ASSERT_NE(forStmt, nullptr);
    EXPECT_TRUE(forStmt->hasTuplePattern());
    EXPECT_EQ(forStmt->getVarName(), "k");
    EXPECT_EQ(forStmt->getVarName2(), "v");
}

TEST_F(ParserTest, ForInSingleVar) {
    auto result = parse(R"(
        func test() {
            for x in arr {
                println(x)
            }
        }
    )");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    auto &stmts = func->getBody()->getStatements();
    ASSERT_GE(stmts.size(), 1u);
    auto *forStmt = dynamic_cast<ForStmt *>(stmts[0].get());
    ASSERT_NE(forStmt, nullptr);
    EXPECT_FALSE(forStmt->hasTuplePattern());
    EXPECT_EQ(forStmt->getVarName(), "x");
}

TEST_F(ParserTest, WhileLetParsing) {
    auto result = parse(R"(
        func test() {
            while let x = opt {
                println(x)
            }
        }
    )");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    auto &stmts = func->getBody()->getStatements();
    ASSERT_GE(stmts.size(), 1u);
    auto *whileLet = dynamic_cast<WhileLetStmt *>(stmts[0].get());
    ASSERT_NE(whileLet, nullptr);
    EXPECT_EQ(whileLet->getBindingName(), "x");
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

// === M16b: Closure Parameter Type Inference ===

TEST_F(ParserTest, ClosureUntypedParams) {
    auto result = parse(R"--(
        func main() {
            let f: (i32) -> i32 = |x| -> i32 { return x }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->getName(), "main");
}

TEST_F(ParserTest, ClosureMixedParams) {
    auto result = parse(R"--(
        func main() {
            let f = |x, y: i32| -> i32 { return y }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

// === M16c: Trailing Closure Syntax ===

TEST_F(ParserTest, TrailingClosureBasic) {
    auto result = parse(R"--(
        func apply(x: i32, f: (i32) -> i32) -> i32 {
            return f(x)
        }
        func main() {
            let r = apply(5) |x: i32| -> i32 { return x + 1 }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, ProtocolDefaultMethod) {
    auto result = parse(R"--(
        protocol Greetable {
            func greet(self) -> string
            func shout(self) -> string {
                return self.greet()
            }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *proto = dynamic_cast<ProtocolDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(proto, nullptr);
    EXPECT_EQ(proto->getMethods().size(), 2);
    EXPECT_FALSE(proto->getMethods()[0]->hasBody());
    EXPECT_TRUE(proto->getMethods()[1]->hasBody());
}

TEST_F(ParserTest, GenericFuncWithBound) {
    auto result = parse("func show<T: Printable>(item: T) -> string { return item.toString() }");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->isGeneric());
    ASSERT_EQ(func->getTypeParams().size(), 1);
    EXPECT_EQ(func->getTypeParams()[0], "T");
    ASSERT_EQ(func->getTypeParamBounds().size(), 1);
    ASSERT_EQ(func->getTypeParamBounds().at("T").size(), 1);
    EXPECT_EQ(func->getTypeParamBounds().at("T")[0], "Printable");
}

TEST_F(ParserTest, GenericFuncMultiParamsMixedBounds) {
    auto result = parse("func combine<T: Printable, U>(a: T, b: U) -> string { return a.toString() }");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->getTypeParams().size(), 2);
    EXPECT_EQ(func->getTypeParams()[0], "T");
    EXPECT_EQ(func->getTypeParams()[1], "U");
    ASSERT_EQ(func->getTypeParamBounds().size(), 1);
    ASSERT_EQ(func->getTypeParamBounds().at("T").size(), 1);
    EXPECT_EQ(func->getTypeParamBounds().at("T")[0], "Printable");
    EXPECT_EQ(func->getTypeParamBounds().count("U"), 0);
}

TEST_F(ParserTest, GenericStructWithBound) {
    auto result = parse(R"--(
        struct Wrapper<T: Printable> {
            value: T
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *s = dynamic_cast<StructDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->isGeneric());
    ASSERT_EQ(s->getTypeParamBounds().size(), 1);
    ASSERT_EQ(s->getTypeParamBounds().at("T").size(), 1);
    EXPECT_EQ(s->getTypeParamBounds().at("T")[0], "Printable");
}

TEST_F(ParserTest, GenericImplWithBound) {
    auto result = parse(R"--(
        impl Wrapper<T: Printable> {
            func show(self) -> string { return self.value.toString() }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *impl = dynamic_cast<ImplDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(impl, nullptr);
    EXPECT_TRUE(impl->isGeneric());
    ASSERT_EQ(impl->getTypeParamBounds().size(), 1);
    ASSERT_EQ(impl->getTypeParamBounds().at("T").size(), 1);
    EXPECT_EQ(impl->getTypeParamBounds().at("T")[0], "Printable");
}

TEST_F(ParserTest, GenericFuncNoBoundsStillWorks) {
    auto result = parse("func identity<T>(x: T) -> T { return x }");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->isGeneric());
    EXPECT_TRUE(func->getTypeParamBounds().empty());
}

TEST_F(ParserTest, WhereClauseFunc) {
    auto result = parse("func show<T>(item: T) -> string where T: Printable { return item.toString() }");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->isGeneric());
    ASSERT_EQ(func->getTypeParamBounds().size(), 1);
    ASSERT_EQ(func->getTypeParamBounds().at("T").size(), 1);
    EXPECT_EQ(func->getTypeParamBounds().at("T")[0], "Printable");
}

TEST_F(ParserTest, WhereClauseMultipleBounds) {
    auto result = parse("func combine<T, U>(a: T, b: U) where T: Printable, U: Hashable { return a }");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->getTypeParamBounds().size(), 2);
    ASSERT_EQ(func->getTypeParamBounds().at("T").size(), 1);
    EXPECT_EQ(func->getTypeParamBounds().at("T")[0], "Printable");
    ASSERT_EQ(func->getTypeParamBounds().at("U").size(), 1);
    EXPECT_EQ(func->getTypeParamBounds().at("U")[0], "Hashable");
}

TEST_F(ParserTest, WhereClauseStruct) {
    auto result = parse(R"--(
        struct Box<T> where T: Printable {
            value: T
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *s = dynamic_cast<StructDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(s->isGeneric());
    ASSERT_EQ(s->getTypeParamBounds().size(), 1);
    ASSERT_EQ(s->getTypeParamBounds().at("T").size(), 1);
    EXPECT_EQ(s->getTypeParamBounds().at("T")[0], "Printable");
}

TEST_F(ParserTest, WhereClauseImpl) {
    auto result = parse(R"--(
        impl Box<T> where T: Printable {
            func show(self) -> string { return self.value }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *impl = dynamic_cast<ImplDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(impl, nullptr);
    ASSERT_EQ(impl->getTypeParamBounds().size(), 1);
    ASSERT_EQ(impl->getTypeParamBounds().at("T").size(), 1);
    EXPECT_EQ(impl->getTypeParamBounds().at("T")[0], "Printable");
}

TEST_F(ParserTest, WhereClauseCombinedWithInline) {
    auto result = parse("func show<T: Printable>(item: T) where T: Serializable { return item }");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    // Both inline and where clause bounds are collected
    ASSERT_EQ(func->getTypeParamBounds().size(), 1);
    ASSERT_EQ(func->getTypeParamBounds().at("T").size(), 2);
    EXPECT_EQ(func->getTypeParamBounds().at("T")[0], "Printable");
    EXPECT_EQ(func->getTypeParamBounds().at("T")[1], "Serializable");
}

TEST_F(ParserTest, OptionalChainingField) {
    auto result = parse(R"--(
        func main() {
            p?.x
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    auto &stmts = func->getBody()->getStatements();
    ASSERT_GE(stmts.size(), 1u);
    auto *exprStmt = dynamic_cast<ExprStmt *>(stmts[0].get());
    ASSERT_NE(exprStmt, nullptr);
    auto *member = dynamic_cast<MemberExpr *>(exprStmt->getExpr());
    ASSERT_NE(member, nullptr);
    EXPECT_TRUE(member->isOptionalChain());
    EXPECT_EQ(member->getMember(), "x");
}

TEST_F(ParserTest, OptionalChainingMethod) {
    auto result = parse(R"--(
        func main() {
            p?.getX()
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    auto &stmts = func->getBody()->getStatements();
    ASSERT_GE(stmts.size(), 1u);
    auto *exprStmt = dynamic_cast<ExprStmt *>(stmts[0].get());
    ASSERT_NE(exprStmt, nullptr);
    auto *call = dynamic_cast<CallExpr *>(exprStmt->getExpr());
    ASSERT_NE(call, nullptr);
    auto *member = dynamic_cast<MemberExpr *>(call->getCallee());
    ASSERT_NE(member, nullptr);
    EXPECT_TRUE(member->isOptionalChain());
    EXPECT_EQ(member->getMember(), "getX");
}

TEST_F(ParserTest, RegularMemberUnchanged) {
    auto result = parse(R"--(
        func main() {
            p.x
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    auto &stmts = func->getBody()->getStatements();
    ASSERT_GE(stmts.size(), 1u);
    auto *exprStmt = dynamic_cast<ExprStmt *>(stmts[0].get());
    ASSERT_NE(exprStmt, nullptr);
    auto *member = dynamic_cast<MemberExpr *>(exprStmt->getExpr());
    ASSERT_NE(member, nullptr);
    EXPECT_FALSE(member->isOptionalChain());
    EXPECT_EQ(member->getMember(), "x");
}

// ===== M29: Default Function Arguments =====

TEST_F(ParserTest, DefaultArgParsing) {
    auto result = parse(R"--(
        func greet(name: string = "World") {
            println(name)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->getName(), "greet");
    ASSERT_EQ(func->getParams().size(), 1u);
    EXPECT_TRUE(func->getParams()[0].hasDefault());
}

TEST_F(ParserTest, TernaryExpression) {
    auto result = parse(R"--(
        func test() {
            let x = true ? 1 : 0
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    auto &stmts = func->getBody()->getStatements();
    ASSERT_GE(stmts.size(), 1u);
    auto *varDecl = dynamic_cast<VarDecl *>(stmts[0].get());
    ASSERT_NE(varDecl, nullptr);
    auto *ternary = dynamic_cast<TernaryExpr *>(const_cast<Expr *>(varDecl->getInit()));
    ASSERT_NE(ternary, nullptr);
}

TEST_F(ParserTest, TypeAlias) {
    auto result = parse(R"--(
        type Int = i32
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *alias = dynamic_cast<TypeAliasDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(alias->getName(), "Int");
    EXPECT_EQ(alias->getTargetType()->getKind(), TypeRepr::Kind::I32);
}

TEST_F(ParserTest, TypeAliasNamed) {
    auto result = parse(R"--(
        struct Point {
            x: i32
            y: i32
        }
        type Pos = Point
    )--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_GE(result.tu->getDeclarations().size(), 2u);
    auto *alias = dynamic_cast<TypeAliasDecl *>(result.tu->getDeclarations()[1].get());
    ASSERT_NE(alias, nullptr);
    EXPECT_EQ(alias->getName(), "Pos");
    auto *target = dynamic_cast<const NamedTypeRepr *>(alias->getTargetType());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->getName(), "Point");
}

// --- Tuple Tests ---

TEST_F(ParserTest, TupleType) {
    auto result = parse(R"--(
        func f() -> (i32, string) {
            return (1, "hello")
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(fn, nullptr);
    auto *retType = dynamic_cast<const TupleTypeRepr *>(fn->getReturnType());
    ASSERT_NE(retType, nullptr);
    EXPECT_EQ(retType->getArity(), 2u);
    EXPECT_EQ(retType->getElements()[0]->getKind(), TypeRepr::Kind::I32);
    EXPECT_EQ(retType->getElements()[1]->getKind(), TypeRepr::Kind::String);
}

TEST_F(ParserTest, TupleLiteral) {
    auto result = parse(R"--(
        let x = (1, "hello")
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *var = dynamic_cast<VarDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(var, nullptr);
    auto *tuple = dynamic_cast<TupleLiteralExpr *>(var->getInit());
    ASSERT_NE(tuple, nullptr);
    EXPECT_EQ(tuple->getArity(), 2u);
}

TEST_F(ParserTest, TupleDestructuring) {
    auto result = parse(R"--(
        let (x, y) = (1, 2)
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *var = dynamic_cast<VarDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_TRUE(var->isDestructured());
    EXPECT_EQ(var->getDestructuredNames().size(), 2u);
    EXPECT_EQ(var->getDestructuredNames()[0], "x");
    EXPECT_EQ(var->getDestructuredNames()[1], "y");
}

TEST_F(ParserTest, TupleMemberAccess) {
    auto result = parse(R"--(
        func main() {
            let pair = (1, 2)
            let a = pair.0
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(fn, nullptr);
    // Second statement: let a = pair.0
    auto *stmt = dynamic_cast<VarDecl *>(fn->getBody()->getStatements()[1].get());
    ASSERT_NE(stmt, nullptr);
    auto *member = dynamic_cast<MemberExpr *>(stmt->getInit());
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(member->getMember(), "0");
}

TEST_F(ParserTest, AsyncFuncDecl) {
    auto result = parse("async func fetchData() -> i32 { return 42 }");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->getName(), "fetchData");
    EXPECT_TRUE(fn->isAsync());
}

TEST_F(ParserTest, PubAsyncFuncDecl) {
    auto result = parse("pub async func getData() -> string { return \"hello\" }");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(fn, nullptr);
    EXPECT_EQ(fn->getName(), "getData");
    EXPECT_TRUE(fn->isPublic());
    EXPECT_TRUE(fn->isAsync());
}

TEST_F(ParserTest, AwaitExpr) {
    auto result = parse(R"--(
        async func fetchData() -> i32 { return 42 }
        async func main2() -> i32 {
            let x: i32 = await fetchData()
            return x
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_GE(result.tu->getDeclarations().size(), 2);
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[1].get());
    ASSERT_NE(fn, nullptr);
    EXPECT_TRUE(fn->isAsync());
    // First statement should be a VarDecl with await in init
    auto *varDecl = dynamic_cast<VarDecl *>(fn->getBody()->getStatements()[0].get());
    ASSERT_NE(varDecl, nullptr);
    EXPECT_EQ(varDecl->getName(), "x");
}

// === M35: Const Declaration Tests ===

// ===== TD5: Parser Error Path Tests =====

TEST_F(ParserTest, ErrorExpectedExpression) {
    auto result = parse("func main() { let x = }");
    EXPECT_TRUE(result.hasErrors);
    bool found = false;
    for (auto &d : result.diag.getDiagnostics())
        if (d.id == DiagID::err_expected_expression) found = true;
    EXPECT_TRUE(found);
}

TEST_F(ParserTest, ErrorExpectedType) {
    auto result = parse("func main() { let x: = 1 }");
    EXPECT_TRUE(result.hasErrors);
    bool found = false;
    for (auto &d : result.diag.getDiagnostics())
        if (d.id == DiagID::err_expected_type) found = true;
    EXPECT_TRUE(found);
}

TEST_F(ParserTest, ErrorExpectedToken) {
    auto result = parse("func main( { }");
    EXPECT_TRUE(result.hasErrors);
    bool found = false;
    for (auto &d : result.diag.getDiagnostics())
        if (d.id == DiagID::err_expected_token) found = true;
    EXPECT_TRUE(found);
}

// === M35: Const Declaration Tests ===

TEST_F(ParserTest, ConstDeclBasic) {
    auto result = parse("const x: i32 = 42");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *var = dynamic_cast<VarDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->getName(), "x");
    EXPECT_TRUE(var->isConst());
    EXPECT_FALSE(var->isMutable());
    EXPECT_TRUE(var->hasInit());
}

TEST_F(ParserTest, ConstDeclInferred) {
    auto result = parse("const x = 42");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *var = dynamic_cast<VarDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->getName(), "x");
    EXPECT_TRUE(var->isConst());
    EXPECT_FALSE(var->isMutable());
    EXPECT_TRUE(var->hasInit());
}

TEST_F(ParserTest, MultiBoundInline) {
    auto result = parse("func show<T: Printable + Hashable>(x: T) {}");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->isGeneric());
    ASSERT_EQ(func->getTypeParamBounds().size(), 1);
    ASSERT_EQ(func->getTypeParamBounds().at("T").size(), 2);
    EXPECT_EQ(func->getTypeParamBounds().at("T")[0], "Printable");
    EXPECT_EQ(func->getTypeParamBounds().at("T")[1], "Hashable");
}

TEST_F(ParserTest, MultiBoundWhere) {
    auto result = parse("func show<T>(x: T) where T: Printable + Hashable {}");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->isGeneric());
    ASSERT_EQ(func->getTypeParamBounds().size(), 1);
    ASSERT_EQ(func->getTypeParamBounds().at("T").size(), 2);
    EXPECT_EQ(func->getTypeParamBounds().at("T")[0], "Printable");
    EXPECT_EQ(func->getTypeParamBounds().at("T")[1], "Hashable");
}

TEST_F(ParserTest, MultiBoundMixed) {
    auto result = parse("func f<T: A + B, U: C>(x: T, y: U) {}");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->getTypeParams().size(), 2);
    ASSERT_EQ(func->getTypeParamBounds().size(), 2);
    ASSERT_EQ(func->getTypeParamBounds().at("T").size(), 2);
    EXPECT_EQ(func->getTypeParamBounds().at("T")[0], "A");
    EXPECT_EQ(func->getTypeParamBounds().at("T")[1], "B");
    ASSERT_EQ(func->getTypeParamBounds().at("U").size(), 1);
    EXPECT_EQ(func->getTypeParamBounds().at("U")[0], "C");
}

// === F3: Guard Clause (where) in Match Arms ===

TEST_F(ParserTest, MatchExprWithGuardClause) {
    auto result = parse(R"--(
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
                _ => println(2)
            }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[1].get());
    ASSERT_NE(fn, nullptr);
    // Find the match expression in the function body
    auto &stmts = fn->getBody()->getStatements();
    ASSERT_GE(stmts.size(), 2);
    auto *exprStmt = dynamic_cast<ExprStmt *>(stmts[1].get());
    ASSERT_NE(exprStmt, nullptr);
    auto *matchExpr = dynamic_cast<MatchExpr *>(exprStmt->getExpr());
    ASSERT_NE(matchExpr, nullptr);
    ASSERT_EQ(matchExpr->getArms().size(), 3);
    // First arm has a guard
    EXPECT_NE(matchExpr->getArms()[0].guard, nullptr);
    // Second arm has no guard
    EXPECT_EQ(matchExpr->getArms()[1].guard, nullptr);
    // Third arm (wildcard) has no guard
    EXPECT_EQ(matchExpr->getArms()[2].guard, nullptr);
}

// === F7: Variadic Parameters ===

TEST_F(ParserTest, VariadicParam) {
    auto result = parse(R"--(
        func f(x: i32...) {
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->getParams().size(), 1);
    EXPECT_EQ(func->getParams()[0].name, "x");
    EXPECT_TRUE(func->getParams()[0].isVariadic);
}

TEST_F(ParserTest, VariadicParamWithNormal) {
    auto result = parse(R"--(
        func f(a: i32, b: string...) {
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->getParams().size(), 2);
    EXPECT_EQ(func->getParams()[0].name, "a");
    EXPECT_FALSE(func->getParams()[0].isVariadic);
    EXPECT_EQ(func->getParams()[1].name, "b");
    EXPECT_TRUE(func->getParams()[1].isVariadic);
}

// === F8: Nested Pattern Matching ===

TEST_F(ParserTest, NestedPatternMatch) {
    auto result = parse(R"--(
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
    ASSERT_FALSE(result.hasErrors);
    // Verify the match expression parsed correctly with nested pattern
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[2].get());
    ASSERT_NE(fn, nullptr);
    auto &stmts = fn->getBody()->getStatements();
    ASSERT_GE(stmts.size(), 2);
    auto *exprStmt = dynamic_cast<ExprStmt *>(stmts[1].get());
    ASSERT_NE(exprStmt, nullptr);
    auto *matchExpr = dynamic_cast<MatchExpr *>(exprStmt->getExpr());
    ASSERT_NE(matchExpr, nullptr);
    ASSERT_EQ(matchExpr->getArms().size(), 3);
    // First arm pattern should contain nested pattern string
    EXPECT_NE(matchExpr->getArms()[0].pattern.find("Inner.Val"), std::string::npos);
}

// === Doc Comment Tests ===

TEST_F(ParserTest, DocCommentOnFunc) {
    auto result = parse("/// Adds two numbers\nfunc add(x: i32, y: i32) -> i32 {\n    return x\n}");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(fn, nullptr);
    EXPECT_TRUE(fn->hasDocComment());
    EXPECT_EQ(fn->getDocComment(), "Adds two numbers");
}

TEST_F(ParserTest, DocCommentMultiLine) {
    auto result = parse("/// First line\n/// Second line\nfunc foo() {}");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(fn, nullptr);
    EXPECT_TRUE(fn->hasDocComment());
    EXPECT_EQ(fn->getDocComment(), "First line\nSecond line");
}

TEST_F(ParserTest, DocCommentOnStruct) {
    auto result = parse("/// A 2D point\nstruct Point {\n    var x: i32\n}");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *sd = dynamic_cast<StructDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(sd, nullptr);
    EXPECT_TRUE(sd->hasDocComment());
    EXPECT_EQ(sd->getDocComment(), "A 2D point");
}

TEST_F(ParserTest, DocCommentOnEnum) {
    auto result = parse("/// Direction enum\nenum Dir {\n    case North\n}");
    ASSERT_FALSE(result.hasErrors);
    auto *ed = dynamic_cast<EnumDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(ed, nullptr);
    EXPECT_TRUE(ed->hasDocComment());
    EXPECT_EQ(ed->getDocComment(), "Direction enum");
}

TEST_F(ParserTest, NoDocComment) {
    auto result = parse("func bar() {}");
    ASSERT_FALSE(result.hasErrors);
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(fn, nullptr);
    EXPECT_FALSE(fn->hasDocComment());
}

TEST_F(ParserTest, DocCommentOnPublicFunc) {
    auto result = parse("/// Public function\npub func hello() {}");
    ASSERT_FALSE(result.hasErrors);
    auto *fn = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(fn, nullptr);
    EXPECT_TRUE(fn->hasDocComment());
    EXPECT_EQ(fn->getDocComment(), "Public function");
}

// ===== Error Recovery Tests =====

TEST_F(ParserTest, StructInlineSemicolons) {
    auto result = parse("struct Pt {\n    var x: i32; var y: i32\n}");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *sd = dynamic_cast<StructDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(sd, nullptr);
    EXPECT_EQ(sd->getName(), "Pt");
    ASSERT_EQ(sd->getFields().size(), 2);
    EXPECT_EQ(sd->getFields()[0]->getName(), "x");
    EXPECT_EQ(sd->getFields()[1]->getName(), "y");
}

TEST_F(ParserTest, StructMultipleSemicolons) {
    auto result = parse("struct Pt {\n    var x: i32;; var y: i32;\n}");
    ASSERT_FALSE(result.hasErrors);
    auto *sd = dynamic_cast<StructDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(sd, nullptr);
    ASSERT_EQ(sd->getFields().size(), 2);
    EXPECT_EQ(sd->getFields()[0]->getName(), "x");
    EXPECT_EQ(sd->getFields()[1]->getName(), "y");
}

TEST_F(ParserTest, StructBadFieldRecovery) {
    auto result = parse("struct Pt {\n    123\n    var x: i32\n}");
    EXPECT_TRUE(result.hasErrors);
    auto *sd = dynamic_cast<StructDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(sd, nullptr);
    EXPECT_EQ(sd->getFields().size(), 1);
    EXPECT_EQ(sd->getFields()[0]->getName(), "x");
}

TEST_F(ParserTest, EnumSemicolonRecovery) {
    auto result = parse("enum Color {\n    case Red; case Green; case Blue\n}");
    ASSERT_FALSE(result.hasErrors);
    auto *ed = dynamic_cast<EnumDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(ed, nullptr);
    ASSERT_EQ(ed->getCases().size(), 3);
    EXPECT_EQ(ed->getCases()[0]->getName(), "Red");
    EXPECT_EQ(ed->getCases()[1]->getName(), "Green");
    EXPECT_EQ(ed->getCases()[2]->getName(), "Blue");
}

TEST_F(ParserTest, ImplSemicolonRecovery) {
    auto result = parse("struct Foo {\n    var x: i32\n}\nimpl Foo {\n    func a() {}; func b() {}\n}");
    ASSERT_FALSE(result.hasErrors);
    auto *id = dynamic_cast<ImplDecl *>(result.tu->getDeclarations()[1].get());
    ASSERT_NE(id, nullptr);
    ASSERT_EQ(id->getMethods().size(), 2);
    EXPECT_EQ(id->getMethods()[0]->getName(), "a");
    EXPECT_EQ(id->getMethods()[1]->getName(), "b");
}

TEST_F(ParserTest, ProtocolSemicolonRecovery) {
    auto result = parse("protocol P {\n    func a(); func b()\n}");
    ASSERT_FALSE(result.hasErrors);
    auto *pd = dynamic_cast<ProtocolDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(pd, nullptr);
    ASSERT_EQ(pd->getMethods().size(), 2);
    EXPECT_EQ(pd->getMethods()[0]->getName(), "a");
    EXPECT_EQ(pd->getMethods()[1]->getName(), "b");
}

TEST_F(ParserTest, StructEmptyWithSemicolons) {
    auto result = parse("struct Empty {\n    ;;\n}");
    ASSERT_FALSE(result.hasErrors);
    auto *sd = dynamic_cast<StructDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(sd, nullptr);
    EXPECT_EQ(sd->getFields().size(), 0);
}

TEST_F(ParserTest, EnumBadCaseRecovery) {
    auto result = parse("enum E {\n    123\n    case A\n}");
    EXPECT_TRUE(result.hasErrors);
    auto *ed = dynamic_cast<EnumDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(ed, nullptr);
    EXPECT_EQ(ed->getCases().size(), 1);
    EXPECT_EQ(ed->getCases()[0]->getName(), "A");
}

// =============================================================================
// Foreign Keyword Detection Tests
// =============================================================================

TEST_F(ParserTest, ForeignKeyword_Fn) {
    auto result = parse("fn foo() {}");
    EXPECT_TRUE(result.hasErrors);
    bool found = false;
    for (auto &d : result.diag.getDiagnostics()) {
        if (d.id == DiagID::err_foreign_keyword)
            found = true;
    }
    EXPECT_TRUE(found);
    // Should also emit note_use_func_keyword
    bool foundNote = false;
    for (auto &d : result.diag.getDiagnostics()) {
        if (d.id == DiagID::note_use_func_keyword)
            foundNote = true;
    }
    EXPECT_TRUE(foundNote);
}

TEST_F(ParserTest, ForeignKeyword_Def) {
    auto result = parse("def bar() {}");
    EXPECT_TRUE(result.hasErrors);
    bool found = false;
    for (auto &d : result.diag.getDiagnostics()) {
        if (d.id == DiagID::err_foreign_keyword)
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserTest, ForeignKeyword_Function) {
    auto result = parse("function baz() {}");
    EXPECT_TRUE(result.hasErrors);
    bool found = false;
    for (auto &d : result.diag.getDiagnostics()) {
        if (d.id == DiagID::err_foreign_keyword)
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserTest, ClassDecl_Empty) {
    auto result = parse("class Foo {}");
    EXPECT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1u);
    auto *cls = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->getName(), "Foo");
    EXPECT_FALSE(cls->hasParentClass());
    EXPECT_TRUE(cls->getMembers().empty());
}

TEST_F(ParserTest, ForeignKeyword_Trait) {
    auto result = parse("trait Foo {}");
    EXPECT_TRUE(result.hasErrors);
    bool found = false;
    for (auto &d : result.diag.getDiagnostics()) {
        if (d.id == DiagID::err_foreign_keyword &&
            d.message.find("trait") != std::string::npos &&
            d.message.find("protocol") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserTest, ForeignKeyword_Interface) {
    auto result = parse("interface Foo {}");
    EXPECT_TRUE(result.hasErrors);
    bool found = false;
    for (auto &d : result.diag.getDiagnostics()) {
        if (d.id == DiagID::err_foreign_keyword &&
            d.message.find("interface") != std::string::npos &&
            d.message.find("protocol") != std::string::npos)
            found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(ParserTest, LetMut) {
    auto result = parse("let mut x = 5");
    EXPECT_TRUE(result.hasErrors);
    bool found = false;
    for (auto &d : result.diag.getDiagnostics()) {
        if (d.id == DiagID::err_let_mut_not_supported)
            found = true;
    }
    EXPECT_TRUE(found);
    // Should still parse as a mutable variable
    ASSERT_EQ(result.tu->getDeclarations().size(), 1);
    auto *var = dynamic_cast<VarDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(var, nullptr);
    EXPECT_TRUE(var->isMutable());
    EXPECT_EQ(var->getName(), "x");
}

TEST_F(ParserTest, ForeignKeyword_RecoveryParsesBody) {
    auto result = parse("fn add(a: i32, b: i32) -> i32 {\n    return a + b\n}");
    EXPECT_TRUE(result.hasErrors);
    // Despite the error, the parser should recover and parse the function body
    ASSERT_GE(result.tu->getDeclarations().size(), 1u);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->getName(), "add");
    EXPECT_EQ(func->getParams().size(), 2);
    EXPECT_TRUE(func->hasBody());
}

// =============================================================================
// Class Declaration Parser Tests
// =============================================================================

TEST_F(ParserTest, ClassDecl_WithFields) {
    auto result = parse(R"--(
        class Point {
            var x: f64
            var y: f64
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1u);
    auto *cls = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->getName(), "Point");
    auto fields = cls->getFields();
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0]->getName(), "x");
    EXPECT_EQ(fields[1]->getName(), "y");
    EXPECT_TRUE(fields[0]->isMutable());
}

TEST_F(ParserTest, ClassDecl_WithInit) {
    auto result = parse(R"--(
        class Foo {
            var x: i32
            init(x: i32) {
                self.x = x
            }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *cls = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->getName(), "Foo");
    auto *initMethod = cls->getInit();
    ASSERT_NE(initMethod, nullptr);
    EXPECT_EQ(initMethod->getName(), "init");
    EXPECT_TRUE(initMethod->hasBody());
    // x param only (self is implicit)
    EXPECT_EQ(initMethod->getParams().size(), 1u);
}

TEST_F(ParserTest, ClassDecl_WithDeinit) {
    auto result = parse(R"--(
        class Resource {
            var handle: i32
            deinit() {
                println(self.handle)
            }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *cls = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(cls, nullptr);
    auto *deinitMethod = cls->getDeinit();
    ASSERT_NE(deinitMethod, nullptr);
    EXPECT_EQ(deinitMethod->getName(), "deinit");
    EXPECT_TRUE(deinitMethod->hasBody());
    EXPECT_EQ(deinitMethod->getParams().size(), 0u);
}

TEST_F(ParserTest, ClassDecl_WithMethods) {
    auto result = parse(R"--(
        class Calculator {
            var value: i32
            func add(n: i32) -> i32 {
                return self.value + n
            }
            func reset() {
                self.value = 0
            }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *cls = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->getName(), "Calculator");
    auto methods = cls->getMethods();
    ASSERT_EQ(methods.size(), 2u);
    EXPECT_EQ(methods[0]->getName(), "add");
    EXPECT_EQ(methods[1]->getName(), "reset");
}

TEST_F(ParserTest, ClassDecl_Inheritance) {
    auto result = parse(R"--(
        class Animal {
            var name: string
        }
        class Dog : Animal {
            var breed: string
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 2u);
    auto *dog = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[1].get());
    ASSERT_NE(dog, nullptr);
    EXPECT_EQ(dog->getName(), "Dog");
    EXPECT_TRUE(dog->hasParentClass());
    EXPECT_EQ(dog->getParentClass(), "Animal");
}

TEST_F(ParserTest, ClassDecl_InheritanceWithProtocols) {
    auto result = parse(R"--(
        class Dog : Animal, Printable, Hashable {
            var breed: string
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *cls = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->getName(), "Dog");
    EXPECT_TRUE(cls->hasParentClass());
    EXPECT_EQ(cls->getParentClass(), "Animal");
    ASSERT_EQ(cls->getProtocols().size(), 2u);
    EXPECT_EQ(cls->getProtocols()[0], "Printable");
    EXPECT_EQ(cls->getProtocols()[1], "Hashable");
}

TEST_F(ParserTest, ClassDecl_OverrideMethod) {
    auto result = parse(R"--(
        class Animal {
            func speak() {
                println(0)
            }
        }
        class Dog : Animal {
            override func speak() {
                println(1)
            }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 2u);
    auto *dog = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[1].get());
    ASSERT_NE(dog, nullptr);
    EXPECT_TRUE(dog->isOverride("speak"));
}

TEST_F(ParserTest, ClassDecl_PrivateField) {
    auto result = parse(R"--(
        class Account {
            private var balance: i32
            var name: string
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *cls = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_TRUE(cls->isPrivate("balance"));
    EXPECT_FALSE(cls->isPrivate("name"));
}

TEST_F(ParserTest, ClassDecl_Generic) {
    auto result = parse(R"--(
        class Box<T> {
            var value: T
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *cls = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->getName(), "Box");
    EXPECT_TRUE(cls->isGeneric());
    ASSERT_EQ(cls->getTypeParams().size(), 1u);
    EXPECT_EQ(cls->getTypeParams()[0], "T");
}

TEST_F(ParserTest, ClassDecl_PublicKeyword) {
    auto result = parse("pub class Foo {}");
    ASSERT_FALSE(result.hasErrors);
    auto *cls = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->getName(), "Foo");
    EXPECT_TRUE(cls->isPublic());
}

TEST_F(ParserTest, ClassDecl_InitAndDeinit) {
    auto result = parse(R"--(
        class ManagedResource {
            var id: i32
            init(id: i32) {
                self.id = id
            }
            deinit() {
                println(self.id)
            }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *cls = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->getName(), "ManagedResource");
    ASSERT_NE(cls->getInit(), nullptr);
    ASSERT_NE(cls->getDeinit(), nullptr);
    EXPECT_EQ(cls->getInit()->getName(), "init");
    EXPECT_EQ(cls->getDeinit()->getName(), "deinit");
    auto fields = cls->getFields();
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_EQ(fields[0]->getName(), "id");
}

TEST_F(ParserTest, ClassDecl_LetField) {
    auto result = parse(R"--(
        class Config {
            let name: string
            var value: i32
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *cls = dynamic_cast<ClassDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(cls, nullptr);
    auto fields = cls->getFields();
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_EQ(fields[0]->getName(), "name");
    EXPECT_FALSE(fields[0]->isMutable());
    EXPECT_EQ(fields[1]->getName(), "value");
    EXPECT_TRUE(fields[1]->isMutable());
}

// ===== Y3: Where Clause Associated Type Constraint Parser Tests =====

TEST_F(ParserTest, WhereClause_AssocTypeEqual) {
    auto result = parse(R"--(
        func process<T: Container>(c: T) where T.Item == i32 {
            println(0)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->getName(), "process");
    ASSERT_EQ(func->getWhereConstraints().size(), 1u);
    auto &wc = func->getWhereConstraints()[0];
    EXPECT_EQ(wc.kind, WhereConstraint::Kind::AssociatedTypeEqual);
    EXPECT_EQ(wc.paramName, "T");
    EXPECT_EQ(wc.assocTypeName, "Item");
    EXPECT_EQ(wc.equalTypeName, "i32");
}

TEST_F(ParserTest, WhereClause_AssocTypeBound) {
    auto result = parse(R"--(
        func process<T: Container>(c: T) where T.Item: Comparable {
            println(0)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->getWhereConstraints().size(), 1u);
    auto &wc = func->getWhereConstraints()[0];
    EXPECT_EQ(wc.kind, WhereConstraint::Kind::AssociatedTypeBound);
    EXPECT_EQ(wc.paramName, "T");
    EXPECT_EQ(wc.assocTypeName, "Item");
    ASSERT_EQ(wc.protocolNames.size(), 1u);
    EXPECT_EQ(wc.protocolNames[0], "Comparable");
}

TEST_F(ParserTest, WhereClause_Mixed) {
    auto result = parse(R"--(
        func process<T>(c: T) where T: Container, T.Item == i32 {
            println(0)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    // T: Container is a regular bound
    auto &bounds = func->getTypeParamBounds();
    auto it = bounds.find("T");
    ASSERT_NE(it, bounds.end());
    EXPECT_EQ(it->second.size(), 1u);
    EXPECT_EQ(it->second[0], "Container");
    // T.Item == i32 is a where constraint
    ASSERT_EQ(func->getWhereConstraints().size(), 1u);
    EXPECT_EQ(func->getWhereConstraints()[0].kind, WhereConstraint::Kind::AssociatedTypeEqual);
}

TEST_F(ParserTest, WhereClause_AssocMultipleBounds) {
    auto result = parse(R"--(
        func process<T: Container>(c: T) where T.Item: A + B {
            println(0)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->getWhereConstraints().size(), 1u);
    auto &wc = func->getWhereConstraints()[0];
    EXPECT_EQ(wc.kind, WhereConstraint::Kind::AssociatedTypeBound);
    ASSERT_EQ(wc.protocolNames.size(), 2u);
    EXPECT_EQ(wc.protocolNames[0], "A");
    EXPECT_EQ(wc.protocolNames[1], "B");
}

TEST_F(ParserTest, WhereClause_AssocTypeInImpl) {
    auto result = parse(R"--(
        impl MyType : Container where T.Item == i32 {
            func get(self) -> i32 { return 0 }
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *impl = dynamic_cast<ImplDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(impl, nullptr);
    ASSERT_EQ(impl->getWhereConstraints().size(), 1u);
    EXPECT_EQ(impl->getWhereConstraints()[0].kind, WhereConstraint::Kind::AssociatedTypeEqual);
    EXPECT_EQ(impl->getWhereConstraints()[0].paramName, "T");
    EXPECT_EQ(impl->getWhereConstraints()[0].assocTypeName, "Item");
}

TEST_F(ParserTest, WhereClause_MultipleAssocConstraints) {
    auto result = parse(R"--(
        func process<T>(c: T) where T.Item == i32, T.Key == string {
            println(0)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->getWhereConstraints().size(), 2u);
    EXPECT_EQ(func->getWhereConstraints()[0].assocTypeName, "Item");
    EXPECT_EQ(func->getWhereConstraints()[0].equalTypeName, "i32");
    EXPECT_EQ(func->getWhereConstraints()[1].assocTypeName, "Key");
    EXPECT_EQ(func->getWhereConstraints()[1].equalTypeName, "string");
}

// ===== Y3: Associated Type Reference Parser Tests =====

TEST_F(ParserTest, AssocTypeRepr_ReturnType) {
    auto result = parse(R"--(
        func get<T: Container>(c: T) -> T.Item {
            return c.get()
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    auto *retType = func->getReturnType();
    ASSERT_NE(retType, nullptr);
    EXPECT_EQ(retType->getKind(), TypeRepr::Kind::AssociatedType);
    auto *assocType = static_cast<const AssociatedTypeRepr *>(retType);
    EXPECT_EQ(assocType->getBaseName(), "T");
    EXPECT_EQ(assocType->getAssocTypeName(), "Item");
}

TEST_F(ParserTest, AssocTypeRepr_ParamType) {
    auto result = parse(R"--(
        func set<T: Container>(c: T, item: T.Item) {
            println(0)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    ASSERT_GE(func->getParams().size(), 2u);
    auto *paramType = func->getParams()[1].type.get();
    ASSERT_NE(paramType, nullptr);
    EXPECT_EQ(paramType->getKind(), TypeRepr::Kind::AssociatedType);
}

TEST_F(ParserTest, AssocTypeRepr_ToString) {
    auto result = parse(R"--(
        func get<T: Container>(c: T) -> T.Item {
            return c.get()
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->getReturnType()->toString(), "T.Item");
}

TEST_F(ParserTest, AssocTypeRepr_MultipleAssoc) {
    auto result = parse(R"--(
        func swap<T: Container>(a: T.Key, b: T.Value) {
            println(0)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    ASSERT_GE(func->getParams().size(), 2u);
    auto *p0 = func->getParams()[0].type.get();
    auto *p1 = func->getParams()[1].type.get();
    EXPECT_EQ(p0->getKind(), TypeRepr::Kind::AssociatedType);
    EXPECT_EQ(p1->getKind(), TypeRepr::Kind::AssociatedType);
    EXPECT_EQ(p0->toString(), "T.Key");
    EXPECT_EQ(p1->toString(), "T.Value");
}

// === Y4: Postfix ? Operator ===

TEST_F(ParserTest, PostfixQuestion_BasicParse) {
    auto result = parse(R"--(
        func foo() -> Result<i32, string> {
            return Result.ok(1)
        }
        func bar() -> Result<i32, string> {
            let x = foo()?
            return Result.ok(x)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, PostfixQuestion_InFuncCall) {
    auto result = parse(R"--(
        func foo() -> Result<i32, string> {
            return Result.ok(1)
        }
        func bar(x: i32) -> i32 {
            return x
        }
        func baz() -> Result<i32, string> {
            let y = bar(foo()?)
            return Result.ok(y)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, PostfixQuestion_Chained) {
    auto result = parse(R"--(
        func foo() -> Result<i32, string> {
            return Result.ok(42)
        }
        func process() -> Result<i32, string> {
            let x = foo()?
            return Result.ok(x)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, PostfixQuestion_WithBinaryOp) {
    auto result = parse(R"--(
        func foo() -> Result<i32, string> {
            return Result.ok(1)
        }
        func bar() -> Result<i32, string> {
            let x = foo()? + 5
            return Result.ok(x)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, PostfixQuestion_TernaryStillWorks) {
    auto result = parse(R"--(
        func main() {
            let cond = true
            let x = cond ? 1 : 2
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

TEST_F(ParserTest, PostfixQuestion_WithSemicolon) {
    auto result = parse(R"--(
        func foo() -> Result<i32, string> {
            return Result.ok(1)
        }
        func bar() -> Result<i32, string> {
            foo()?
            return Result.ok(0)
        }
    )--");
    ASSERT_FALSE(result.hasErrors);
}

// === FFI Tests ===

TEST_F(ParserTest, FFI_ExternCSingleFunc) {
    auto result = parse(R"--(extern "C" func c_abs(x: i32) -> i32)--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1u);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->getName(), "c_abs");
    EXPECT_TRUE(func->isExtern());
    EXPECT_FALSE(func->hasBody());
}

TEST_F(ParserTest, FFI_ExternCBlock) {
    auto result = parse(R"--(extern "C" {
        func malloc(size: u64) -> ref i8
        func free(ptr: ref i8)
    })--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 2u);
    auto *f1 = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    auto *f2 = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[1].get());
    ASSERT_NE(f1, nullptr);
    ASSERT_NE(f2, nullptr);
    EXPECT_EQ(f1->getName(), "malloc");
    EXPECT_TRUE(f1->isExtern());
    EXPECT_EQ(f2->getName(), "free");
    EXPECT_TRUE(f2->isExtern());
}

TEST_F(ParserTest, FFI_ExternCVarargs) {
    auto result = parse(R"--(extern "C" func printf(fmt: string, ...) -> i32)--");
    ASSERT_FALSE(result.hasErrors);
    ASSERT_EQ(result.tu->getDeclarations().size(), 1u);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->getName(), "printf");
    EXPECT_TRUE(func->isExtern());
    EXPECT_TRUE(func->isCVarargs());
    EXPECT_EQ(func->getParams().size(), 1u);
}

TEST_F(ParserTest, FFI_ExternCNoBody) {
    auto result = parse(R"--(extern "C" func strlen(str: ref i8) -> u64)--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_FALSE(func->hasBody());
    EXPECT_TRUE(func->isExtern());
}

TEST_F(ParserTest, FFI_ExternCWithPub) {
    auto result = parse(R"--(pub extern "C" func c_abs(x: i32) -> i32)--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->isPublic());
    EXPECT_TRUE(func->isExtern());
}

TEST_F(ParserTest, FFI_ExternCMultipleParams) {
    auto result = parse(R"--(extern "C" func memcpy(dst: ref i8, src: ref i8, n: u64) -> ref i8)--");
    ASSERT_FALSE(result.hasErrors);
    auto *func = dynamic_cast<FuncDecl *>(result.tu->getDeclarations()[0].get());
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->getParams().size(), 3u);
    EXPECT_TRUE(func->isExtern());
}

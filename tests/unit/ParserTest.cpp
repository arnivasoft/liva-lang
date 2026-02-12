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

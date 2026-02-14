// Self-hosting end-to-end tests: compile Liva programs, run them, verify output.
// Only built when LIVA_HAS_LLVM is defined (Clang build with LLVM backend).

#ifdef LIVA_HAS_LLVM

#include "liva/Driver/CompilerInstance.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

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

#endif // LIVA_HAS_LLVM

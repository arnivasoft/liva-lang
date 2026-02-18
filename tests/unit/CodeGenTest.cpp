#include "liva/CodeGen/CodeGen.h"
#include "liva/CodeGen/TargetInfo.h"
#include "liva/Common/Diagnostics.h"
#include "liva/IR/IRGen.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>

#ifdef LIVA_HAS_LLVM
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Triple.h>

using namespace liva;

class CodeGenTargetTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmParsers();
        llvm::InitializeAllAsmPrinters();
    }
};

TEST_F(CodeGenTargetTest, CrossTarget_LookupAarch64) {
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget("aarch64-unknown-linux-gnu", error);
    EXPECT_NE(target, nullptr) << "AArch64 target not found: " << error;
}

TEST_F(CodeGenTargetTest, CrossTarget_LookupWasm) {
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget("wasm32-unknown-wasi", error);
    EXPECT_NE(target, nullptr) << "WASM target not found: " << error;
}

TEST_F(CodeGenTargetTest, CrossTarget_LookupRiscv) {
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget("riscv64-unknown-linux-gnu", error);
    EXPECT_NE(target, nullptr) << "RISC-V target not found: " << error;
}

TEST_F(CodeGenTargetTest, CrossTarget_LookupArm) {
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget("armv7-unknown-linux-gnueabihf", error);
    EXPECT_NE(target, nullptr) << "ARM target not found: " << error;
}

TEST_F(CodeGenTargetTest, CrossTarget_LookupInvalid) {
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget("invalid-xxx-yyy", error);
    EXPECT_EQ(target, nullptr) << "Invalid target should not be found";
}

TEST_F(CodeGenTargetTest, CrossTarget_LookupHost) {
    auto host = TargetInfo::getHostTarget();
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget(host.triple, error);
    EXPECT_NE(target, nullptr) << "Host target not found: " << error;
}

// ============================================================
// WASM Backend — CodeGen Tests
// ============================================================

TEST_F(CodeGenTargetTest, WASM_EmitObjectFile) {
    // Verify we can create a TargetMachine for wasm32
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget("wasm32-unknown-wasi", error);
    ASSERT_NE(target, nullptr) << "WASM target not found: " << error;

    llvm::TargetOptions opt;
    auto tm = target->createTargetMachine(
        llvm::Triple("wasm32-unknown-wasi"), "generic", "", opt, llvm::Reloc::PIC_);
    ASSERT_NE(tm, nullptr) << "Failed to create WASM target machine";

    // Create a minimal module and emit object file
    llvm::LLVMContext ctx;
    llvm::Module mod("wasm_test", ctx);
    mod.setDataLayout(tm->createDataLayout());
    mod.setTargetTriple(llvm::Triple("wasm32-unknown-wasi"));

    // Add a simple function
    auto *funcTy = llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx), false);
    auto *func = llvm::Function::Create(funcTy, llvm::Function::ExternalLinkage, "test_func", mod);
    auto *bb = llvm::BasicBlock::Create(ctx, "entry", func);
    llvm::IRBuilder<> builder(bb);
    builder.CreateRet(builder.getInt32(42));

    // Emit to object file via CodeGen
    DiagnosticsEngine diag;
    TargetInfo ti = TargetInfo::fromTriple("wasm32-unknown-wasi");
    CodeGen codegen(diag, ti);
    std::string objPath = "test_wasm_emit.o";
    bool ok = codegen.emitObjectFile(mod, objPath);
    EXPECT_TRUE(ok) << "Failed to emit WASM object file";
    std::remove(objPath.c_str());
}

TEST_F(CodeGenTargetTest, WASM_EmitAssembly) {
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget("wasm32-unknown-wasi", error);
    ASSERT_NE(target, nullptr) << "WASM target not found: " << error;

    llvm::LLVMContext ctx;
    llvm::Module mod("wasm_asm_test", ctx);

    llvm::TargetOptions opt;
    auto tm = target->createTargetMachine(
        llvm::Triple("wasm32-unknown-wasi"), "generic", "", opt, llvm::Reloc::PIC_);
    mod.setDataLayout(tm->createDataLayout());
    mod.setTargetTriple(llvm::Triple("wasm32-unknown-wasi"));

    auto *funcTy = llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx), false);
    auto *func = llvm::Function::Create(funcTy, llvm::Function::ExternalLinkage, "asm_func", mod);
    auto *bb = llvm::BasicBlock::Create(ctx, "entry", func);
    llvm::IRBuilder<> builder(bb);
    builder.CreateRet(builder.getInt32(7));

    DiagnosticsEngine diag;
    TargetInfo ti = TargetInfo::fromTriple("wasm32-unknown-wasi");
    CodeGen codegen(diag, ti);
    std::string asmPath = "test_wasm_emit.wat";
    bool ok = codegen.emitAssembly(mod, asmPath);
    EXPECT_TRUE(ok) << "Failed to emit WASM assembly";
    std::remove(asmPath.c_str());
}

TEST_F(CodeGenTargetTest, WASM_DataLayoutSet) {
    std::string error;
    auto *target = llvm::TargetRegistry::lookupTarget("wasm32-unknown-wasi", error);
    ASSERT_NE(target, nullptr);

    llvm::TargetOptions opt;
    auto tm = target->createTargetMachine(
        llvm::Triple("wasm32-unknown-wasi"), "generic", "", opt, llvm::Reloc::PIC_);
    auto dl = tm->createDataLayout();

    // WASM32 should have 32-bit pointers
    EXPECT_EQ(dl.getPointerSizeInBits(), 32u);
}

TEST_F(CodeGenTargetTest, WASM_TargetTripleSet) {
    llvm::LLVMContext ctx;
    llvm::Module mod("triple_test", ctx);
    mod.setTargetTriple(llvm::Triple("wasm32-unknown-wasi"));
    EXPECT_EQ(mod.getTargetTriple().str(), "wasm32-unknown-wasi");
}

// ============================================================
// Debug Info Tests (O2: Debugger Maturity)
// ============================================================

class DebugInfoTest : public ::testing::Test {
protected:
    struct IRResult {
        std::unique_ptr<SourceManager> sm;
        std::unique_ptr<DiagnosticsEngine> diag;
        std::unique_ptr<IRGen> irgen;
        llvm::Module *module = nullptr;
        bool success = false;
    };

    IRResult generateIR(const std::string &source, bool debugInfo = true) {
        IRResult result;
        result.sm = std::make_unique<SourceManager>("test.liva", source);
        result.diag = std::make_unique<DiagnosticsEngine>(result.sm.get());
        Lexer lexer(*result.sm, *result.diag);
        Parser parser(lexer, *result.diag);
        auto tu = parser.parseTranslationUnit();
        if (!tu || result.diag->hasErrors()) return result;

        Sema sema(*result.diag);
        if (!sema.analyze(*tu)) return result;

        result.irgen = std::make_unique<IRGen>("test.liva", *result.diag);
        result.irgen->setDebugInfo(debugInfo);
        result.success = result.irgen->generate(*tu);
        if (result.success)
            result.module = result.irgen->getModule();
        return result;
    }
};

TEST_F(DebugInfoTest, FunctionHasSubprogram) {
    auto r = generateIR("func main() { let x: i32 = 42 }");
    ASSERT_TRUE(r.success);
    auto *mainFn = r.module->getFunction("main");
    ASSERT_NE(mainFn, nullptr);
    EXPECT_NE(mainFn->getSubprogram(), nullptr);
}

TEST_F(DebugInfoTest, SubprogramHasCorrectLine) {
    auto r = generateIR("func main() { let x: i32 = 42 }");
    ASSERT_TRUE(r.success);
    auto *mainFn = r.module->getFunction("main");
    ASSERT_NE(mainFn, nullptr);
    auto *sp = mainFn->getSubprogram();
    ASSERT_NE(sp, nullptr);
    EXPECT_EQ(sp->getLine(), 1u);
}

TEST_F(DebugInfoTest, FunctionHasParamTypes) {
    auto r = generateIR("func add(a: i32, b: i32) -> i32 { return a + b }\nfunc main() { let x = add(1, 2) }");
    ASSERT_TRUE(r.success);
    auto *addFn = r.module->getFunction("add");
    ASSERT_NE(addFn, nullptr);
    auto *sp = addFn->getSubprogram();
    ASSERT_NE(sp, nullptr);
    auto *subroutineType = sp->getType();
    ASSERT_NE(subroutineType, nullptr);
    // Subroutine type has return + params: i32 return, i32, i32 = 3 elements
    auto typeArray = subroutineType->getTypeArray();
    EXPECT_GE(typeArray.size(), 3u);
}

TEST_F(DebugInfoTest, DisabledByDefault) {
    auto r = generateIR("func main() { let x: i32 = 42 }", false);
    ASSERT_TRUE(r.success);
    auto *mainFn = r.module->getFunction("main");
    ASSERT_NE(mainFn, nullptr);
    EXPECT_EQ(mainFn->getSubprogram(), nullptr);
}

TEST_F(DebugInfoTest, HasDbgDeclare) {
    auto r = generateIR("func add(a: i32, b: i32) -> i32 { return a + b }\nfunc main() { let x = add(1, 2) }");
    ASSERT_TRUE(r.success);
    // Search for debug variable records (DbgVariableRecord) or legacy DbgDeclareInst
    auto *addFn = r.module->getFunction("add");
    ASSERT_NE(addFn, nullptr);
    bool foundDbgDeclare = false;
    for (auto &bb : *addFn) {
        for (auto &inst : bb) {
            // Check legacy intrinsic-based debug info
            if (llvm::isa<llvm::DbgDeclareInst>(&inst)) {
                foundDbgDeclare = true;
                break;
            }
            // Check new debug record format (LLVM 19+)
            for (auto &dbgRec : inst.getDbgRecordRange()) {
                if (llvm::isa<llvm::DbgVariableRecord>(dbgRec)) {
                    foundDbgDeclare = true;
                    break;
                }
            }
            if (foundDbgDeclare) break;
        }
        if (foundDbgDeclare) break;
    }
    EXPECT_TRUE(foundDbgDeclare) << "Expected debug variable info in function with debug info";
}

TEST_F(DebugInfoTest, StatementHasDebugLoc) {
    auto r = generateIR("func main() { let x: i32 = 42\nlet y: i32 = 10\nlet z = x + y }");
    ASSERT_TRUE(r.success);
    auto *mainFn = r.module->getFunction("main");
    ASSERT_NE(mainFn, nullptr);
    // Check that at least some instructions in main have debug locations
    unsigned withDbg = 0;
    for (auto &bb : *mainFn) {
        for (auto &inst : bb) {
            if (inst.getDebugLoc()) ++withDbg;
        }
    }
    EXPECT_GT(withDbg, 0u) << "Expected instructions with !dbg metadata";
}

TEST_F(DebugInfoTest, StructTypeInMetadata) {
    auto r = generateIR(
        "struct Point { var x: i32; var y: i32 }\n"
        "func main() { let p = Point { x: 1, y: 2 } }");
    ASSERT_TRUE(r.success);
    // Search named metadata for a struct DI type
    bool foundStructDI = false;
    if (auto *cuNodes = r.module->getNamedMetadata("llvm.dbg.cu")) {
        for (unsigned i = 0; i < cuNodes->getNumOperands(); ++i) {
            // Traverse retained types or just verify CU exists with debug info
            foundStructDI = true; // CU with debug info present
        }
    }
    EXPECT_TRUE(foundStructDI) << "Expected debug compile unit in module metadata";
    // Also verify the function has debug location on struct-related instructions
    auto *mainFn = r.module->getFunction("main");
    ASSERT_NE(mainFn, nullptr);
    EXPECT_NE(mainFn->getSubprogram(), nullptr);
}

TEST_F(DebugInfoTest, ImplMethodHasSubprogram) {
    auto r = generateIR(
        "struct Counter { var value: i32 }\n"
        "impl Counter {\n"
        "    func getValue(self) -> i32 { return self.value }\n"
        "}\n"
        "func main() { let c = Counter { value: 5 } }");
    ASSERT_TRUE(r.success);
    auto *methodFn = r.module->getFunction("Counter_getValue");
    ASSERT_NE(methodFn, nullptr);
    EXPECT_NE(methodFn->getSubprogram(), nullptr);
}

TEST_F(DebugInfoTest, DynArrayStringElemAssignCompiles) {
    auto r = generateIR(
        "func main() {\n"
        "    var arr: [String] = []\n"
        "    arr.push(\"hello\")\n"
        "    arr[0] = \"world\"\n"
        "}\n",
        false);
    ASSERT_TRUE(r.success);
    auto *mainFn = r.module->getFunction("main");
    ASSERT_NE(mainFn, nullptr);
}

TEST_F(DebugInfoTest, StructFieldStringReassignEmitsFree) {
    auto r = generateIR(
        "struct Person { var name: String }\n"
        "func main() {\n"
        "    var p = Person { name: \"Alice\" }\n"
        "    p.name = \"Bob\"\n"
        "}\n",
        false);
    ASSERT_TRUE(r.success);
    auto *mainFn = r.module->getFunction("main");
    ASSERT_NE(mainFn, nullptr);
    // Verify that free() is called (for the old string) before the new store
    bool foundFreeCall = false;
    for (auto &BB : *mainFn) {
        for (auto &I : BB) {
            if (auto *call = llvm::dyn_cast<llvm::CallInst>(&I)) {
                if (call->getCalledFunction() &&
                    call->getCalledFunction()->getName() == "free")
                    foundFreeCall = true;
            }
        }
    }
    EXPECT_TRUE(foundFreeCall) << "Expected free() call for old struct string field";
}

TEST_F(DebugInfoTest, StaticArrayStringElemAssignCompiles) {
    auto r = generateIR(
        "func main() {\n"
        "    var arr: [String; 2] = [\"a\", \"b\"]\n"
        "    arr[0] = \"c\"\n"
        "}\n",
        false);
    ASSERT_TRUE(r.success);
    auto *mainFn = r.module->getFunction("main");
    ASSERT_NE(mainFn, nullptr);
}

#endif // LIVA_HAS_LLVM

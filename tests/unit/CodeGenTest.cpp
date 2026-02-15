#include "liva/CodeGen/CodeGen.h"
#include "liva/CodeGen/TargetInfo.h"
#include "liva/Common/Diagnostics.h"
#include <gtest/gtest.h>

#ifdef LIVA_HAS_LLVM
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>

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

#endif // LIVA_HAS_LLVM

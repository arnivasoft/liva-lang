#include "liva/Common/Diagnostics.h"
#include "liva/Common/SourceLocation.h"
#include "liva/Driver/ProjectConfig.h"
#include "liva/Lexer/Lexer.h"
#include "liva/Parser/Parser.h"
#include "liva/Plugin/BuiltinPlugins.h"
#include "liva/Plugin/PluginRegistry.h"
#include "liva/Sema/Sema.h"
#include <gtest/gtest.h>

using namespace liva;

class PluginTest : public ::testing::Test {
protected:
    struct PipelineResult {
        std::unique_ptr<SourceManager> sm;
        DiagnosticsEngine diag;
        std::unique_ptr<TranslationUnit> tu;
        bool parseOk;
        bool semaOk;
    };

    PipelineResult runWithPlugins(const std::string &source, PluginRegistry &reg) {
        PipelineResult result;
        result.sm = std::make_unique<SourceManager>("test.liva", source);
        result.diag.setSourceManager(result.sm.get());
        Lexer lexer(*result.sm, result.diag);
        Parser parser(lexer, result.diag);
        result.tu = parser.parseTranslationUnit();

        if (result.diag.hasErrors()) {
            result.parseOk = false;
            result.semaOk = false;
            return result;
        }
        result.parseOk = true;

        // Run afterParse plugins
        reg.runAfterParse(*result.tu, result.diag);

        // Run sema
        Sema sema(result.diag);
        result.semaOk = sema.analyze(*result.tu);

        // Run afterSema plugins
        reg.runAfterSema(*result.tu, result.diag);

        return result;
    }

    bool hasDiag(const PipelineResult &result, DiagID id) {
        for (auto &d : result.diag.getDiagnostics()) {
            if (d.id == id)
                return true;
        }
        return false;
    }

    int countDiag(const PipelineResult &result, DiagID id) {
        int count = 0;
        for (auto &d : result.diag.getDiagnostics()) {
            if (d.id == id)
                ++count;
        }
        return count;
    }

    bool diagContains(const PipelineResult &result, DiagID id, const std::string &substr) {
        for (auto &d : result.diag.getDiagnostics()) {
            if (d.id == id && d.message.find(substr) != std::string::npos)
                return true;
        }
        return false;
    }
};

// ==================== Registry Tests ====================

TEST_F(PluginTest, RegistryRegisterAndRetrieve) {
    PluginRegistry reg;
    reg.registerPlugin(std::make_unique<NamingConventionPlugin>());
    EXPECT_EQ(reg.size(), 1u);
    EXPECT_NE(reg.getPlugin("naming-convention"), nullptr);
    EXPECT_EQ(reg.getPlugin("nonexistent"), nullptr);
}

TEST_F(PluginTest, RegistryCreateWithBuiltins) {
    auto reg = PluginRegistry::createWithBuiltins();
    EXPECT_EQ(reg.size(), 2u);
    EXPECT_NE(reg.getPlugin("naming-convention"), nullptr);
    EXPECT_NE(reg.getPlugin("unused-function"), nullptr);
}

TEST_F(PluginTest, RegistryClear) {
    auto reg = PluginRegistry::createWithBuiltins();
    EXPECT_EQ(reg.size(), 2u);
    reg.clear();
    EXPECT_EQ(reg.size(), 0u);
}

TEST_F(PluginTest, DisabledPluginNotInvoked) {
    auto reg = PluginRegistry::createWithBuiltins();
    reg.getPlugin("naming-convention")->setEnabled(false);

    auto result = runWithPlugins(R"(
        struct bad_name {
            var x: i32
        }
        func main() {
            println(42)
        }
    )", reg);

    EXPECT_TRUE(result.parseOk);
    EXPECT_FALSE(hasDiag(result, DiagID::warn_lint_type_naming));
}

// ==================== TOML Configuration Tests ====================

TEST_F(PluginTest, ConfigureFromTOML_DisablePlugin) {
    auto reg = PluginRegistry::createWithBuiltins();

    auto tomlResult = parseTOML("[plugins]\nnaming-convention = false\n");
    ASSERT_TRUE(tomlResult.success);
    reg.configureFromTOML(tomlResult.doc);

    auto *naming = reg.getPlugin("naming-convention");
    ASSERT_NE(naming, nullptr);
    EXPECT_FALSE(naming->isEnabled());

    auto *unused = reg.getPlugin("unused-function");
    ASSERT_NE(unused, nullptr);
    EXPECT_TRUE(unused->isEnabled());
}

TEST_F(PluginTest, ConfigureFromTOML_EnablePlugin) {
    auto reg = PluginRegistry::createWithBuiltins();
    reg.getPlugin("unused-function")->setEnabled(false);

    auto tomlResult = parseTOML("[plugins]\nunused-function = true\n");
    ASSERT_TRUE(tomlResult.success);
    reg.configureFromTOML(tomlResult.doc);

    EXPECT_TRUE(reg.getPlugin("unused-function")->isEnabled());
}

TEST_F(PluginTest, ConfigureFromTOML_NoPluginsSection) {
    auto reg = PluginRegistry::createWithBuiltins();

    auto tomlResult = parseTOML("[package]\nname = \"test\"\n");
    ASSERT_TRUE(tomlResult.success);
    reg.configureFromTOML(tomlResult.doc);

    EXPECT_TRUE(reg.getPlugin("naming-convention")->isEnabled());
    EXPECT_TRUE(reg.getPlugin("unused-function")->isEnabled());
}

// ==================== NamingConventionPlugin Tests ====================

TEST_F(PluginTest, NamingConvention_PascalCaseStruct_NoWarning) {
    auto reg = PluginRegistry::createWithBuiltins();
    reg.getPlugin("unused-function")->setEnabled(false);

    auto result = runWithPlugins(R"(
        struct MyStruct {
            var x: i32
        }
        func main() {
            let s = MyStruct { x: 1 }
            println(s.x)
        }
    )", reg);

    EXPECT_TRUE(result.parseOk);
    EXPECT_FALSE(hasDiag(result, DiagID::warn_lint_type_naming));
}

TEST_F(PluginTest, NamingConvention_LowerCaseStruct_Warning) {
    auto reg = PluginRegistry::createWithBuiltins();
    reg.getPlugin("unused-function")->setEnabled(false);

    auto result = runWithPlugins(R"(
        struct my_struct {
            var x: i32
        }
        func main() {
            println(42)
        }
    )", reg);

    EXPECT_TRUE(result.parseOk);
    EXPECT_TRUE(hasDiag(result, DiagID::warn_lint_type_naming));
}

TEST_F(PluginTest, NamingConvention_UpperCaseFunc_Warning) {
    auto reg = PluginRegistry::createWithBuiltins();
    reg.getPlugin("unused-function")->setEnabled(false);

    auto result = runWithPlugins(R"(
        func BadFunc() -> i32 {
            return 42
        }
        func main() {
            let x = BadFunc()
            println(x)
        }
    )", reg);

    EXPECT_TRUE(result.parseOk);
    EXPECT_TRUE(hasDiag(result, DiagID::warn_plugin_naming_func));
    EXPECT_TRUE(diagContains(result, DiagID::warn_plugin_naming_func, "BadFunc"));
}

TEST_F(PluginTest, NamingConvention_MainExempt) {
    auto reg = PluginRegistry::createWithBuiltins();
    reg.getPlugin("unused-function")->setEnabled(false);

    auto result = runWithPlugins(R"(
        func main() {
            println(42)
        }
    )", reg);

    EXPECT_TRUE(result.parseOk);
    EXPECT_FALSE(hasDiag(result, DiagID::warn_plugin_naming_func));
}

// ==================== UnusedFunctionPlugin Tests ====================

TEST_F(PluginTest, UnusedFunction_CalledFunc_NoWarning) {
    auto reg = PluginRegistry::createWithBuiltins();
    reg.getPlugin("naming-convention")->setEnabled(false);

    auto result = runWithPlugins(R"(
        func helper() -> i32 {
            return 42
        }
        func main() {
            let x = helper()
            println(x)
        }
    )", reg);

    EXPECT_TRUE(result.semaOk);
    EXPECT_FALSE(hasDiag(result, DiagID::warn_plugin_unused_function));
}

TEST_F(PluginTest, UnusedFunction_UncalledFunc_Warning) {
    auto reg = PluginRegistry::createWithBuiltins();
    reg.getPlugin("naming-convention")->setEnabled(false);

    auto result = runWithPlugins(R"(
        func unused() -> i32 {
            return 42
        }
        func main() {
            println(1)
        }
    )", reg);

    EXPECT_TRUE(result.semaOk);
    EXPECT_TRUE(hasDiag(result, DiagID::warn_plugin_unused_function));
    EXPECT_TRUE(diagContains(result, DiagID::warn_plugin_unused_function, "unused"));
}

TEST_F(PluginTest, UnusedFunction_MainExempt) {
    auto reg = PluginRegistry::createWithBuiltins();
    reg.getPlugin("naming-convention")->setEnabled(false);

    auto result = runWithPlugins(R"(
        func main() {
            println(1)
        }
    )", reg);

    EXPECT_TRUE(result.semaOk);
    EXPECT_FALSE(hasDiag(result, DiagID::warn_plugin_unused_function));
}

TEST_F(PluginTest, UnusedFunction_ExternExempt) {
    auto reg = PluginRegistry::createWithBuiltins();
    reg.getPlugin("naming-convention")->setEnabled(false);

    auto result = runWithPlugins(R"(
        extern "C" func externalFunc() -> i32
        func main() {
            println(1)
        }
    )", reg);

    EXPECT_TRUE(result.semaOk);
    EXPECT_FALSE(diagContains(result, DiagID::warn_plugin_unused_function, "externalFunc"));
}

// ==================== Integration Tests ====================

TEST_F(PluginTest, MultiplePlugins_BothActive) {
    auto reg = PluginRegistry::createWithBuiltins();

    auto result = runWithPlugins(R"(
        struct bad_name {
            var x: i32
        }
        func unused() -> i32 {
            return 42
        }
        func main() {
            println(42)
        }
    )", reg);

    EXPECT_TRUE(result.parseOk);
    EXPECT_TRUE(hasDiag(result, DiagID::warn_lint_type_naming));
    EXPECT_TRUE(hasDiag(result, DiagID::warn_plugin_unused_function));
}

TEST_F(PluginTest, NoPlugins_NoWarnings) {
    PluginRegistry reg; // empty, no plugins

    auto result = runWithPlugins(R"(
        struct bad_name {
            var x: i32
        }
        func unused() -> i32 {
            return 42
        }
        func main() {
            println(42)
        }
    )", reg);

    EXPECT_TRUE(result.parseOk);
    EXPECT_FALSE(hasDiag(result, DiagID::warn_lint_type_naming));
    EXPECT_FALSE(hasDiag(result, DiagID::warn_plugin_unused_function));
}

TEST_F(PluginTest, PluginReturnsTrue_PipelineContinues) {
    auto reg = PluginRegistry::createWithBuiltins();

    auto result = runWithPlugins(R"(
        struct MyStruct {
            var x: i32
        }
        func helper() -> i32 {
            return 42
        }
        func main() {
            let s = MyStruct { x: helper() }
            println(s.x)
        }
    )", reg);

    EXPECT_TRUE(result.parseOk);
    EXPECT_TRUE(result.semaOk);
    EXPECT_FALSE(hasDiag(result, DiagID::warn_lint_type_naming));
    EXPECT_FALSE(hasDiag(result, DiagID::warn_plugin_naming_func));
    EXPECT_FALSE(hasDiag(result, DiagID::warn_plugin_unused_function));
}

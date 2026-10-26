#include <cstddef>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "../com/json_helper.hpp"
#include "../com/wasm_helper.hpp"

TEST(WasmCompilerLearnTest, Chapter03Test) { // NOLINT(cert-err58-cpp)
    const Commands cmd = ReadJson("./arithm.json");
    ASSERT_EQ(cmd.ls.size(), 2U);
    for (const Module &module : cmd.ls) {
        ASSERT_EQ(module.fun.size(), 24U);
    }
    PrintCommands(cmd);

    std::vector<CompiledModule> compiled;
    for (const Module &module : cmd.ls) {
        compiled.push_back(ParseWasm(module.file_name));
    }

    bool res = true;
    for (size_t i = 0; i < cmd.ls.size(); ++i) {
        std::cout << "JIT map " << compiled[i].file_name << " functions " << compiled[i].functions.size() << '\n';
        res &= RunAssert(cmd.ls[i], compiled[i]);
    }
    EXPECT_TRUE(res);
}

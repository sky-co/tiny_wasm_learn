#include <cstddef>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "../com/json_helper.hpp"
#include "../com/wasm_helper.hpp"

TEST(WasmCompilerLearnTest, Chapter02Test) {
    const Commands cmd = ReadJson("./local.json");
    ASSERT_FALSE(cmd.ls.empty()) << "No Wasm modules were loaded from local.json";
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

#include <cstddef>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "../com/json_helper.hpp"
#include "../com/wasm_helper.hpp"

TEST(WasmCompilerLearnTest, ParseUnsignedInteger) {
    EXPECT_EQ(StringToUnsignedLongLong("0"), 0ULL);
    EXPECT_EQ(StringToUnsignedLongLong("00010"), 10ULL);
    EXPECT_EQ(StringToUnsignedLongLong("4294967295"), 0xFFFFFFFFULL);
    EXPECT_EQ(StringToUnsignedLongLong("18446744073709551615"), 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_EQ(StringToUnsignedLongLong("0xffffffff"), 0xFFFFFFFFULL);
    EXPECT_EQ(StringToUnsignedLongLong("0XFFFFFFFFFFFFFFFF"), 0xFFFFFFFFFFFFFFFFULL);
    EXPECT_THROW(StringToUnsignedLongLong("18446744073709551616"), std::out_of_range);
    EXPECT_THROW(StringToUnsignedLongLong("0x10000000000000000"), std::out_of_range);
    EXPECT_THROW(StringToUnsignedLongLong(""), std::invalid_argument);
    EXPECT_THROW(StringToUnsignedLongLong("0x"), std::invalid_argument);
    EXPECT_THROW(StringToUnsignedLongLong("-1"), std::invalid_argument);
    EXPECT_THROW(StringToUnsignedLongLong("12abc"), std::invalid_argument);
}

TEST(WasmCompilerLearnTest, ReadLargeArgument) {
    const Argument i32_argument = ReadArgument(json{{"type", "i32"}, {"value", "4294967295"}});
    EXPECT_EQ(i32_argument.type, "i32");
    EXPECT_EQ(i32_argument.value, 0xFFFFFFFFULL);
    const Argument i64_argument = ReadArgument(json{{"type", "i64"}, {"value", "18446744073709551615"}});
    EXPECT_EQ(i64_argument.type, "i64");
    EXPECT_EQ(i64_argument.value, 0xFFFFFFFFFFFFFFFFULL);
}

TEST(WasmCompilerLearnTest, Chapter02Test) {
    const char *const json_env = std::getenv("WASM_TEST_JSON");
    const char *const json_path = json_env != nullptr ? json_env : "./local.json";
    const Commands cmd = ReadJson(json_path);
    ASSERT_FALSE(cmd.ls.empty()) << "No Wasm modules were loaded from " << json_path;
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

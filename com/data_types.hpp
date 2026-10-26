#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "executable_buffer.hpp"

struct Argument {
    std::string type;
    unsigned long long value{}; // NOLINT(google-runtime-int)
};
struct Action {
    std::string type;
    std::string field;
    std::vector<Argument> args;
};

struct Expected {
    std::vector<Argument> args;
};
struct Function {
    std::string type;
    Action act;
    Expected exp;
};
struct Module {
    std::string file_name;
    std::vector<Function> fun;
};

struct Commands {
    std::vector<Module> ls;
};

enum class WasmValueType : uint8_t {
    I32 = 0x7FU,
    I64 = 0x7EU,
};

struct FuncType {
    std::vector<WasmValueType> params;
    std::vector<WasmValueType> results;
};

struct ExpFunc {
    std::string name;
    uint32_t kind = 0;
    uint32_t idx = 0;
};

struct Instruction {
    uint8_t opcode = 0;
    uint32_t index = 0;
    int64_t immediate = 0;
};

struct CompiledFunctions {
    std::string name;
    uint32_t decl_cnt = 0;
    std::vector<WasmValueType> params;
    std::vector<WasmValueType> results;
    std::vector<Instruction> body;
};

// The machine-code ABI is uniform: arguments and locals occupy 64-bit slots.
// JitFunc retains the Wasm signature so i32 and i64 calls are marshalled correctly.
using JitEntry = uint64_t (*)(uint64_t *slots);

struct JitFunc {
    std::vector<WasmValueType> params;
    std::vector<WasmValueType> results;
    uint32_t slot_count = 0;
    JitEntry entry = nullptr;
};

struct CompiledModule {
    std::string file_name;
    std::vector<CompiledFunctions> functions;
    std::vector<JitFunc> entries;
    std::unordered_map<std::string, uint32_t> exports;
    ExecutableBuffer code;
};

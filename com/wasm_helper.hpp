#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "byte_reader.hpp"
#include "data_types.hpp"

inline void PrintCommands(const Commands &cmd) {
    for (const Module &module : cmd.ls) {
        std::cout << "module " << module.file_name << " asserts " << module.fun.size() << '\n';
        for (const Function &function : module.fun) {
            std::cout << "  " << function.act.field << " args";
            for (const Argument &arg : function.act.args) {
                std::cout << ' ' << arg.type << ':' << arg.value;
            }
            std::cout << " expected";
            for (const Argument &expected : function.exp.args) {
                std::cout << ' ' << expected.type << ':' << expected.value;
            }
            std::cout << '\n';
        }
    }
}

inline void AppendByte(std::vector<uint8_t> &out, uint8_t value) {
    out.push_back(value);
}

inline void AppendWord(std::vector<uint8_t> &out, uint32_t word) {
    AppendByte(out, static_cast<uint8_t>(word & 0xFFU));
    AppendByte(out, static_cast<uint8_t>((word >> 8U) & 0xFFU));
    AppendByte(out, static_cast<uint8_t>((word >> 16U) & 0xFFU));
    AppendByte(out, static_cast<uint8_t>((word >> 24U) & 0xFFU));
}

inline void EmitLoadLocal(std::vector<uint8_t> &out, uint32_t index) {
    AppendWord(out, 0xF9400001U | (index << 10U));
}

inline void EmitStoreLocal(std::vector<uint8_t> &out, uint32_t index) {
    AppendWord(out, 0xF9000001U | (index << 10U));
}

inline void EmitMovX1Imm(std::vector<uint8_t> &out, uint64_t imm) {
    bool first = true;
    for (uint32_t hw = 0; hw < 4U; ++hw) {
        // Get the low 16bits
        const uint32_t chunk = static_cast<uint32_t>(imm & 0xFFFFU);
        imm >>= 16U; // Get next 16 bits in high part
        if (chunk == 0U && !first) {
            continue;
        }
        const uint32_t base = first ? 0xD2800001U : 0xF2800001U;
        AppendWord(out, base | (hw << 21U) | (chunk << 5U));
        first = false;
        if (imm == 0U) {
            break;
        }
    }
}

inline void PushStack(std::vector<uint8_t> &out) {
    AppendWord(out, 0xD10043FFU);
    AppendWord(out, 0xF90003E1U);
}

inline void LoadStack(std::vector<uint8_t> &out) {
    AppendWord(out, 0xF94003E2U);
    AppendWord(out, 0x910043FFU);
    AppendWord(out, 0xF94003E1U);
}

inline void EmitAdd(std::vector<uint8_t> &out, uint8_t op) {
    LoadStack(out);

    if (op == 0x6AU) {
        // add w1, w1, w2
        AppendWord(out, 0x0B020021U);
    } else if (op == 0x7CU) {
        // add x1, x1, x2
        AppendWord(out, 0x8B020021U);
    }

    // str x1, [sp]  用原 x 的栈槽保存结果
    AppendWord(out, 0xF90003E1U);
}

inline void EmitSub(std::vector<uint8_t> &out, uint8_t op) {
    LoadStack(out);

    if (op == 0x6BU) {
        // sub w1, w1, w2
        AppendWord(out, 0x4B020021U);
    } else if (op == 0x7DU) {
        // sub x1, x1, x2
        AppendWord(out, 0xCB020021U);
    }

    // str x1, [sp]  用原 x 的栈槽保存结果
    AppendWord(out, 0xF90003E1U);
}

inline void EmitMul(std::vector<uint8_t> &out, uint8_t op) {
    LoadStack(out);

    // mul w1, w1, w2
    if (op == 0x6CU) {
        // mul w1, w1, w2
        AppendWord(out, 0x1B027C21U);
    } else if (op == 0x7EU) {
        // mul x1, x1, x2
        AppendWord(out, 0x9B027C21U);
    }

    // str x1, [sp]  用原 x 的栈槽保存结果
    // 写入 w1 同时将 x1 高 32 位清零
    AppendWord(out, 0xF90003E1U);
}

inline std::vector<uint8_t> CompileInstructions(const CompiledFunctions &func) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < func.body.size(); ++i) {
        const Instruction &instr = func.body[i];
        switch (instr.opcode) {
        case 0x20U:
            EmitLoadLocal(out, instr.index);
            PushStack(out);
            break;
        case 0x21U:
            AppendWord(out, 0xF94003E1U);
            AppendWord(out, 0x910043FFU);
            EmitStoreLocal(out, instr.index);
            break;
        case 0x22U:
            AppendWord(out, 0xF94003E1U);
            EmitStoreLocal(out, instr.index);
            break;
        case 0x41U:
        case 0x42U:
            EmitMovX1Imm(out, static_cast<uint64_t>(instr.immediate));
            PushStack(out);
            break;
        case 0x6AU:
        case 0x7CU:
            EmitAdd(out, instr.opcode);
            break;
        case 0x6BU:
        case 0x7DU:
            EmitSub(out, instr.opcode);
            break;
        case 0x6CU:
        case 0x7EU:
            EmitMul(out, instr.opcode);
            break;
        case 0x0BU:
            if (!func.results.empty()) {
                AppendWord(out, 0xF94003E0U);
                AppendWord(out, 0x910043FFU);
            } else {
                AppendWord(out, 0xAA1F03E0U);
            }
            AppendWord(out, 0xD65F03C0U);
            break;
        default:
            std::cout << "cannot compile opcode " << static_cast<unsigned>(instr.opcode) << '\n';
            return {};
        }
    }

    if (out.size() < 4U || out[out.size() - 4U] != 0xC0U) {
        AppendWord(out, 0xAA1F03E0U);
        AppendWord(out, 0xD65F03C0U);
    }
    return out;
}

inline void BindJITTable(const std::vector<ExpFunc> &exp, const std::vector<CompiledFunctions> &com, CompiledModule &module) {
    module.functions = com;
    for (const ExpFunc &item : exp) {
        if (item.kind == 0U && item.idx < module.functions.size()) {
            module.functions[item.idx].name = item.name;
        }
    }

    // Compile each Code-section body, then place that machine code at the JIT
    // entry selected by the same func_index the Export section stored.
    std::vector<std::vector<uint8_t>> machine(module.functions.size());
    size_t bytes = 16U;
    for (size_t i = 0; i < module.functions.size(); ++i) {
        machine[i] = CompileInstructions(module.functions[i]);
        // 向上对齐到 16 字节
        bytes += (machine[i].size() + 15U) & ~15U;
    }
    if (!module.code.Allocate(bytes)) {
        std::cout << "failed to allocate JIT memory for " << module.file_name << '\n';
        return;
    }

    module.entries.resize(machine.size());
    for (size_t i = 0; i < machine.size(); ++i) {
        uint8_t *const entry = module.code.Emit(machine[i]);
        if (entry == nullptr) {
            std::cout << "failed to emit JIT entry for function " << i << '\n';
            continue;
        }
        module.entries[i].params = module.functions[i].params;
        module.entries[i].results = module.functions[i].results;
        module.entries[i].slot_count = static_cast<uint32_t>(module.functions[i].params.size()) + module.functions[i].decl_cnt;
        module.entries[i].entry = reinterpret_cast<JitEntry>(entry); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    }
    for (const ExpFunc &item : exp) {
        if (item.kind == 0U && item.idx < module.entries.size()) {
            module.exports[item.name] = item.idx;
        }
    }
    if (!module.code.ProtectExecutable()) {
        std::cout << "failed to mark JIT memory executable for " << module.file_name << '\n';
    }
}

inline CompiledModule ParseWasm(const std::string &file_path) {
    CompiledModule module;
    module.file_name = file_path;
    if (file_path.empty()) {
        return module;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "failed to open " << file_path << '\n';
        return module;
    }

    // Parse wasm file
    std::cout << "=========================== Parsing WASM ===========================" << '\n';
    std::cout << "Open " << file_path << "..." << '\n';
    const std::vector<uint8_t> stream{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    ByteReader br(stream);
    br.ParseHeader();
    br.ParseSections();

    // Bind to machine code
    BindJITTable(br.GetExportedFunctions(), br.GetCompiledFunctions(), module);

    return module;
}

inline bool RunAssert(const Module &module, const CompiledModule &compiled) {
    bool res = true;
    for (const Function &test : module.fun) {
        // map<function_name, index>
        const std::unordered_map<std::string, uint32_t>::const_iterator found = compiled.exports.find(test.act.field);
        if (found == compiled.exports.end() || found->second >= compiled.entries.size()) {
            std::cout << "FAIL missing " << test.act.field << '\n';
            res = false;
            continue;
        }
        const JitFunc &jit = compiled.entries[found->second];
        if (jit.entry == nullptr || test.act.args.size() != jit.params.size()) {
            std::cout << "FAIL invalid invocation " << test.act.field << '\n';
            res = false;
            continue;
        }
        const uint32_t slot_count = (jit.slot_count == 0U) ? 1U : jit.slot_count;
        std::vector<uint64_t> slots(slot_count, 0U);
        for (size_t i = 0; i < test.act.args.size() && i < slots.size(); ++i) {
            slots[i] = test.act.args[i].value;
        }
        const uint64_t result = jit.entry(slots.data());
        // VOID function
        if (test.exp.args.empty()) {
            std::cout << "PASS " << test.act.field << '\n';
            continue;
        }
        // Compare the result with expection
        const uint64_t expected = test.exp.args[0].value;
        if (result == expected) {
            std::cout << "PASS " << test.act.field << " => " << result << '\n';
        } else {
            std::cout << "FAIL " << test.act.field << " got " << result << " expected " << expected << '\n';
            res = false;
        }
    }
    return res;
}

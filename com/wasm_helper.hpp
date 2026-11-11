#include <csetjmp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

#include "byte_reader.hpp"
#include "data_types.hpp"
#include "json_helper.hpp"

enum class TrapCode : int {
    None = 0,
    IntegerDivideByZero = 1,
    IntegerOverflow = 2,
};

inline thread_local std::jmp_buf *g_wasm_trap_env = nullptr;

inline void RaiseTrap(TrapCode code) {
    if (g_wasm_trap_env != nullptr) {
        longjmp(*g_wasm_trap_env, static_cast<int>(code)); // NOLINT(cert-err52-cpp)
    }
    std::abort();
}

template <typename T> inline T div(T lhs, T rhs) {
    return (lhs / rhs);
}

extern "C" inline uint64_t WasmDivHelper(uint64_t op, uint64_t lhs, uint64_t rhs) {
    if (rhs == 0U) {
        RaiseTrap(TrapCode::IntegerDivideByZero);
    }

    const vb::OPCode div_kind = static_cast<vb::OPCode>(op);
    // Signed overflow case: "i32 INT_MIN / -1"
    if (div_kind == vb::OPCode::I32_DIV_S) {
        const uint32_t lhs32 = static_cast<uint32_t>(lhs);
        const uint32_t rhs32 = static_cast<uint32_t>(rhs);
        if (lhs32 == 0x80000000U && rhs32 == 0xFFFFFFFFU) {
            RaiseTrap(TrapCode::IntegerOverflow);
        }
        const int32_t result = div<int32_t>(static_cast<int32_t>(lhs), static_cast<int32_t>(rhs));
        return static_cast<uint64_t>(static_cast<uint32_t>(result));
    }

    if (div_kind == vb::OPCode::I32_DIV_U) {
        return div<uint32_t>(static_cast<uint32_t>(lhs), static_cast<uint32_t>(rhs));
    }

    if (div_kind == vb::OPCode::I64_DIV_S) {
        if (lhs == 0x8000000000000000ULL && rhs == 0xFFFFFFFFFFFFFFFFULL) {
            RaiseTrap(TrapCode::IntegerOverflow);
        }
        return div<int64_t>(static_cast<int64_t>(lhs), static_cast<int64_t>(rhs));
    }

    if (div_kind == vb::OPCode::I64_DIV_U) {
        return div<uint64_t>(lhs, rhs);
    }

    RaiseTrap(TrapCode::IntegerOverflow);
    UNREACHABLE(, "Unsupported opcode: " << static_cast<uint8_t>(div_kind));
}

inline int ExpectedTrapCode(const Function &test) {
    if (test.text == "integer divide by zero") {
        return static_cast<int>(TrapCode::IntegerDivideByZero);
    }
    if (test.text == "integer overflow") {
        return static_cast<int>(TrapCode::IntegerOverflow);
    }
    return static_cast<int>(TrapCode::None);
}

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

inline bool IsInputOverflow(const std::string &type, uint64_t value) {
    if (type == "i32") {
        if (value > 0xFFFFFFFFU) {
            return true;
        }
    } else if (type == "i64") {
        if (value > 0xFFFFFFFFFFFFFFFFU) {
            return true;
        }
    } else {
        // Do nothing
    }

    return false;
}

inline bool IsArgumentOverflow(const Function &test) {
    const Action &act = test.act;
    for (const Argument &arg : act.args) {
        if (IsInputOverflow(arg.type, arg.value)) {
            return true;
        }
    }
    return false;
}

inline bool IsDivideByZero(const Function &test) {
    const Action &act = test.act;
    if (act.field == "div_s" || act.field == "div_u") {
        if (act.args.size() > 1 && act.args[1].value == 0) {
            return true;
        }
    }
    return false;
}
inline bool IsInputValid(const Function &test) {
    const bool res = IsArgumentOverflow(test) || IsDivideByZero(test);
    return !res;
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

inline void EmitMovXImm(std::vector<uint8_t> &out, uint32_t reg, uint64_t imm) {
    bool first = true;
    for (uint32_t hw = 0; hw < 4U; ++hw) {
        const uint32_t chunk = static_cast<uint32_t>(imm & 0xFFFFU);
        if (chunk == 0U && !first) {
            imm >>= 16U;
            continue;
        }
        const uint32_t base = first ? 0xD2800000U : 0xF2800000U;
        AppendWord(out, base | (hw << 21U) | (chunk << 5U) | reg);

        imm >>= 16U; // Get next 16 bits in high part
        first = false;
        if (imm == 0U) {
            break;
        }
    }
}

inline void EmitMovX1Imm(std::vector<uint8_t> &out, uint64_t imm) {
    EmitMovXImm(out, 1U, imm);
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

inline void EmitAdd(std::vector<uint8_t> &out, vb::OPCode op) {
    LoadStack(out);

    if (op == vb::OPCode::I32_ADD) {
        // add w1, w1, w2
        AppendWord(out, 0x0B020021U);
    } else if (op == vb::OPCode::I64_ADD) {
        // add x1, x1, x2
        AppendWord(out, 0x8B020021U);
    }

    // str x1, [sp]  用原 x 的栈槽保存结果
    AppendWord(out, 0xF90003E1U);
}

inline void EmitSub(std::vector<uint8_t> &out, vb::OPCode op) {
    LoadStack(out);

    if (op == vb::OPCode::I32_SUB) {
        // sub w1, w1, w2
        AppendWord(out, 0x4B020021U);
    } else if (op == vb::OPCode::I64_SUB) {
        // sub x1, x1, x2
        AppendWord(out, 0xCB020021U);
    }

    // str x1, [sp]  用原 x 的栈槽保存结果
    AppendWord(out, 0xF90003E1U);
}

inline void EmitMul(std::vector<uint8_t> &out, vb::OPCode op) {
    LoadStack(out);

    // mul w1, w1, w2
    if (op == vb::OPCode::I32_MUL) {
        // mul w1, w1, w2
        AppendWord(out, 0x1B027C21U);
    } else if (op == vb::OPCode::I64_MUL) {
        // mul x1, x1, x2
        AppendWord(out, 0x9B027C21U);
    }

    // str x1, [sp]  用原 x 的栈槽保存结果
    // 写入 w1 同时将 x1 高 32 位清零
    AppendWord(out, 0xF90003E1U);
}

inline void EmitDiv(std::vector<uint8_t> &out, vb::OPCode op) {
    const uint64_t div_kind = static_cast<uint64_t>(op);
    // Load the division kind into x0
    EmitMovXImm(out, 0U, div_kind);

    // Load the operands to x1 and x2 from the stack.
    LoadStack(out);

    // Load the address of the WasmDivHelper function into x16
    EmitMovXImm(out, 16U, reinterpret_cast<uint64_t>(&WasmDivHelper)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

    // Save LR because blr overwrites x30 and this JIT function still needs it on final ret.
    AppendWord(out, 0xD10043FFU);
    AppendWord(out, 0xF90003FEU);

    // blr x16, enter helper function for division
    AppendWord(out, 0xD63F0200U);

    // Restore LR after returning from the helper function
    AppendWord(out, 0xF94003FEU);
    AppendWord(out, 0x910043FFU);

    // mov x1, x0
    AppendWord(out, 0xAA0003E1U);

    // str x1, [sp]  用原 x 的栈槽保存结果
    AppendWord(out, 0xF90003E1U);
}

inline void EmitI32LtS(std::vector<uint8_t> &out) {
    LoadStack(out);
    // cmp w1, w2
    AppendWord(out, 0x6B02003FU);
    // cset w1, lt
    AppendWord(out, 0x1A9FA7E1U);
    // str x1, [sp]  用原 x 的栈槽保存结果
    AppendWord(out, 0xF90003E1U);
}

inline size_t EmitIF(std::vector<uint8_t> &out) {
    AppendWord(out, 0xB94003E1U);
    AppendWord(out, 0x910043FFU);
    const size_t branch_pos = out.size();
    // cbz w1, <else_or_end_if>; imm19 is patched after the else/end target is known.
    AppendWord(out, 0x34000001U);
    return branch_pos;
}

inline bool PatchCBZW1(std::vector<uint8_t> &out, size_t branch_pos, size_t target_pos) {
    if (branch_pos + 4U > out.size()) {
        return false;
    }
    const int64_t delta = static_cast<int64_t>(target_pos) - static_cast<int64_t>(branch_pos);
    if ((delta % 4) != 0) {
        return false;
    }
    const int64_t imm19_signed = delta / 4;
    if (imm19_signed < -262144LL || imm19_signed > 262143LL) {
        return false;
    }

    const uint32_t imm19 = static_cast<uint32_t>(imm19_signed) & 0x7FFFFU;
    const uint32_t word = 0x34000001U | (imm19 << 5U);
    for (size_t byte_index = 0; byte_index < 4U; ++byte_index) {
        out[branch_pos + byte_index] = static_cast<uint8_t>(word >> (byte_index * 8U));
    }
    return true;
}

inline bool PatchB(std::vector<uint8_t> &out, size_t branch_pos, size_t target_pos) {
    if (branch_pos + 4U > out.size()) {
        return false;
    }
    const int64_t delta = static_cast<int64_t>(target_pos) - static_cast<int64_t>(branch_pos);
    if ((delta % 4) != 0) {
        return false;
    }
    const int64_t imm26_signed = delta / 4;
    if (imm26_signed < -33554432LL || imm26_signed > 33554431LL) {
        return false;
    }

    const uint32_t imm26 = static_cast<uint32_t>(imm26_signed) & 0x3FFFFFFU;
    const uint32_t word = 0x14000000U | imm26;
    for (size_t byte_index = 0; byte_index < 4U; ++byte_index) {
        out[branch_pos + byte_index] = static_cast<uint8_t>(word >> (byte_index * 8U));
    }
    return true;
}

inline size_t EmitELSE(std::vector<uint8_t> &out, size_t branch_pos) {
    const size_t jump_to_end_pos = out.size();
    // b <end_if>; imm26 is patched at END when the end_if address is known.
    AppendWord(out, 0x14000000U);
    if (!PatchCBZW1(out, branch_pos, out.size())) {
        return static_cast<size_t>(-1);
    }
    return jump_to_end_pos;
}

inline std::vector<uint8_t> CompileInstructions(const CompiledFunctions &func) {
    struct IfContext {
        size_t branch_to_else_or_end = 0U;
        size_t branch_to_end = static_cast<size_t>(-1);
    };

    std::vector<uint8_t> out;
    std::vector<IfContext> if_stack;
    bool reached_function_end = false;
    for (size_t i = 0; i < func.body.size(); ++i) {
        const Instruction &instr = func.body[i];
        switch (instr.opcode) {
        case vb::OPCode::LOCAL_GET:
            EmitLoadLocal(out, instr.index);
            PushStack(out);
            break;
        case vb::OPCode::LOCAL_SET:
            AppendWord(out, 0xF94003E1U);
            AppendWord(out, 0x910043FFU);
            EmitStoreLocal(out, instr.index);
            break;
        case vb::OPCode::LOCAL_TEE:
            AppendWord(out, 0xF94003E1U);
            EmitStoreLocal(out, instr.index);
            break;
        case vb::OPCode::I32_CONST:
        case vb::OPCode::I64_CONST:
            EmitMovX1Imm(out, static_cast<uint64_t>(instr.immediate));
            PushStack(out);
            break;
        case vb::OPCode::I32_ADD:
        case vb::OPCode::I64_ADD:
            EmitAdd(out, instr.opcode);
            break;
        case vb::OPCode::I32_SUB:
        case vb::OPCode::I64_SUB:
            EmitSub(out, instr.opcode);
            break;
        case vb::OPCode::I32_MUL:
        case vb::OPCode::I64_MUL:
            EmitMul(out, instr.opcode);
            break;
        case vb::OPCode::I32_DIV_S:
        case vb::OPCode::I32_DIV_U:
        case vb::OPCode::I64_DIV_S:
        case vb::OPCode::I64_DIV_U:
            EmitDiv(out, instr.opcode);
            break;
        case vb::OPCode::I32_LT_S:
            EmitI32LtS(out);
            break;
        case vb::OPCode::IF:
            if_stack.push_back(IfContext{EmitIF(out), static_cast<size_t>(-1)});
            break;
        case vb::OPCode::ELSE:
            if (if_stack.empty()) {
                std::cout << "cannot compile ELSE without matching IF" << '\n';
                return {};
            }
            if_stack.back().branch_to_end = EmitELSE(out, if_stack.back().branch_to_else_or_end);
            if (if_stack.back().branch_to_end == static_cast<size_t>(-1)) {
                std::cout << "failed to patch IF branch to ELSE" << '\n';
                return {};
            }
            break;
        case vb::OPCode::END:
            if (!if_stack.empty()) {
                const IfContext context = if_stack.back();
                if_stack.pop_back();
                if (context.branch_to_end == static_cast<size_t>(-1)) {
                    if (!PatchCBZW1(out, context.branch_to_else_or_end, out.size())) {
                        std::cout << "failed to patch IF branch to END" << '\n';
                        return {};
                    }
                } else if (!PatchB(out, context.branch_to_end, out.size())) {
                    std::cout << "failed to patch ELSE branch to END" << '\n';
                    return {};
                } else {
                }
                break;
            }

            if (!func.results.empty()) {
                AppendWord(out, 0xF94003E0U);
                AppendWord(out, 0x910043FFU);
            } else {
                AppendWord(out, 0xAA1F03E0U);
            }
            AppendWord(out, 0xD65F03C0U);
            reached_function_end = true;
            break;
        case vb::OPCode::NOP:
            // NOP does nothing, just continue
            break;
        case vb::OPCode::RETURN:
            // RETURN does nothing, just continue
            break;
        default:
            std::cout << "cannot compile opcode " << static_cast<unsigned>(instr.opcode) << '\n';
            return {};
        }
    }

    if (!if_stack.empty()) {
        std::cout << "cannot compile IF without matching END" << '\n';
        return {};
    }

    if (!reached_function_end) {
        std::cout << "cannot compile function without END" << '\n';
        return {};
    }

    // 如果生成的机器码末尾没有返回指令，就补上“返回 0”的结尾
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
        if (machine[i].empty()) {
            std::cout << "failed to compile function " << i << '\n';
            continue;
        }
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

        std::jmp_buf env;
        g_wasm_trap_env = &env;
        const int trap_code = setjmp(env); // NOLINT(cert-err52-cpp)
        if (trap_code == 0) {
            const uint64_t result = jit.entry(slots.data());
            const uint64_t expected = test.exp.args[0].value;
            if (result == expected) {
                std::cout << "PASS: " << test.act.field << " => " << result << '\n';
            } else {
                std::cout << "FAIL: " << test.act.field << " got " << result << " expected " << expected << '\n';
                res = false;
            }
        } else {
            const int expected_code = ExpectedTrapCode(test);
            if (expected_code != static_cast<int>(TrapCode::None) && trap_code != expected_code) {
                std::cout << "FAIL: " << test.act.field << " trap code " << trap_code << " expected " << expected_code << " (" << test.text << ")\n";
                res = false;
            } else {
                std::cout << "PASS: " << test.act.field << " trapped " << test.text << '\n';
            }
        }
        g_wasm_trap_env = nullptr;
    }
    return res;
}

inline bool RunWasmCommands(const Commands &cmd) {
    if (cmd.ls.empty()) {
        std::cout << "No Wasm modules were loaded" << '\n';
        return false;
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

    return res;
}

inline int RunWasmJson(const std::string &json_path) {
    if (json_path.empty()) {
        std::cout << "json path is empty" << '\n';
        return 1;
    }
    const Commands cmd = ReadJson(json_path);
    return RunWasmCommands(cmd) ? 0 : 1;
}

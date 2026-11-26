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
inline thread_local const CompiledModule *g_active_module = nullptr;

inline void RaiseTrap(TrapCode code) {
    if (g_wasm_trap_env != nullptr) {
        longjmp(*g_wasm_trap_env, static_cast<int>(code)); // NOLINT(cert-err52-cpp)
    }
    std::abort();
}

template <typename T> inline T div(T lhs, T rhs) {
    return (lhs / rhs);
}

inline uint64_t WasmDivHelper(uint64_t op, uint64_t lhs, uint64_t rhs) {
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

inline uint64_t WasmCallHelper(uint64_t func_index, const uint64_t *stack_top, uint64_t arg_count) {
    if (g_active_module == nullptr || func_index >= g_active_module->entries.size()) {
        std::cout << "invalid call target index " << func_index << '\n';
        std::abort();
    }
    const JitFunc &callee = g_active_module->entries[func_index];
    if (callee.entry == nullptr) {
        std::cout << "call target has no jit entry " << func_index << '\n';
        std::abort();
    }
    if (arg_count != callee.params.size()) {
        std::cout << "call arity mismatch target " << func_index << " expected " << callee.params.size() << " got " << arg_count << '\n';
        std::abort();
    }

    const uint32_t slot_count = (callee.slot_count == 0U) ? 1U : callee.slot_count;
    std::vector<uint64_t> slots(slot_count, 0U);
    const uint8_t *const stack_bytes = reinterpret_cast<const uint8_t *>(stack_top); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    for (size_t i = 0; i < arg_count; ++i) {
        const size_t reversed = static_cast<size_t>(arg_count - 1U - i);
        const uint64_t *const arg_ptr =
            reinterpret_cast<const uint64_t *>(stack_bytes + (reversed * 16U)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        slots[i] = *arg_ptr;
    }
    return callee.entry(slots.data());
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
    AppendWord(out, 0xF9400001U | (index << 10U)); // ldr  x1, [x0， #index * 8]
}

inline void EmitStoreLocal(std::vector<uint8_t> &out, uint32_t index) {
    AppendWord(out, 0xF9000001U | (index << 10U)); // str  x1, [x0]
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

inline void EmitMovX1Imm(std::vector<uint8_t> &out, uint64_t imm) {
    EmitMovXImm(out, 1U, imm);
}

inline void PushStack(std::vector<uint8_t> &out) {
    AppendWord(out, 0xD10043FFU); // sub  sp, sp, #0x10
    AppendWord(out, 0xF90003E1U); // str  x1, [sp]
}

inline void EmitLocalGet(std::vector<uint8_t> &out, uint32_t index) {
    EmitLoadLocal(out, index);
    PushStack(out);
}

inline void EmitLocalSet(std::vector<uint8_t> &out, uint32_t index) {
    AppendWord(out, 0xF94003E1U);
    AppendWord(out, 0x910043FFU);
    EmitStoreLocal(out, index);
}

inline void EmitLocalTee(std::vector<uint8_t> &out, uint32_t index) {
    AppendWord(out, 0xF94003E1U);
    EmitStoreLocal(out, index);
}

inline void LoadStack(std::vector<uint8_t> &out) {
    AppendWord(out, 0xF94003E2U); // ldr x2, [sp]
    AppendWord(out, 0x910043FFU); // add sp, sp, #16 -> pop stack
    AppendWord(out, 0xF94003E1U); // ldr x1, [sp]
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

inline void CallHelperFun(std::vector<uint8_t> &out, uint64_t fun_addr) {
    // Load the address of the helper function into x16
    EmitMovXImm(out, 16U, fun_addr);

    // Save LR because blr overwrites x30 and this JIT function still needs it on final ret.
    AppendWord(out, 0xD10043FFU);
    AppendWord(out, 0xF90003FEU);

    // blr x16, enter helper function for division
    AppendWord(out, 0xD63F0200U);

    // Restore LR after returning from the helper function
    AppendWord(out, 0xF94003FEU);
    AppendWord(out, 0x910043FFU);
}

inline void EmitDiv(std::vector<uint8_t> &out, vb::OPCode op) {
    const uint64_t div_kind = static_cast<uint64_t>(op);
    // Load the division kind into x0
    EmitMovXImm(out, 0U, div_kind);

    // Load the operands to x1 and x2 from the stack.
    LoadStack(out);

    CallHelperFun(out, reinterpret_cast<uint64_t>(&WasmDivHelper)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

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

inline void EmitI32GeS(std::vector<uint8_t> &out) {
    LoadStack(out);
    // cmp w1, w2
    AppendWord(out, 0x6B02003FU);
    // cset w1, ge
    AppendWord(out, 0x1A9FB7E1U);
    // str x1, [sp]  用原 x 的栈槽保存结果
    AppendWord(out, 0xF90003E1U);
}

inline void EmitLeU(std::vector<uint8_t> &out, vb::OPCode op) {
    LoadStack(out);

    if (op == vb::OPCode::I32_LE_U) {
        // cmp w1, w2
        AppendWord(out, 0x6B02003FU);
        // cset w1, le
        AppendWord(out, 0x1A9FD7E1U);
    } else {
        // cmp x1, x2
        AppendWord(out, 0xEB02003FU);
        // cset x1, le
        AppendWord(out, 0x9A9FD7E1U);
    }

    // str x1, [sp]  用原 x 的栈槽保存结果
    AppendWord(out, 0xF90003E1U);
}

inline void EmitCtz(std::vector<uint8_t> &out, vb::OPCode op) {
    // Unary operation: replace the top stack value without popping a slot.
    AppendWord(out, 0xF94003E1U);
    if (op == vb::OPCode::I32_CTZ) {
        // rbit w1, w1
        AppendWord(out, 0x5AC00021U);
        // clz w1, w1
        AppendWord(out, 0x5AC01021U);
    } else if (op == vb::OPCode::I64_CTZ) {
        // rbit x1, x1
        AppendWord(out, 0xDAC00021U);
        // clz x1, x1
        AppendWord(out, 0xDAC01021U);
    }
    // str x1, [sp]  用原 x 的栈槽保存结果
    AppendWord(out, 0xF90003E1U);
}

inline void EmitEqz(std::vector<uint8_t> &out, vb::OPCode op) {
    AppendWord(out, 0xF94003E1U); // ldr x1, [sp]
    if (op == vb::OPCode::I32_EQZ) {
        AppendWord(out, 0x7100003FU); // cmp w1, #0
    } else if (op == vb::OPCode::I64_EQZ) {
        AppendWord(out, 0xF100003FU); // cmp x1, #0
    }
    AppendWord(out, 0x1A9F17E1U); // cset w1, eq
    AppendWord(out, 0xF90003E1U); // str x1, [sp]
}

inline void EmitEq(std::vector<uint8_t> &out, vb::OPCode op) {
    LoadStack(out);
    if (op == vb::OPCode::I32_EQ) {
        AppendWord(out, 0x6B02003FU); // cmp w1, w2
    } else if (op == vb::OPCode::I64_EQ) {
        AppendWord(out, 0xEB02003FU); // cmp x1, x2
    }
    AppendWord(out, 0x1A9F17E1U); // cset w1, eq
    AppendWord(out, 0xF90003E1U); // str x1, [sp]
}

inline void EmitGtU(std::vector<uint8_t> &out, vb::OPCode op) {
    LoadStack(out);
    if (op == vb::OPCode::I32_GT_U) {
        AppendWord(out, 0x6B02003FU); // cmp w1, w2
        AppendWord(out, 0x1A9F17E1U); // cset w1, gt
    } else if (op == vb::OPCode::I64_GT_U) {
        AppendWord(out, 0xEB02003FU); // cmp x1, x2
        AppendWord(out, 0x1A9F97E1U); // cset w1, hi
    }

    AppendWord(out, 0xF90003E1U); // str x1, [sp]
}

inline size_t EmitLoop(std::vector<uint8_t> &out) {
    AppendWord(out, 0xB94003E1U); // ldr x1, [sp]
    AppendWord(out, 0x910043FFU); // mov xzr, x1
    const size_t branch_pos = out.size();
    AppendWord(out, 0x34000001U);
    return branch_pos;
}

inline size_t EmitIF(std::vector<uint8_t> &out) {
    AppendWord(out, 0xB94003E1U);
    AppendWord(out, 0x910043FFU);
    const size_t branch_pos = out.size();
    // cbz w1, <else_or_end_if>; imm19 is patched after the else/end target is known.
    AppendWord(out, 0x34000001U);
    return branch_pos;
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

inline void EmitStackCleanup(std::vector<uint8_t> &out, size_t slots) {
    while (slots != 0U) {
        const size_t chunk = slots > 255U ? 255U : slots;
        // add sp, sp, #(chunk * 16)
        AppendWord(out, 0x910003FFU | (static_cast<uint32_t>(chunk * 16U) << 10U));
        slots -= chunk;
    }
}

inline void EmitReturn(std::vector<uint8_t> &out, const CompiledFunctions &func, size_t stack_depth) {
    AppendWord(out, func.results.empty() ? 0xAA1F03E0U : 0xF94003E0U);
    EmitStackCleanup(out, stack_depth);
    AppendWord(out, 0xD65F03C0U);
}

inline bool EmitCall(std::vector<uint8_t> &out, const std::vector<CompiledFunctions> &functions, const Instruction &instr, size_t &stack_depth) {
    if (instr.index >= functions.size()) {
        std::cout << "invalid call function index " << instr.index << '\n';
        return false;
    }

    const CompiledFunctions &callee = functions[instr.index];
    const size_t arg_count = callee.params.size();
    const size_t result_count = callee.results.size();

    EmitMovXImm(out, 0U, instr.index);
    AppendWord(out, 0x910003E1U); // mov x1, sp
    EmitMovXImm(out, 2U, arg_count);

    CallHelperFun(out, reinterpret_cast<uint64_t>(&WasmCallHelper)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

    EmitStackCleanup(out, arg_count);
    stack_depth -= arg_count;
    if (result_count != 0U) {
        AppendWord(out, 0xAA0003E1U); // mov x1, x0
        PushStack(out);
        ++stack_depth;
    }
    return true;
}

inline std::vector<uint8_t> CompileInstructions(const CompiledFunctions &func, const std::vector<CompiledFunctions> &functions) {
    std::vector<uint8_t> out;
    std::vector<ControlContext> control_stack;
    size_t stack_depth = 0U;
    bool reached_function_end = false;
    for (const Instruction &instr : func.body) {
        switch (instr.opcode) {
        case vb::OPCode::LOCAL_GET:
            EmitLocalGet(out, instr.index);
            ++stack_depth;
            break;
        case vb::OPCode::LOCAL_SET:
            EmitLocalSet(out, instr.index);
            --stack_depth;
            break;
        case vb::OPCode::LOCAL_TEE:
            EmitLocalTee(out, instr.index);
            break;
        case vb::OPCode::I32_CONST:
        case vb::OPCode::I64_CONST:
            EmitMovX1Imm(out, static_cast<uint64_t>(instr.immediate));
            PushStack(out);
            ++stack_depth;
            break;
        case vb::OPCode::CALL:
            if (!EmitCall(out, functions, instr, stack_depth)) {
                return {};
            }
            break;
        case vb::OPCode::I32_ADD:
        case vb::OPCode::I64_ADD:
            EmitAdd(out, instr.opcode);
            --stack_depth;
            break;
        case vb::OPCode::I32_SUB:
        case vb::OPCode::I64_SUB:
            EmitSub(out, instr.opcode);
            --stack_depth;
            break;
        case vb::OPCode::I32_MUL:
        case vb::OPCode::I64_MUL:
            EmitMul(out, instr.opcode);
            --stack_depth;
            break;
        case vb::OPCode::I32_DIV_S:
        case vb::OPCode::I32_DIV_U:
        case vb::OPCode::I64_DIV_S:
        case vb::OPCode::I64_DIV_U:
            EmitDiv(out, instr.opcode);
            --stack_depth;
            break;
        case vb::OPCode::I32_LT_S:
            EmitI32LtS(out);
            --stack_depth;
            break;
        case vb::OPCode::I32_GE_S:
            EmitI32GeS(out);
            --stack_depth;
            break;
        case vb::OPCode::I32_LE_U:
        case vb::OPCode::I64_LE_U:
            EmitLeU(out, instr.opcode);
            --stack_depth;
            break;
        case vb::OPCode::I32_CTZ:
        case vb::OPCode::I64_CTZ:
            EmitCtz(out, instr.opcode);
            break;
        case vb::OPCode::I32_EQZ:
        case vb::OPCode::I64_EQZ:
            EmitEqz(out, instr.opcode);
            break;
        case vb::OPCode::I32_EQ:
            EmitEq(out, instr.opcode);
            --stack_depth;
            break;
        case vb::OPCode::I64_GT_U:
            EmitGtU(out, instr.opcode);
            --stack_depth;
            break;
        case vb::OPCode::IF:
        case vb::OPCode::BLOCK:
        case vb::OPCode::LOOP: {
            size_t branch_pos = 0U;
            if (instr.opcode == vb::OPCode::IF) {
                branch_pos = EmitIF(out);
                --stack_depth;
            }
            size_t loop_head_pos = static_cast<size_t>(-1);
            if (instr.opcode == vb::OPCode::LOOP) {
                loop_head_pos = out.size();
            }
            if (instr.immediate != 0x40 && instr.immediate != -64 && instr.immediate != 0x7F && instr.immediate != 0x7E) {
                std::cout << "unsupported control block type" << '\n';
                return {};
            }
            const size_t results = (instr.immediate == 0x40 || instr.immediate == -64) ? 0U : 1U;
            control_stack.push_back(ControlContext{instr.opcode, stack_depth, results, branch_pos, static_cast<size_t>(-1), loop_head_pos, {}});
            break;
        }
        case vb::OPCode::ELSE:
            if (control_stack.empty() || control_stack.back().opcode != vb::OPCode::IF) {
                std::cout << "cannot compile ELSE without matching IF" << '\n';
                return {};
            }
            control_stack.back().branch_to_end = EmitELSE(out, control_stack.back().branch_to_else_or_end);
            if (control_stack.back().branch_to_end == static_cast<size_t>(-1)) {
                return {};
            }
            stack_depth = control_stack.back().stack_depth;
            break;
        case vb::OPCode::DROP: {
            EmitStackCleanup(out, 1U);
            --stack_depth;
            break;
        }
        case vb::OPCode::END:
            if (!control_stack.empty()) {
                const ControlContext context = control_stack.back();
                control_stack.pop_back();
                if (context.opcode == vb::OPCode::IF) {
                    if (context.branch_to_end == static_cast<size_t>(-1)) {
                        if (!PatchCBZW1(out, context.branch_to_else_or_end, out.size())) {
                            return {};
                        }
                    } else if (!PatchB(out, context.branch_to_end, out.size())) {
                        return {};
                    }
                }
                for (const size_t branch_pos : context.branches) {
                    if (!PatchB(out, branch_pos, out.size())) {
                        return {};
                    }
                }
                stack_depth = context.stack_depth + context.result_count;
                break;
            }
            EmitReturn(out, func, stack_depth);
            reached_function_end = true;
            break;
        case vb::OPCode::BR:
        case vb::OPCode::BR_IF: {
            const bool conditional = (instr.opcode == vb::OPCode::BR_IF);
            size_t skip_pos = 0U;
            if (conditional) {
                skip_pos = EmitIF(out);
                --stack_depth;
            }
            if (instr.index > control_stack.size()) {
                std::cout << "invalid branch label depth" << '\n';
                return {};
            }
            if (instr.index == control_stack.size()) {
                EmitReturn(out, func, stack_depth);
            } else {
                ControlContext &target = control_stack[control_stack.size() - 1U - instr.index];
                if (stack_depth < target.stack_depth + target.result_count) {
                    std::cout << "insufficient branch operands" << '\n';
                    return {};
                }
                if (target.opcode == vb::OPCode::LOOP) {
                    const size_t branch_pos = out.size();
                    AppendWord(out, 0x14000000U);
                    PatchB(out, branch_pos, target.loop_head_pos);
                } else {
                    const size_t discarded = stack_depth - target.stack_depth - target.result_count;
                    if (target.result_count != 0U && discarded != 0U) {
                        AppendWord(out, 0xF94003E1U);
                    }
                    EmitStackCleanup(out, discarded);
                    if (target.result_count != 0U && discarded != 0U) {
                        AppendWord(out, 0xF90003E1U);
                    }
                    target.branches.push_back(out.size());
                    AppendWord(out, 0x14000000U);
                }
            }
            if (conditional && !PatchCBZW1(out, skip_pos, out.size())) {
                return {};
            }
            break;
        }
        case vb::OPCode::NOP:
            break;
        case vb::OPCode::RETURN:
            EmitReturn(out, func, stack_depth);
            break;
        default:
            std::cout << "cannot compile opcode " << static_cast<unsigned>(instr.opcode) << '\n';
            return {};
        }
    }

    if (!control_stack.empty() || !reached_function_end) {
        std::cout << "cannot compile control block or function without END" << '\n';
        return {};
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
        machine[i] = CompileInstructions(module.functions[i], module.functions);
        // 16 Bits alignment
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
        g_active_module = &compiled;
        const int trap_code = setjmp(env); // NOLINT(cert-err52-cpp)
        if (trap_code == 0) {
            const uint64_t result = jit.entry(slots.data());
            if (test.exp.args.size() == 0) {
                std::cout << "PASS: " << test.act.field << '\n';
                g_wasm_trap_env = nullptr;
                g_active_module = nullptr;
                continue;
            }
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
        g_active_module = nullptr;
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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "../com/OPCode.hpp"
#include "byte_reader.hpp"

using namespace std;
using json = nlohmann::json;

struct Argument {
    string type;
    uint64_t value{};
};
struct Action {
    string type;
    string field;
    vector<Argument> args;
};

struct Expected {
    vector<Argument> args;
};
struct Function {
    string type;
    Action act;
    Expected exp;
};
struct Module {
    string file_name;
    vector<Function> fun;
};

struct Commands {
    vector<Module> ls;
};

Argument ReadArgument(const json &node) {
    Argument argument;
    node.at("type").get_to(argument.type);
    // wast2json stores numeric values as strings, for example "10".
    const string text = node.at("value").get<string>();
    argument.value = stoi(text);
    return argument;
}

// local.json is one JSON document, not one object per line.
// Each assert_return belongs to the nearest preceding module.
Commands ReadJson(const string &json_path) {
    ifstream json_file(json_path);
    if (!json_file.is_open()) {
        cout << "failed to open " << json_path << '\n';
        return {};
    }

    const json data = json::parse(json_file);
    Commands cmd;
    Module md;
    for (const json &command : data.at("commands")) {
        const string type = command.at("type").get<string>();
        if (type == "module") {
            if (!md.file_name.empty()) {
                cmd.ls.push_back(md);
            }
            md = Module();
            command.at("filename").get_to(md.file_name);
        } else if (type == "assert_return") {
            if (md.file_name.empty()) {
                cout << "assert_return appears before any module\n";
                continue;
            }
            Function function;
            function.type = type;
            const json &action = command.at("action");
            action.at("type").get_to(function.act.type);
            action.at("field").get_to(function.act.field);
            for (const json &arg : action.at("args")) {
                function.act.args.push_back(ReadArgument(arg));
            }
            for (const json &item : command.at("expected")) {
                function.exp.args.push_back(ReadArgument(item));
            }
            md.fun.push_back(function);
        }
    }
    if (!md.file_name.empty()) {
        cmd.ls.push_back(md);
    }
    return cmd;
}

void PrintCommands(const Commands &cmd) {
    for (const Module &module : cmd.ls) {
        cout << "module " << module.file_name << " asserts " << module.fun.size() << '\n';
        for (const Function &function : module.fun) {
            cout << "  " << function.act.field << " args";
            for (const Argument &arg : function.act.args) {
                cout << ' ' << arg.type << ':' << arg.value;
            }
            cout << " expected";
            for (const Argument &expected : function.exp.args) {
                cout << ' ' << expected.type << ':' << expected.value;
            }
            cout << '\n';
        }
    }
}

class ExecutableBuffer {
  public:
    ExecutableBuffer() = default;
    ~ExecutableBuffer() {
        Release();
    }

    ExecutableBuffer(const ExecutableBuffer &) = delete;
    ExecutableBuffer &operator=(const ExecutableBuffer &) = delete;

    ExecutableBuffer(ExecutableBuffer &&other) noexcept : data_(other.data_), size_(other.size_), used_(other.used_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.used_ = 0;
    }

    ExecutableBuffer &operator=(ExecutableBuffer &&other) noexcept {
        if (this != &other) {
            Release();
            data_ = other.data_;
            size_ = other.size_;
            used_ = other.used_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.used_ = 0;
        }
        return *this;
    }

    bool Allocate(size_t bytes) {
        Release();
        const long page_size = sysconf(_SC_PAGESIZE);
        if (page_size <= 0 || bytes == 0U) {
            return false;
        }

        const size_t page = static_cast<size_t>(page_size);
        size_ = ((bytes + page - 1U) / page) * page;
        void *mem = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) {
            size_ = 0;
            return false;
        }

        data_ = static_cast<uint8_t *>(mem);
        used_ = 0;
        return true;
    }

    uint8_t *Emit(const vector<uint8_t> &machine) {
        const size_t aligned = (used_ + 15U) & ~15U;
        if (data_ == nullptr || machine.empty() || aligned + machine.size() > size_) {
            return nullptr;
        }

        uint8_t *entry = data_ + aligned;
        memcpy(entry, machine.data(), machine.size());
        used_ = aligned + machine.size();
        return entry;
    }

    bool ProtectExecutable() {
        if (data_ == nullptr || mprotect(data_, size_, PROT_READ | PROT_EXEC) != 0) {
            return false;
        }
        char *start = reinterpret_cast<char *>(data_);
        __builtin___clear_cache(start, start + size_);
        return true;
    }

  private:
    void Release() {
        if (data_ != nullptr) {
            munmap(data_, size_);
            data_ = nullptr;
            size_ = 0;
            used_ = 0;
        }
    }

    uint8_t *data_ = nullptr;
    size_t size_ = 0;
    size_t used_ = 0;
};

// The machine-code ABI is uniform: arguments and locals occupy 64-bit slots.
// JitFunc retains the Wasm signature so i32 and i64 calls are marshalled correctly.
using JitEntry = uint64_t (*)(uint64_t *slots);

struct JitFunc {
    vector<WasmValueType> params;
    vector<WasmValueType> results;
    uint32_t slot_count = 0;
    JitEntry entry = nullptr;
};

struct CompiledModule {
    string file_name;
    vector<CompiledFunctions> functions;
    vector<JitFunc> entries;
    unordered_map<string, uint32_t> exports;
    ExecutableBuffer code;
};

void AppendByte(vector<uint8_t> &out, uint8_t value) {
    out.push_back(value);
}

void AppendWord(vector<uint8_t> &out, uint32_t word) {
    AppendByte(out, static_cast<uint8_t>(word & 0xFFU));
    AppendByte(out, static_cast<uint8_t>((word >> 8) & 0xFFU));
    AppendByte(out, static_cast<uint8_t>((word >> 16) & 0xFFU));
    AppendByte(out, static_cast<uint8_t>((word >> 24) & 0xFFU));
}

void EmitLoadLocal(vector<uint8_t> &out, uint32_t index) {
    AppendWord(out, 0xF9400001U | (index << 10U));
}

void EmitStoreLocal(vector<uint8_t> &out, uint32_t index) {
    AppendWord(out, 0xF9000001U | (index << 10U));
}

void EmitMovX1Imm(vector<uint8_t> &out, uint64_t imm) {
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

void PushStack(vector<uint8_t> &out) {
    AppendWord(out, 0xD10043FFU);
    AppendWord(out, 0xF90003E1U);
}

vector<uint8_t> CompileInstructions(const CompiledFunctions &func) {
    vector<uint8_t> out;
    for (size_t i = 0; i < func.body.size(); ++i) {
        const Instruction &instr = func.body[i];
        switch (instr.opcode) {
        case 0x20U:
            EmitLoadLocal(out, instr.index);
            PushStack(out);
            break;
        case 0x41U:
        case 0x42U:
            EmitMovX1Imm(out, static_cast<uint64_t>(instr.immediate));
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
            cout << "cannot compile opcode " << static_cast<unsigned>(instr.opcode) << '\n';
            return {};
        }
    }

    if (out.size() < 4U || out[out.size() - 4U] != 0xC0U) {
        AppendWord(out, 0xAA1F03E0U);
        AppendWord(out, 0xD65F03C0U);
    }
    return out;
}

void BindJITTable(const vector<ExpFunc> &exp, const vector<CompiledFunctions> &com, CompiledModule &module) {
    module.functions = com;
    for (const ExpFunc &item : exp) {
        if (item.kind == 0U && item.idx < module.functions.size()) {
            module.functions[item.idx].name = item.name;
        }
    }

    // Compile each Code-section body, then place that machine code at the JIT
    // entry selected by the same func_index the Export section stored.
    vector<vector<uint8_t>> machine(module.functions.size());
    size_t bytes = 16U;
    for (size_t i = 0; i < module.functions.size(); ++i) {
        machine[i] = CompileInstructions(module.functions[i]);
        // 向上对齐到 16 字节
        bytes += (machine[i].size() + 15U) & ~15U;
    }
    if (!module.code.Allocate(bytes)) {
        cout << "failed to allocate JIT memory for " << module.file_name << '\n';
        return;
    }

    module.entries.resize(machine.size());
    for (size_t i = 0; i < machine.size(); ++i) {
        uint8_t *entry = module.code.Emit(machine[i]);
        if (entry == nullptr) {
            cout << "failed to emit JIT entry for function " << i << '\n';
            continue;
        }
        module.entries[i].params = module.functions[i].params;
        module.entries[i].results = module.functions[i].results;
        module.entries[i].slot_count = static_cast<uint32_t>(module.functions[i].params.size()) + module.functions[i].decl_cnt;
        module.entries[i].entry = reinterpret_cast<JitEntry>(entry);
    }
    for (const ExpFunc &item : exp) {
        if (item.kind == 0U && item.idx < module.entries.size()) {
            module.exports[item.name] = item.idx;
        }
    }
    if (!module.code.ProtectExecutable()) {
        cout << "failed to mark JIT memory executable for " << module.file_name << '\n';
    }
}

CompiledModule ParseWasm(const string &file_path) {
    CompiledModule module;
    module.file_name = file_path;
    if (file_path.empty()) {
        return module;
    }

    ifstream file(file_path, ios::binary);
    if (!file.is_open()) {
        cout << "failed to open " << file_path << '\n';
        return module;
    }

    // Parse wasm file
    cout << "Open " << file_path << "..." << '\n';
    const vector<uint8_t> stream{istreambuf_iterator<char>(file), istreambuf_iterator<char>()};
    ByteReader br(stream);
    br.ParseHeader();
    br.ParseSections();

    // Bind to machine code
    BindJITTable(br.GetExportedFunctions(), br.GetCompiledFunctions(), module);

    return module;
}

bool RunAssert(const Module &module, const CompiledModule &compiled) {
    bool res = true;
    for (const Function &test : module.fun) {
        // map<function_name, index>
        const unordered_map<string, uint32_t>::const_iterator found = compiled.exports.find(test.act.field);
        if (found == compiled.exports.end() || found->second >= compiled.entries.size()) {
            cout << "FAIL missing " << test.act.field << '\n';
            res = false;
            continue;
        }
        const JitFunc &jit = compiled.entries[found->second];
        if (jit.entry == nullptr || test.act.args.size() != jit.params.size()) {
            cout << "FAIL invalid invocation " << test.act.field << '\n';
            res = false;
            continue;
        }
        const uint32_t slot_count = (jit.slot_count == 0U) ? 1U : jit.slot_count;
        vector<uint64_t> slots(slot_count, 0U);
        for (size_t i = 0; i < test.act.args.size() && i < slots.size(); ++i) {
            slots[i] = test.act.args[i].value;
        }
        const uint64_t result = jit.entry(slots.data());
        // VOID function
        if (test.exp.args.empty()) {
            cout << "PASS " << test.act.field << '\n';
            continue;
        }
        // Compare the result with expection
        const uint64_t expected = test.exp.args[0].value;
        if (result == expected) {
            cout << "PASS " << test.act.field << " => " << result << '\n';
        } else {
            cout << "FAIL " << test.act.field << " got " << result << " expected " << expected << '\n';
            res = false;
        }
    }
    return res;
}

TEST(WasmCompilerLearnTest, Chapter02Test) {
    cout << "Star to parse wasm code..." << endl;

    // Parse json file
    const Commands cmd = ReadJson("./local.json");
    ASSERT_FALSE(cmd.ls.empty()) << "No Wasm modules were loaded from local.json";
    PrintCommands(cmd);

    // Parse wasm
    vector<CompiledModule> com;
    for (const Module &module : cmd.ls) {
        com.push_back(ParseWasm(module.file_name));
    }

    // Run commands/functions
    bool res = true;
    for (size_t i = 0; i < cmd.ls.size(); ++i) {
        cout << "JIT map " << com[i].file_name << " functions " << com[i].functions.size() << '\n';
        res &= RunAssert(cmd.ls[i], com[i]);
    }
    EXPECT_TRUE(res);
}

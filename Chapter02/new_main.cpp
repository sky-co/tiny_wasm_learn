// #include <cstddef>
// #include <cstdint>
// #include <cstring>
// #include <fstream>
// #include <iomanip>
// #include <iostream>
// #include <iterator>
// #include <string>
// #include <sys/mman.h>
// #include <sys/types.h>
// #include <unistd.h>
// #include <unordered_map>
// #include <vector>

#include "../com/OPCode.hpp"
#include "byte_reader.hpp"

using namespace std;
using json = nlohmann::json;

struct Argument {
    string type;
    int value{};
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

struct CompiledModule {
    string file_name;
    vector<CompiledFunctions> functions;
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
        default:
            break;
        }
    }

    return out;
}

void BindJITTable(const vector<ExpFunc> &exp, const vector<CompiledFunctions> &com, CompiledModule &module) {
    module.functions = com;
    for (size_t i = 0; i < exp.size(); ++i) {
        module.functions[i].name = exp[i].name;
    }
    // Compile opcode
    for (const auto &func : module.functions) {
        CompileInstructions(func);
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

int main() {
    cout << "Star to parse wasm code..." << endl;

    // Parse json file
    const Commands cmd = ReadJson("./local.json");
    PrintCommands(cmd);

    // Parse wasm
    vector<CompiledModule> com;
    for (const Module &module : cmd.ls) {
        com.push_back(ParseWasm(module.file_name));
    }

    // Run commands/functions

    return 0;
}

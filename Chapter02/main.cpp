#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "nlohmann/json.hpp"

using namespace std;
using json = nlohmann::json;

// Build and run from Chapter02:
//   g++ -std=c++14 -Wall -Wextra -g main.cpp -o assert_wasm && ./assert_wasm
//
// JIT ABI: uint64_t (*)(uint64_t* locals)
//   x86_64: locals in rdi, wasm stack top / return value in rax
//   arm64:  locals in x0, wasm stack top in x1, return value moved to x0

// One JSON value, for example {"type":"i32","value":"2"}.
struct WasmValue {
    string type;
    string value;
};

void from_json(const json &node, WasmValue &value) {
    node.at("type").get_to(value.type);
    node.at("value").get_to(value.value);
}

// invoke action inside an assert_return command.
struct InvokeAction {
    string type;
    string field;
    vector<WasmValue> args;
};

// assert_return commands belong to the nearest preceding module command.
struct AssertReturn {
    int line = 0;
    InvokeAction action;
    vector<WasmValue> expected;
};

// A wast module plus every assert_return until the next "type":"module".
struct TestModule {
    int line = 0;
    string filename;
    vector<AssertReturn> asserts;
};

struct WastSuite {
    string source_filename;
    vector<TestModule> modules;
};

enum class ValueType { I32, I64 };

struct FuncType {
    vector<ValueType> params;
    vector<ValueType> results;
};

struct Instr {
    uint8_t opcode = 0;
    uint32_t index = 0;
    int64_t immediate = 0;
};

struct WasmFunction {
    uint32_t type_index = 0;
    vector<ValueType> locals;
    vector<Instr> body;
    string export_name;
};

struct WasmModule {
    vector<FuncType> types;
    vector<WasmFunction> functions;
};

// Task 2.2: storage for one function's parameters and locals.
// Slots [0, param_count) are parameters. The rest are locals and start at 0.
class LocalFrame {
  public:
    void Reset(uint32_t slot_count) {
        slots_.assign(slot_count == 0U ? 1U : slot_count, 0U);
    }

    void Set(uint32_t index, uint64_t value) {
        slots_.at(index) = value;
    }

    uint64_t *data() {
        return slots_.data();
    }

    uint32_t size() const {
        return static_cast<uint32_t>(slots_.size());
    }

  private:
    vector<uint64_t> slots_;
};

using JitFunc = uint64_t (*)(uint64_t *locals);

// Task 2.1: exported function name -> JIT entry address, plus the signature
// needed to build that function's LocalFrame.
struct CompiledFunction {
    JitFunc code = nullptr;
    uint32_t param_count = 0;
    uint32_t local_count = 0;
    vector<ValueType> params;
    vector<ValueType> results;
};

class ExecutableBuffer {
  public:
    ExecutableBuffer() = default;
    ~ExecutableBuffer() {
        Release();
    }

    ExecutableBuffer(const ExecutableBuffer &) = delete;
    ExecutableBuffer &operator=(const ExecutableBuffer &) = delete;

    ExecutableBuffer(ExecutableBuffer &&other) noexcept : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    ExecutableBuffer &operator=(ExecutableBuffer &&other) noexcept {
        if (this != &other) {
            Release();
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    bool Allocate(size_t bytes) {
        Release();
        if (sysconf(_SC_PAGESIZE) <= 0) {
            return false;
        }
        const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        size_ = ((bytes + page - 1U) / page) * page;
        void *mem = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mem == MAP_FAILED) {
            size_ = 0;
            return false;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        data_ = reinterpret_cast<uint8_t *>(mem);
        return true;
    }

    bool ProtectExecutable() {
        if (data_ == nullptr) {
            return false;
        }
        if (mprotect(data_, size_, PROT_READ | PROT_EXEC) != 0) {
            return false;
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        char *start = reinterpret_cast<char *>(data_);
        __builtin___clear_cache(start, start + size_);
        return true;
    }

    uint8_t *data() {
        return data_;
    }

  private:
    void Release() {
        if (data_ != nullptr) {
            munmap(data_, size_);
            data_ = nullptr;
            size_ = 0;
        }
    }

    uint8_t *data_ = nullptr;
    size_t size_ = 0;
};

struct CompiledModule {
    unordered_map<string, CompiledFunction> exports;
    ExecutableBuffer code;
};

class ByteReader {
  public:
    explicit ByteReader(const vector<uint8_t> &bytes) : bytes_(bytes), pos_(0) {
    }

    void ParseHeader() {
        if (ReadU8() != 0x00 || ReadU8() != 0x61 || ReadU8() != 0x73 || ReadU8() != 0x6d) {
            cout << "invalid wasm magic" << endl;
        } else {
            cout << "WASM_BINARY_MAGIC: 0061 736d" << endl;
        }

        if (ReadU8() != 0x01 || ReadU8() != 0x00 || ReadU8() != 0x00 || ReadU8() != 0x00) {
            cout << "unsupported wasm version" << endl;
        } else {
            cout << "WASM_BINARY_VERSION: 0100 0000" << endl;
        }
    }

    void ParseSections() {
        while (true) {
            const uint8_t section_id = ReadU8();
            // cout << "Parse section: " << ReadString() << endl;
            const uint32_t payload_size = ReadU32Leb128();
            cout << "section id = " << static_cast<unsigned>(section_id) << ", payload size = " << payload_size << '\n';

            switch (section_id) {
            case 1:
                ParseTypeSection(payload_size);
                break;
            case 3:
                ParseFunctionSection(payload_size);
                break;
            case 7:
                ParseExportSection(payload_size);
                break;
            case 10:
                ParseCodeSection(payload_size);
                break;
            default:
                SkipBytes();
                return;
            }
        }
    }

    size_t Position() const {
        return pos_;
    }

  private:
    uint8_t ReadU8() {
        int8_t res = 0;
        if (pos_ < bytes_.size()) {
            res = static_cast<int8_t>(bytes_[pos_++]);
        }
        return res;
    }

    void PrintHexByte(uint8_t value) const {
        cout << hex << setfill('0') << setw(2) << static_cast<unsigned>(value) << dec << ' ';
    }

    uint32_t ReadU32Leb128() {
        uint32_t result = 0;
        uint32_t shift = 0;
        uint32_t bits = 0;
        do {
            bits = ReadU8();
            result |= (bits & 0x7FU) << shift; /* low-order 7 bits of byte */
            shift += 7;
        } while ((bits & 0x80U) != 0); /* get high-order bit of byte */

        return result;
    }

    void ParseTypeSection(const uint32_t payload_size) {
        for (uint32_t i = 0; i < payload_size; ++i) {
            PrintHexByte(ReadU8());
        }
        cout << '\n';
    }

    void ParseFunctionSection(const uint32_t payload_size) {
        for (uint32_t i = 0; i < payload_size; ++i) {
            PrintHexByte(ReadU8());
        }
        cout << '\n';
    }

    void ParseExportSection(const uint32_t payload_size) {
        for (uint32_t i = 0; i < payload_size; ++i) {
            PrintHexByte(ReadU8());
        }
        cout << '\n';
    }

    void ParseCodeSection(const uint32_t payload_size) {
        if (payload_size <= 0) {
            return;
        }

        const uint8_t fun_cnt = ReadU8();
        cout << "Function count: " << static_cast<unsigned>(fun_cnt) << " \n";
        if (fun_cnt > 0) {
            const uint8_t bd_sz = ReadU8();
            cout << "Function body size: " << static_cast<unsigned>(bd_sz) << " \n";
            if (bd_sz > 0) {
                const uint8_t loc_decl_cnt = ReadU8();
                cout << "Function local decl count: " << static_cast<unsigned>(loc_decl_cnt) << " \n";
                cout << "Function opcode: ";
                PrintHexByte(ReadU8());
                cout << '\n';
                const uint8_t op_code = ReadU8();
                cout << "Function opcode: ";
                PrintHexByte(op_code);
                cout << '\n';
            }
        }
    }

    void SkipBytes() {
        for (uint32_t i = Position(); i < bytes_.size(); ++i) {
            PrintHexByte(ReadU8());
        }
        cout << '\n';
    }

    string ReadString() {
        const uint32_t length = ReadU32Leb128();
        cout << "payload size: " << length << endl;
        std::string result;

        for (uint32_t i = 0; i < length; ++i) {
            result += std::to_string(ReadU8()) + " ";
        }
        cout << "section content: " << result << endl;

        return result;
    }

  private:
    vector<uint8_t> bytes_;
    uint8_t pos_;
};

void ParseWasm(const string file_path) {
    if (file_path.empty()) {
        return;
    }

    ifstream file(file_path, ios::binary);
    if (!file.is_open()) {
        cout << "failed to open " << file_path << '\n';
    } else {
        cout << "Open " << file_path << "..." << '\n';
        // Parse wasm code here
        const vector<uint8_t> stream{istreambuf_iterator<char>(file), istreambuf_iterator<char>()};
        ByteReader br(stream);
        br.ParseHeader();
        br.ParseSections();
    }
}

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

int main() {
    cout << "Star to parse wasm code..." << endl;

    const Commands cmd = ReadJson("./local.json");
    PrintCommands(cmd);
    for (const Module &module : cmd.ls) {
        ParseWasm(module.file_name);
    }

    return 0;
}

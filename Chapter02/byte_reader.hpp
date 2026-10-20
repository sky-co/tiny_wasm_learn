#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "../com/nlohmann/json.hpp"

using namespace std;
// using json = nlohmann::json;

enum class WasmValueType : uint8_t {
    I32 = 0x7FU,
    I64 = 0x7EU,
};

struct FuncType {
    vector<WasmValueType> params;
    vector<WasmValueType> results;
};

struct ExpFunc {
    string name;
    uint32_t kind = 0;
    uint32_t idx = 0;
};

struct Instruction {
    uint8_t opcode = 0;
    uint32_t index = 0;
    int64_t immediate = 0;
};

struct CompiledFunctions {
    string name;
    uint32_t decl_cnt = 0;
    vector<WasmValueType> params;
    vector<WasmValueType> results;
    vector<Instruction> body;
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
        while (pos_ < bytes_.size()) {
            const uint8_t section_id = ReadU8();
            const uint32_t payload_size = ReadU32Leb128();
            const size_t section_end = pos_ + payload_size;
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
                SkipTo(section_end);
                break;
            }
        }
    }

    const vector<CompiledFunctions> &GetCompiledFunctions() {
        return functions_;
    }

    const vector<ExpFunc> &GetExportedFunctions() {
        return exports_;
    }

  private:
    uint8_t ReadU8() {
        if (pos_ >= bytes_.size()) {
            return 0;
        }
        return bytes_[pos_++];
    }

    uint32_t ReadU32Leb128() {
        uint32_t result = 0;
        uint32_t shift = 0;
        uint32_t bits = 0;
        do {
            bits = ReadU8();
            result |= (bits & 0x7FU) << shift;
            shift += 7U;
        } while ((bits & 0x80U) != 0U);

        return result;
    }

    int64_t ReadSignedLeb128(uint32_t max_bits) {
        uint64_t result = 0;
        uint32_t shift = 0;
        uint8_t byte = 0;
        do {
            byte = ReadU8();
            result |= static_cast<uint64_t>(byte & 0x7FU) << shift;
            shift += 7U;
        } while ((byte & 0x80U) != 0U && shift < max_bits);
        if (shift < max_bits && (byte & 0x40U) != 0U) {
            result |= ~uint64_t{0} << shift;
        }
        return static_cast<int64_t>(result);
    }

    void SkipTo(size_t section_end) {
        while (pos_ < section_end && pos_ < bytes_.size()) {
            ReadU8();
        }
    }

    void ParseTypeSection(const uint32_t payload_size) {
        const size_t section_end = pos_ + payload_size;
        const uint32_t type_nums = ReadU32Leb128();
        cout << "Type nums: " << type_nums << '\n';
        types_.clear();
        for (uint32_t i = 0; i < type_nums; ++i) {
            FuncType func_type;
            if (ReadU8() == 0x60) {
                const uint32_t param_count = ReadU32Leb128();
                for (uint32_t param = 0; param < param_count; ++param) {
                    func_type.params.push_back(static_cast<WasmValueType>(ReadU8()));
                }
                const uint32_t result_count = ReadU32Leb128();
                for (uint32_t result = 0; result < result_count; ++result) {
                    func_type.results.push_back(static_cast<WasmValueType>(ReadU8()));
                }
            }
            types_.push_back(func_type);
            cout << "  type " << i << " params " << func_type.params.size() << " results " << func_type.results.size() << '\n';
        }
        SkipTo(section_end);
    }

    void ParseFunctionSection(const uint32_t payload_size) {
        const size_t section_end = pos_ + payload_size;
        const uint32_t func_nums = ReadU32Leb128();
        functions_.assign(func_nums, CompiledFunctions{});
        cout << "Function nums: " << func_nums << '\n';
        for (uint32_t i = 0; i < func_nums; ++i) {
            const uint32_t type_index = ReadU32Leb128();
            if (type_index < types_.size()) {
                functions_[i].params = types_[type_index].params;
                functions_[i].results = types_[type_index].results;
            }
        }

        SkipTo(section_end);
    }

    void ParseExportSection(const uint32_t payload_size) {
        const size_t section_end = pos_ + payload_size;
        const uint32_t export_nums = ReadU32Leb128();
        cout << "Export nums: " << export_nums << '\n';

        for (uint32_t i = 0; i < export_nums; ++i) {
            const uint32_t name_len = ReadU32Leb128();
            string name;
            name.reserve(name_len);
            for (uint32_t n = 0; n < name_len; ++n) {
                // name.push_back(static_cast<char>(ReadU8()));
                name += static_cast<char>(ReadU8());
            }

            ExpFunc exp;
            exp.name = name;
            exp.kind = ReadU8();
            exp.idx = ReadU32Leb128();
            cout << "export " << exp.name << " kind " << static_cast<unsigned>(exp.kind) << " func " << exp.idx << '\n';
            exports_.push_back(exp);
        }

        SkipTo(section_end);
    }

    void ParseCodeSection(const uint32_t payload_size) {
        const size_t section_end = pos_ + payload_size;
        const uint32_t func_nums = ReadU32Leb128();
        cout << "Function nums: " << func_nums << '\n';

        for (uint32_t i = 0; i < func_nums; ++i) {
            const uint32_t body_size = ReadU32Leb128();
            const size_t body_end = pos_ + body_size;
            // 声明组的个数
            const uint32_t local_groups = ReadU32Leb128();
            uint32_t local_count = 0;
            for (uint32_t group = 0; group < local_groups; ++group) {
                // 一组有几个 local
                const uint32_t group_count = ReadU32Leb128();
                ReadU8();
                local_count += group_count;
            }

            functions_[i].decl_cnt = local_count;
            cout << "  func " << i << " locals " << local_count << " ops";
            while (pos_ < body_end && pos_ < bytes_.size()) {
                const uint8_t opcode = ReadU8();
                Instruction instr;
                instr.opcode = opcode;
                if (opcode == 0x20U || opcode == 0x21U || opcode == 0x22U) {
                    instr.index = ReadU32Leb128();
                } else if (opcode == 0x41U) {
                    instr.immediate = ReadSignedLeb128(32U);
                } else if (opcode == 0x42U) {
                    instr.immediate = ReadSignedLeb128(64U);
                } else if (opcode != 0x0BU) {
                    cout << " unsupported opcode " << static_cast<unsigned>(opcode);
                    break;
                } else {
                    // do nothing
                }
                functions_[i].body.push_back(instr);
                cout << ' ' << hex << setfill('0') << setw(2) << static_cast<unsigned>(opcode) << dec;
                if (opcode == 0x0BU) {
                    break;
                }
            }

            SkipTo(body_end);
        }

        SkipTo(section_end);
    }

  private:
    vector<uint8_t> bytes_;
    size_t pos_;

    vector<FuncType> types_;
    vector<CompiledFunctions> functions_;
    vector<ExpFunc> exports_;
};

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

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

int main() {
    cout << "Star to parse wasm code..." << endl;

    const string file_path = "./add.wasm";
    ParseWasm(file_path);

    return 0;
}

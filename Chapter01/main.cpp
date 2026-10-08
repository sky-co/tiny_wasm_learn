#include <cstdint>
#include <cstdio>
#include <fstream>
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
            printf("section id = %u, payload size = %u\n", section_id, payload_size);

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
        return bytes_[pos_++];
    }

    uint32_t ReadU32Leb128() {
        uint32_t result = 0;
        uint8_t shift = 0;
        uint8_t byte = 0;
        do {
            byte = ReadU8();
            result |= (byte & 0x7f) << shift; /* low-order 7 bits of byte */
            shift += 7;
        } while ((byte & 0x80) != 0); /* get high-order bit of byte */

        return result;
    }

    void ParseTypeSection(const uint32_t payload_size) {
        for (uint32_t i = 0; i < payload_size; ++i) {
            printf("%02x ", ReadU8());
        }
        printf("\n");
    }

    void ParseFunctionSection(const uint32_t payload_size) {
        for (uint32_t i = 0; i < payload_size; ++i) {
            printf("%02x ", ReadU8());
        }
        printf("\n");
    }

    void ParseExportSection(const uint32_t payload_size) {
        for (uint32_t i = 0; i < payload_size; ++i) {
            printf("%02x ", ReadU8());
        }
        printf("\n");
    }

    void ParseCodeSection(const uint32_t payload_size) {
        if (payload_size <= 0)
            return;

        uint8_t fun_cnt = ReadU8();
        printf("Function count: %d \n", fun_cnt);
        if (fun_cnt > 0) {
            uint8_t bd_sz = ReadU8();
            printf("Function body size: %d \n", bd_sz);
            if (bd_sz > 0) {
                uint8_t loc_decl_cnt = ReadU8();
                printf("Function local decl count: %d \n", bd_sz);
                printf("Function opcode: %02x \n", ReadU8());
                uint8_t op_code = ReadU8();
                printf("Function opcode: %02x \n", op_code);
            }
        }
    }

    void SkipBytes() {
        for (uint32_t i = Position(); i < bytes_.size(); ++i) {
            printf("%02x ", ReadU8());
        }
        printf("\n");
    }

    string ReadString() {
        const uint32_t length = ReadU32Leb128();
        cout << "payload size: " << length << endl;
        std::string result = "";

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
    if (file_path.empty())
        return;

    ifstream file(file_path, ios::binary);
    if (!file.is_open())
        cout << "failed to open " << file_path << '\n';
    else {
        cout << "Open " << file_path << "..." << '\n';
        // Parse wasm code here
        vector<uint8_t> stream{istreambuf_iterator<char>(file), istreambuf_iterator<char>()};
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

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "../com/byte_reader.hpp"

int main() {
    std::cout << "Start to parse wasm code..." << '\n';

    const char *const file_path = "./add.wasm";
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "failed to open " << file_path << '\n';
        return 1;
    }

    std::cout << "Open " << file_path << "..." << '\n';
    const std::vector<uint8_t> stream{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    ByteReader reader(stream);
    reader.ParseHeader();
    reader.ParseSections();
    return 0;
}

#include <cstddef>
#include <cstdlib>
#include <iostream>

#include "../com/wasm_helper.hpp"

int main() {
    std::cout << "Start to parse wasm code..." << '\n';

    const char *const json_env = std::getenv("WASM_TEST_JSON");
    const char *const json_path = json_env != nullptr ? json_env : "./local.json";
    return RunWasmJson(json_path);
}

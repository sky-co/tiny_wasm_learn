#include <cstddef>
#include <cstdlib>
#include <iostream>

#include "../com/wasm_helper.hpp"

using namespace std;

int main() {
    cout << "Star to parse wasm code..." << endl;

    // Parse json file
    const char *const json_env = std::getenv("WASM_TEST_JSON");
    const char *const json_path = json_env != nullptr ? json_env : "./arithm.json";
    return RunWasmJson(json_path);
}

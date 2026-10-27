#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "../com/json_helper.hpp"
#include "../com/wasm_helper.hpp"

using namespace std;
using json = nlohmann::json;

int main() {
    cout << "Star to parse wasm code..." << endl;

    // Parse json file
    const char *const json_env = std::getenv("WASM_TEST_JSON");
    const Commands cmd = ReadJson(json_env != nullptr ? json_env : "./arithm.json");
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

    return res ? 0 : 1;
}

#include <cstddef>
#include <iostream>
#include <vector>

#include "../com/json_helper.hpp"
#include "../com/wasm_helper.hpp"

int main() {
    std::cout << "Start to parse wasm code..." << '\n';

    const Commands cmd = ReadJson("./local.json");
    if (cmd.ls.empty()) {
        std::cerr << "No Wasm modules were loaded from local.json\n";
        return 1;
    }
    PrintCommands(cmd);

    std::vector<CompiledModule> compiled;
    for (const Module &module : cmd.ls) {
        compiled.push_back(ParseWasm(module.file_name));
    }

    bool res = true;
    for (size_t i = 0; i < cmd.ls.size(); ++i) {
        std::cout << "JIT map " << compiled[i].file_name << " functions " << compiled[i].functions.size() << '\n';
        res &= RunAssert(cmd.ls[i], compiled[i]);
    }

    return res ? 0 : 1;
}

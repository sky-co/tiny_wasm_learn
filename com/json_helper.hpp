#pragma once

#include <fstream>
#include <iostream>
#include <string>

#include "data_types.hpp"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

inline Argument ReadArgument(const json &node) {
    Argument argument;
    node.at("type").get_to(argument.type);
    // wast2json stores numeric values as std::strings, for example "10".
    const std::string text = node.at("value").get<std::string>();
    argument.value = std::stoi(text);
    return argument;
}

// local.json is one JSON document, not one object per line.
// Each assert_return belongs to the nearest preceding module.
inline Commands ReadJson(const std::string &json_path) {
    std::ifstream json_file(json_path);
    if (!json_file.is_open()) {
        std::cout << "failed to open " << json_path << '\n';
        return {};
    }

    const json data = json::parse(json_file);
    Commands cmd;
    Module md;
    for (const json &command : data.at("commands")) {
        const std::string type = command.at("type").get<std::string>();
        if (type == "module") {
            if (!md.file_name.empty()) {
                cmd.ls.push_back(md);
            }
            md = Module();
            command.at("filename").get_to(md.file_name);
        } else if (type == "assert_return") {
            if (md.file_name.empty()) {
                std::cout << "assert_return appears before any module\n";
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

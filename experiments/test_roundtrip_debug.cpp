#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include "nlohmann/json.hpp"
#include "pjson.h"

using json = nlohmann::json;

static int max_depth_found = 0;

static int get_json_depth(const json& j, int depth = 0) {
    if (depth > max_depth_found) max_depth_found = depth;
    if (j.is_object()) {
        for (auto& [k, v] : j.items())
            get_json_depth(v, depth + 1);
    } else if (j.is_array()) {
        for (auto& elem : j)
            get_json_depth(elem, depth + 1);
    }
    return max_depth_found;
}

static void nlohmann_to_pjson_full(const json& src, pjson& dst)
{
    switch( src.type() )
    {
    case json::value_t::null: dst.set_null(); break;
    case json::value_t::boolean: dst.set_bool(src.get<bool>()); break;
    case json::value_t::number_integer: dst.set_int(src.get<int64_t>()); break;
    case json::value_t::number_unsigned: dst.set_uint(src.get<uint64_t>()); break;
    case json::value_t::number_float: dst.set_real(src.get<double>()); break;
    case json::value_t::string: dst.set_string(src.get<std::string>().c_str()); break;
    case json::value_t::array:
    {
        dst.set_array();
        for( const auto& elem : src )
            nlohmann_to_pjson_full(elem, dst.push_back());
        break;
    }
    case json::value_t::object:
    {
        dst.set_object();
        for( const auto& [key, val] : src.items() )
            nlohmann_to_pjson_full(val, dst.obj_insert(key.c_str()));
        break;
    }
    default: dst.set_null(); break;
    }
}

int main() {
    std::cout << "Loading test.json..." << std::endl;
    std::ifstream f(TEST_JSON_PATH);
    if (!f.is_open()) { std::cerr << "Cannot open " TEST_JSON_PATH << std::endl; return 1; }
    
    json original;
    f >> original;
    f.close();
    
    std::cout << "Loaded. Size: " << original.size() << std::endl;
    
    // Find max depth
    std::cout << "Computing max depth..." << std::endl;
    get_json_depth(original);
    std::cout << "Max depth: " << max_depth_found << std::endl;
    
    // Try converting just a few top-level keys
    std::cout << "Converting first 3 keys to pjson..." << std::endl;
    fptr<pjson> froot;
    froot.New();
    froot->set_object();
    
    int count = 0;
    for (auto& [key, val] : original.items()) {
        if (count >= 3) break;
        std::cout << "  Key: " << key << " type: " << val.type_name() << std::endl;
        nlohmann_to_pjson_full(val, froot->obj_insert(key.c_str()));
        count++;
    }
    
    std::cout << "Done. froot size: " << froot->size() << std::endl;
    froot->free();
    froot.Delete();
    
    return 0;
}

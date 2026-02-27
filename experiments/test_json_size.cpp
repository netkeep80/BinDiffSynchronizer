#include <fstream>
#include <iostream>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

static size_t count_nodes( const json& v )
{
    size_t count = 1;
    if ( v.is_array() )
        for ( const auto& e : v )
            count += count_nodes( e );
    else if ( v.is_object() )
        for ( const auto& [k, val] : v.items() )
            count += count_nodes( val );
    return count;
}

int main()
{
    std::ifstream f( "tests/test.json" );
    json          root;
    f >> root;
    std::cout << "Nodes: " << count_nodes( root ) << "\n";
    std::cout << "Top level keys: " << root.size() << "\n";
    std::cout << "Is object: " << root.is_object() << "\n";
    std::cout << "Is array: " << root.is_array() << "\n";
    return 0;
}

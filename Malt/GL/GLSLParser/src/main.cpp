#include <string>
#include <iostream>

#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/analyze.hpp>
#include <tao/pegtl/contrib/trace.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/contrib/parse_tree_to_dot.hpp>

#include <rapidjson/document.h>
#include "rapidjson/prettywriter.h"

using namespace tao::pegtl;

#define STRING(string) TAO_PEGTL_STRING(string)
#define ISTRING(string) TAO_PEGTL_ISTRING(string)
#define KEYWORD(string) TAO_PEGTL_KEYWORD(string)

struct line_comment : seq<STRING("//"), until<eol, any>> {};
struct multiline_comment : seq<STRING("/*"), until<STRING("*/"), any>> {};
struct _s_ : star<sor<line_comment, multiline_comment, space>> {};

struct LPAREN : one<'('> {};
struct RPAREN : one<')'> {};
struct LBRACE : one<'{'> {};
struct RBRACE : one<'}'> {};
struct LBRACKET : one<'['> {};
struct RBRACKET : one<']'> {};
struct COMMA : one<','> {};
struct END : one<';'> {};
struct STRUCT : KEYWORD("struct") {};
struct IDENTIFIER : identifier {};
struct TYPE : identifier {};
struct DIGITS : plus<digit> {};
struct PRECISION : sor<KEYWORD("lowp"), KEYWORD("mediump"), KEYWORD("highp")> {};
struct IO : sor<KEYWORD("in"), KEYWORD("out"), KEYWORD("inout")> {};
struct ARRAY_SIZE : seq<LBRACKET, _s_, DIGITS, _s_, RBRACKET> {};

struct FILE_PATH : plus<not_one<'"'>> {};
struct QUOTED_FILE_PATH : seq<one<'"'>, FILE_PATH, one<'"'>> {};
struct LINE_DIRECTIVE : seq<STRING("#line"), _s_, DIGITS, _s_, QUOTED_FILE_PATH> {};

struct MEMBER : seq<opt<PRECISION>, _s_, TYPE, _s_, IDENTIFIER, _s_, opt<ARRAY_SIZE>, _s_, END> {};
struct MEMBERS : plus<seq<_s_, MEMBER, _s_>> {};
struct STRUCT_DEF : seq<STRUCT, _s_, IDENTIFIER, _s_, LBRACE, _s_, opt<MEMBERS>, _s_, RBRACE> {};

struct PARAMETER : seq<opt<IO>, _s_, opt<PRECISION>, _s_, TYPE, _s_, IDENTIFIER, _s_, opt<ARRAY_SIZE>> {};
struct PARAMETERS : list<seq<_s_, PARAMETER, _s_>, seq<_s_, COMMA, _s_>> {};
struct FUNCTION_DEC : seq<TYPE, _s_, IDENTIFIER, _s_, LPAREN, _s_, PARAMETERS, _s_, RPAREN, _s_, LBRACE> {}; 

struct GLSL_GRAMMAR : star<sor<LINE_DIRECTIVE, STRUCT_DEF, FUNCTION_DEC, any>> {};

template<typename Rule>
using selector = parse_tree::selector
<
    Rule,
    parse_tree::store_content::on
    <
        IDENTIFIER,
        TYPE,
        DIGITS,
        PRECISION,
        IO,
        ARRAY_SIZE,
        FILE_PATH,
        MEMBER,
        MEMBERS,
        STRUCT_DEF,
        PARAMETER,
        PARAMETERS,
        FUNCTION_DEC
    >
>;

void print_nodes(parse_tree::node& node)
{
    if(node.has_content())
    {
        std::cout << node.type << " : " << node.string_view() << std::endl;
    }
    
    for(auto& child : node.children)
    {
        if(child)
        {
            print_nodes(*child);
        }
    }
}

template<typename T>
parse_tree::node* get(parse_tree::node* parent)
{
    for(auto& child : parent->children)
    {
        if(child->is_type<T>())
        {
            return child.get();
        }
    }
    return nullptr;
}

int main(int argc, char* argv[])
{
    if(argc < 2) return 1;

    const char* path = argv[1];

    file_input input(path);
    
    #ifdef _DEBUG
    {
        const std::size_t issues = analyze<GLSL_GRAMMAR>();
        if(issues) return issues;

        standard_trace<GLSL_GRAMMAR>(input);
        input.restart();
    }
    #endif
    
    auto root = parse_tree::parse<GLSL_GRAMMAR, selector>(input);
    input.restart();
    if(!root) return 1;

    #ifdef _DEBUG
    {
        print_nodes(*root);
        parse_tree::print_dot(std::cout, *root);
    }
    #endif

    using namespace rapidjson;

    Document json;
    json.Parse("{}");
    json.AddMember("structs", Value(kObjectType), json.GetAllocator());
    Value& structs = json["structs"];
    json.AddMember("functions", Value(kObjectType), json.GetAllocator());
    Value& functions = json["functions"];

    std::string current_file = "";

    for(auto& child : root->children)
    {
        if(child->is_type<FILE_PATH>())
        {
            current_file = std::string(child->string_view());
        }
        else if(child->is_type<STRUCT_DEF>())
        {
            std::string name = std::string(get<IDENTIFIER>(child.get())->string_view());
            
            structs.AddMember(Value(name.c_str(), json.GetAllocator()), Value(kObjectType), json.GetAllocator());
            Value& struct_def = structs[name.c_str()];
            struct_def.AddMember("name", Value(name.c_str(), json.GetAllocator()), json.GetAllocator());
            struct_def.AddMember("file", Value(current_file.c_str(), json.GetAllocator()), json.GetAllocator());

            parse_tree::node* members = get<MEMBERS>(child.get());
            if(!members) continue;

            for(auto& member : members->children)
            {
                std::string name = std::string(get<IDENTIFIER>(member.get())->string_view());
                std::string type = std::string(get<TYPE>(member.get())->string_view());
                int array_size = 0;
                
                parse_tree::node* array_size_node = get<ARRAY_SIZE>(member.get());
                if(array_size_node)
                {
                    std::string size = std::string(get<DIGITS>(array_size_node)->string_view());
                    array_size = std::stoi(size);
                }

                struct_def.AddMember(Value(name.c_str(), json.GetAllocator()), Value(kObjectType), json.GetAllocator());
                Value& member_def = struct_def[name.c_str()];
                member_def.AddMember("name", Value(name.c_str(), json.GetAllocator()), json.GetAllocator());
                member_def.AddMember("type", Value(type.c_str(), json.GetAllocator()), json.GetAllocator());
                member_def.AddMember("size", Value(array_size), json.GetAllocator());
            }
        }
        else if(child->is_type<FUNCTION_DEC>())
        {
            std::string name = std::string(get<IDENTIFIER>(child.get())->string_view());
            std::string key_name = name;
            if(functions.HasMember(key_name.c_str()))
            {
                key_name = std::string(child->string_view());
            }
            std::string type = std::string(get<TYPE>(child.get())->string_view());
            
            functions.AddMember(Value(key_name.c_str(), json.GetAllocator()), Value(kObjectType), json.GetAllocator());
            Value& function_dec = functions[key_name.c_str()];
            function_dec.AddMember("name", Value(name.c_str(), json.GetAllocator()), json.GetAllocator());
            function_dec.AddMember("type", Value(type.c_str(), json.GetAllocator()), json.GetAllocator());
            function_dec.AddMember("file", Value(current_file.c_str(), json.GetAllocator()), json.GetAllocator());

            parse_tree::node* parameters = get<PARAMETERS>(child.get());
            if(!parameters) continue;

            for(auto& parameter : parameters->children)
            {
                std::string name = std::string(get<IDENTIFIER>(parameter.get())->string_view());
                std::string type = std::string(get<TYPE>(parameter.get())->string_view());
                std::string io = "in";
                int array_size = 0;
                
                parse_tree::node* array_size_node = get<ARRAY_SIZE>(parameter.get());
                if(array_size_node)
                {
                    std::string size = std::string(get<DIGITS>(array_size_node)->string_view());
                    array_size = std::stoi(size);
                }
                
                parse_tree::node* io_node = get<IO>(parameter.get());
                if(io_node)
                {
                    io = std::string(io_node->string_view());
                }

                function_dec.AddMember(Value(name.c_str(), json.GetAllocator()), Value(kObjectType), json.GetAllocator());
                Value& parameter_dec = function_dec[name.c_str()];
                parameter_dec.AddMember("name", Value(name.c_str(), json.GetAllocator()), json.GetAllocator());
                parameter_dec.AddMember("type", Value(type.c_str(), json.GetAllocator()), json.GetAllocator());
                parameter_dec.AddMember("size", Value(array_size), json.GetAllocator());
                parameter_dec.AddMember("io", Value(io.c_str(), json.GetAllocator()), json.GetAllocator());
            }
        }
    }

    StringBuffer result;
    PrettyWriter<StringBuffer> writer(result);
    json.Accept(writer);

    std::cout << result.GetString();

    return 0;
}


#pragma once
// A small JSON reader for asset files (glTF): a DOM of null, bool, number, string,
// array and object, with limits so hostile input fails cleanly. Every failure throws
// AssetError; nesting is limited to 64 levels, the input to 256 MiB, so a crafted file
// can neither recurse the stack away nor allocate without bound.
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace raw::assets {

struct AssetError : std::runtime_error { using std::runtime_error::runtime_error; };

struct Json {
    enum class Kind : std::uint8_t { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json, std::less<>> o;

    bool is(Kind k) const { return kind == k; }
    const Json* get(std::string_view key) const;        // object member or nullptr
    const Json& at(std::size_t i) const;                  // array element; throws
    double number(const char* what) const;               // throws unless a finite number
    std::int64_t integer(const char* what, std::int64_t lo, std::int64_t hi) const;
};

Json parseJson(std::string_view text);

}

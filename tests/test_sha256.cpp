// SHA-256 against the FIPS 180-4 / NIST example vectors, including the
// two-block padding boundary.
#include "raw/sha256.hpp"
#include "check.hpp"
#include <string>
using namespace raw;
int main(){
    CHECK(sha256Hex(std::string("")) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex(std::string("abc")) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex(std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sha256Hex(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    CHECK(sha256File("this-file-does-not-exist.bin").empty());
    return raw_test_summary();
}

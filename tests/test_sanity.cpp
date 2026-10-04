#include "raw/core/version.hpp"
#include "check.hpp"
#include <cstring>

int main() {
    CHECK(std::strlen(raw::version()) > 0);
    return raw_test_summary();
}

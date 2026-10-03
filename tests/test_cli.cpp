// Exercise the actual CLI entrypoint, including its exact-budget render pass.
#define main raw_cli_main
#include "../app/main.cpp"
#undef main
#include "check.hpp"
#include <filesystem>
#include <cstdlib>
#include <exception>
#include <chrono>
#include <iterator>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

int main() {
    std::set_terminate([] {
        try { if (auto error = std::current_exception()) std::rethrow_exception(error); }
        catch (const std::exception& error) { std::fprintf(stderr, "terminated: %s\n", error.what()); }
        std::_Exit(3);
    });
#if defined(_MSC_VER)
    // A failed regression must report to CTest, never open an unattended dialog.
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    auto out = std::filesystem::current_path() / ("cli-regression-output-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(out);
    std::string destination = out.string();
    char program[] = "raw_native_cli";
    char* args[] = {program, destination.data()};
    CHECK(raw_cli_main(2, args) == 0);
    char versionFlag[] = "--version";
    char* versionArgs[] = {program, versionFlag};
    CHECK(raw_cli_main(2, versionArgs) == 0);
    CHECK(std::string(raw::version()).rfind("raw-native ", 0) == 0);
    char badFlag[] = "--no-such-flag";
    char* badArgs[] = {program, badFlag};
    CHECK(raw_cli_main(2, badArgs) == 2);
    for (const char* name : {"frame.ppm", "ao_rt.pgm", "ao_ss.pgm",
                             "ao_error.pgm", "certificate.json", "arena_certificate.json"}) {
        CHECK(std::filesystem::exists(out / name));
        CHECK(std::filesystem::file_size(out / name) > 0);
    }
    std::ifstream receipt(out / "arena_certificate.json");
    std::string certificate((std::istreambuf_iterator<char>(receipt)), {});
    CHECK(certificate.find("\"verdict\":\"verified\"") != std::string::npos);
    CHECK(certificate.find("[\"refusals\",\"0\"]") != std::string::npos);
    return raw_test_summary();
}

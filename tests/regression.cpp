// Include this implementation to exercise its internal protocol handlers without
// starting a desktop session or exposing test-only entry points in the product.
#include "../src/mcp_server.cpp"
#include "../src/cli_args.h"
#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) return 2;
    std::string test = argv[1];
    bool ok = false;
    if (test == "json") {
        ok = json_get_string(L"{\"method\" : \"tools/list\"}", L"method") == L"tools/list"
            && json_get_bool(L"{\"exact\"\t:\ttrue}", L"exact")
            && json_get_int(L"{\"pid\" : 42}", L"pid") == 42
            && json_get_string(L"{\"params\":{\"method\":\"wrong\"},\"method\":\"ping\"}", L"method") == L"ping"
            && !json_get_bool(L"{\"exact\":trueish}", L"exact")
            && json_get_int(L"{\"pid\":12.5}", L"pid") == -1
            && json_get_string(json_get_raw(L"{\"params\" : {\"arguments\" : {\"out\":\"x\"}}}", L"params"), L"out").empty();
    } else if (test == "overflow") {
        wchar_t exe[] = L"wgccli", flag[] = L"--pid", value[] = L"4294967296";
        wchar_t* args[] = {exe, flag, value};
        Options opts;
        ok = !parse_args(3, args, opts);
        uint32_t u32 = 0;
        uint64_t hwnd = 0;
        ok = ok && parse_u32_strict(L"4294967295", u32) && u32 == UINT32_MAX
            && !parse_u32_strict(L"-1", u32) && !parse_u32_strict(L"12abc", u32)
            && !parse_u32_strict(L" 12", u32)
            && parse_hwnd_strict(L"0xffffffffffffffff", hwnd) && hwnd == UINT64_MAX
            && !parse_hwnd_strict(L"18446744073709551616", hwnd)
            && !parse_hwnd_strict(L"-1", hwnd) && !parse_hwnd_strict(L"0x12zz", hwnd);
    } else if (test == "output") {
        CaptureRequest req;
        req.out = L"capture.jpg";
        req.format = L"bmp";
        req.format_explicit = true;
        auto output = resolve_output_target(req);
        ok = output.format == L"bmp" && output.path == req.out;
        req.out = L"screenshots.v2";
        req.format = L"png";
        output = resolve_output_target(req);
        ok = ok && std::filesystem::path(output.path).parent_path() == req.out;
        req.out = L"capture.JPEG";
        req.format_explicit = false;
        output = resolve_output_target(req);
        ok = ok && output.format == L"jpg" && output.path == req.out;
        const auto dir = std::filesystem::path("regression-output.jpg");
        const bool created = std::filesystem::create_directory(dir);
        req.out = dir.wstring();
        output = resolve_output_target(req);
        ok = ok && output.format == L"png" && std::filesystem::path(output.path).parent_path() == dir;
        if (created) std::filesystem::remove(dir); // Only our empty directory.
    } else if (test == "mcp_args") {
        ok = true;
        for (const auto& args : {
            L"{\"out\":\"unused\",\"pid\":4294967296}",
            L"{\"out\":\"unused\",\"max_width\":-1}",
            L"{\"out\":\"unused\",\"timeout_ms\":1.5}",
            L"{\"out\":\"unused\",\"resize\":\"4294967296x100\"}",
            L"{\"out\":\"unused\",\"crop\":\"0,0,4294967296,1\"}",
            L"{\"out\":\"unused\",\"hwnd\":\"-1\"}",
            L"{\"out\":\"unused\",\"format\":\"gif\"}"}) {
            auto response = do_capture_window(args);
            ok = ok && json_get_bool(response, L"isError")
                && response.find(L"No window matched") == std::wstring::npos;
        }
    }
    std::cout << test << (ok ? " PASS\n" : " FAIL\n");
    return ok ? 0 : 1;
}

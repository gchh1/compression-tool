#include "old_dpflate_bridge.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace test_bridge {

static bool run_tool(
    const std::vector<uint8_t>& input,
    std::vector<uint8_t>& output,
    const std::string& tool_path,
    const std::string& args)
{
    const auto work = fs::temp_directory_path() / "old_dpflate_bridge";
    std::error_code ec;
    fs::create_directories(work, ec);

    const auto in_path  = work / "in.bin";
    const auto out_path = work / "out.bin";

    {
        std::ofstream f(in_path, std::ios::binary | std::ios::trunc);
        if (!f) {
            fs::remove_all(work, ec);
            return false;
        }
        f.write(reinterpret_cast<const char*>(input.data()),
                static_cast<std::streamsize>(input.size()));
        f.close();
    }

    if (fs::exists(out_path, ec)) {
        fs::remove(out_path, ec);
    }

    std::string cmd = "\"" + tool_path + "\" " + args +
                      " \"" + in_path.string() + "\"" +
                      " \"" + out_path.string() + "\"";

    fprintf(stderr, "[BRIDGE] running: %s\n", cmd.c_str());

    int rc = 0;
#ifdef _WIN32
    {
        std::string cmd_line = cmd;
        STARTUPINFO si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        PROCESS_INFORMATION pi = {};
        BOOL ok = CreateProcessA(
            NULL,
            cmd_line.data(),
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            work.string().c_str(),
            &si,
            &pi);
        if (!ok) {
            fprintf(stderr, "[BRIDGE] CreateProcess failed: %lu\n", GetLastError());
            fs::remove_all(work, ec);
            return false;
        }
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, 300000);
        DWORD exit_code = 0;
        GetExitCodeProcess(pi.hProcess, &exit_code);
        CloseHandle(pi.hProcess);
        rc = static_cast<int>(exit_code);
    }
#else
    rc = std::system(cmd.c_str());
#endif

    fprintf(stderr, "[BRIDGE] tool returned rc=%d output_size=%lld\n", rc,
            static_cast<long long>(fs::file_size(out_path, ec)));
    if (rc != 0) {
        fs::remove_all(work, ec);
        return false;
    }

    std::ifstream in(out_path, std::ios::binary | std::ios::ate);
    if (!in) {
        fs::remove_all(work, ec);
        return false;
    }
    const auto sz = in.tellg();
    in.seekg(0);
    output.resize(static_cast<size_t>(sz));
    in.read(reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(sz));

    fs::remove_all(work, ec);
    return true;
}

std::vector<uint8_t> old_dpflate_compress(
    const std::vector<uint8_t>& data,
    size_t search_size,
    size_t lookahead_size,
    size_t min_match,
    size_t dp_top,
    size_t dp_sub_match_max,
    bool use_3hfmtree,
    size_t huffman_chunk_bits)
{
    fprintf(stderr, "[BRIDGE] old_dpflate_compress: n=%zu search=%zu look=%zu min_match=%zu dp_top=%zu sub=%zu 3hm=%d chunk=%zu\n",
            data.size(), search_size, lookahead_size, min_match,
            dp_top, dp_sub_match_max, use_3hfmtree ? 1 : 0, huffman_chunk_bits);

    char args[512];
    std::snprintf(args, sizeof(args), "%zu %zu %zu %zu %zu %d %zu",
                  search_size, lookahead_size, min_match,
                  dp_top, dp_sub_match_max,
                  use_3hfmtree ? 1 : 0, huffman_chunk_bits);

    const fs::path exe_dir = fs::path(
#ifdef _WIN32
        _pgmptr
#else
        "/proc/self/exe"
#endif
    ).parent_path();
    const std::string tool_path = (exe_dir / "old_dpflate_tool").string()
#ifdef _WIN32
        + ".exe"
#endif
        ;

    std::vector<uint8_t> out;
    if (!run_tool(data, out, tool_path, args)) {
        return {};
    }
    return out;
}

}
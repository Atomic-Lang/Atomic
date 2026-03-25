// linker_windows.hpp
// Linkagem Windows x64 — resolve caminhos do ld.exe embutido, monta comando COFF/PE

#ifndef ATOMIC_LINKER_WINDOWS_HPP
#define ATOMIC_LINKER_WINDOWS_HPP

#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

#ifdef _WIN32
    #include <windows.h>
#endif

namespace atomic {

// Normaliza barras para o padrao do sistema
static std::string normalize_path(const std::string& path) {
    std::string result = path;
    #ifdef _WIN32
    for (auto& c : result) {
        if (c == '/') c = '\\';
    }
    #endif
    return result;
}

// ============================================================================
// LINKAGEM: .obj -> .exe via ld embutido (src/backend_windows/ld_linker/)
// ============================================================================

static bool link_with_ld(const std::string& obj_path, const std::string& exe_path,
                          const std::vector<std::string>& extra_objs = {},
                          const std::vector<std::string>& extra_libs = {},
                          const std::vector<std::string>& extra_lib_paths = {},
                          bool windowed = false) {

    // Procurar ld.exe relativo ao diretorio atual
    std::string ld_dir = "src\\backend_windows\\ld_linker";
    std::string ld_exe = ld_dir + "\\ld.exe";

    if (!fs::exists(ld_exe)) {
        // Tentar relativo ao executavel do compilador
        #ifdef _WIN32
        char buf[MAX_PATH];
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string exe_dir = fs::path(buf).parent_path().string();
        ld_dir = normalize_path(exe_dir + "\\src\\backend_windows\\ld_linker");
        ld_exe = ld_dir + "\\ld.exe";
        #endif
    }

    if (!fs::exists(ld_exe)) {
        std::cerr << "Erro: ld.exe nao encontrado em 'src/backend_windows/ld_linker/'" << std::endl;
        return false;
    }

    std::string norm_obj = normalize_path(obj_path);
    std::string norm_exe = normalize_path(exe_path);

    // Montar comando replicando a ordem exata do g++ -static
    std::string cmd = "\"" + ld_exe + "\"";
    cmd += " -m i386pep -Bstatic";

    // Subsystem GUI (sem console)
    if (windowed) {
        cmd += " --subsystem windows";
    }

    cmd += " -o \"" + norm_exe + "\"";

    // CRT startup objects
    cmd += " \"" + ld_dir + "\\crt2.o\"";
    cmd += " \"" + ld_dir + "\\crtbegin.o\"";

    // Objeto principal do programa
    cmd += " \"" + norm_obj + "\"";

    // Objetos extras (.obj compilados)
    for (auto& obj : extra_objs) {
        if (fs::exists(obj)) {
            cmd += " \"" + normalize_path(obj) + "\"";
        }
    }

    cmd += " -L \"" + ld_dir + "\"";

    // Libs na ordem exata do g++ (primeiro bloco)
    cmd += " -lstdc++ -lmingw32 -lgcc -lgcc_eh -lmingwex -lmsvcrt";
    cmd += " -lkernel32 -lpthread -ladvapi32 -lshell32 -luser32 -lkernel32";

    // Caminhos extras de busca de libs (-L)
    for (auto& lpath : extra_lib_paths) {
        cmd += " -L \"" + normalize_path(lpath) + "\"";
    }

    // Libs extras
    for (auto& lib : extra_libs) {
        cmd += " -l" + lib;
    }

    // Segundo bloco (repetido para resolver dependencias circulares)
    cmd += " -lmingw32 -lgcc -lgcc_eh -lmingwex -lmsvcrt -lkernel32";

    // CRT finalization objects
    cmd += " \"" + ld_dir + "\\default-manifest.o\"";
    cmd += " \"" + ld_dir + "\\crtend.o\"";

    cmd += " -e main";

    // Adiciona ld_dir ao PATH para que o ld.exe encontre suas DLLs
    #ifdef _WIN32
    std::string old_path;
    {
        char* env_path = getenv("PATH");
        if (env_path) old_path = env_path;
        std::string new_path = ld_dir + ";" + old_path;
        SetEnvironmentVariableA("PATH", new_path.c_str());
    }
    #endif

    int ret = std::system(("\"" + cmd + "\"").c_str());

    #ifdef _WIN32
    // Restaurar PATH original
    SetEnvironmentVariableA("PATH", old_path.c_str());
    #endif

    if (ret != 0) {
        std::cerr << "Erro: Linkagem falhou (codigo " << ret << ")" << std::endl;
        return false;
    }

    return true;
}

} // namespace atomic

#endif // ATOMIC_LINKER_WINDOWS_HPP

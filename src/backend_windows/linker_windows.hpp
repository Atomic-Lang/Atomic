// linker_windows.hpp
// Linkagem Windows x64 — usa ld.exe embutido em src/backend_windows/ld_linker/
// Compile: incluido automaticamente pelo main.cpp

#ifndef ATOMIC_LINKER_WINDOWS_HPP
#define ATOMIC_LINKER_WINDOWS_HPP

#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <cstring>

namespace fs = std::filesystem;

namespace atomic {

// Normaliza barras para o padrao do Windows
static std::string normalize_path(const std::string& path) {
    std::string result = path;
    for (auto& c : result) {
        if (c == '/') c = '\\';
    }
    return result;
}

// ============================================================================
// Encontrar diretorio do ld_linker
// ============================================================================

static std::string find_ld_dir(const std::string& exe_dir) {
    // 1. Relativo ao executavel: <exe_dir>/src/backend_windows/ld_linker
    std::string candidate = exe_dir + "\\src\\backend_windows\\ld_linker";
    if (fs::exists(candidate + "\\ld.exe")) return normalize_path(candidate);

    // 2. Relativo ao diretorio atual
    candidate = "src\\backend_windows\\ld_linker";
    if (fs::exists(candidate + "\\ld.exe")) return normalize_path(candidate);

    // 3. Um nivel acima (caso exe esteja em bin/)
    candidate = "..\\src\\backend_windows\\ld_linker";
    if (fs::exists(candidate + "\\ld.exe")) return normalize_path(candidate);

    return "";
}

// ============================================================================
// Copiar DLAs para diretorio do executavel (necessario em runtime)
// ============================================================================

static void copy_dlas_to_exe_dir(const std::string& exe_path,
                                  const std::vector<std::string>& dla_paths) {
    if (dla_paths.empty()) return;

    fs::path exe_dir = fs::path(exe_path).parent_path();
    if (exe_dir.empty()) exe_dir = ".";

    for (auto& dla : dla_paths) {
        fs::path src(dla);
        if (!fs::exists(src)) continue;
        fs::path dst = exe_dir / src.filename();
        try {
            if (src != dst)
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        } catch (...) {}
    }
}

// ============================================================================
// Linkagem: .obj -> .exe via ld.exe embutido
// ============================================================================

static bool link_exe(const std::string& obj_path,
                     const std::string& exe_path,
                     const std::string& exe_dir,
                     const std::vector<std::string>& dla_paths = {},
                     bool windowed = false) {

    std::string ld_dir = find_ld_dir(exe_dir);
    if (ld_dir.empty()) {
        std::cerr << "error: ld.exe not found in src/backend_windows/ld_linker/" << std::endl;
        std::cerr << "Make sure the ld_linker folder is present." << std::endl;
        return false;
    }

    std::string ld_exe = ld_dir + "\\ld.exe";
    std::string norm_obj = normalize_path(obj_path);
    std::string norm_exe = normalize_path(exe_path);

    // Montar comando de linkagem
    std::string cmd = "\"" + ld_exe + "\"";
    cmd += " -m i386pep -Bstatic";

    if (windowed) {
        cmd += " --subsystem windows";
    }

    cmd += " -o \"" + norm_exe + "\"";

    // CRT startup objects
    cmd += " \"" + ld_dir + "\\crt2.o\"";
    cmd += " \"" + ld_dir + "\\crtbegin.o\"";

    // Objeto principal
    cmd += " \"" + norm_obj + "\"";

    // DLAs importadas (bibliotecas Atomic)
    for (auto& dla : dla_paths) {
        if (fs::exists(dla)) {
            cmd += " \"" + normalize_path(dla) + "\"";
        }
    }

    // Diretorio de busca de libs
    cmd += " -L \"" + ld_dir + "\"";

    // Libs do runtime (primeira passada)
    cmd += " -lstdc++ -lmingw32 -lgcc -lgcc_eh -lmingwex -lmsvcrt";
    cmd += " -lkernel32 -lpthread -ladvapi32 -lshell32 -luser32";

    // Segunda passada (resolve dependencias circulares)
    cmd += " -lmingw32 -lgcc -lgcc_eh -lmingwex -lmsvcrt -lkernel32";

    // CRT finalization
    cmd += " \"" + ld_dir + "\\default-manifest.o\"";
    cmd += " \"" + ld_dir + "\\crtend.o\"";

    cmd += " -e main";

    // Adicionar ld_dir ao PATH para DLLs do ld (libintl-8.dll, zlib1.dll, etc.)
    std::string old_path;
    {
        char* env_path = getenv("PATH");
        if (env_path) old_path = env_path;
        std::string new_path = ld_dir + ";" + old_path;
        _putenv_s("PATH", new_path.c_str());
    }

    int ret = std::system(("\"" + cmd + "\"").c_str());

    // Restaurar PATH original
    _putenv_s("PATH", old_path.c_str());

    if (ret != 0) {
        std::cerr << "error: linking failed (code " << ret << ")" << std::endl;
        return false;
    }

    // Copiar DLAs para diretorio do exe
    copy_dlas_to_exe_dir(exe_path, dla_paths);

    return true;
}

} // namespace atomic

#endif // ATOMIC_LINKER_WINDOWS_HPP
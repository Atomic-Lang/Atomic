// lib_loader.hpp
// Carrega descriptors JSON de bibliotecas nativas (.dla) para o compilador Atomic

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
extern "C" {
    __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void* hModule, char* lpFilename, unsigned long nSize);
}
#endif

namespace atomic {

namespace fs = std::filesystem;

// ============================================================================
// Estruturas de dados da biblioteca
// ============================================================================

struct LibParam {
    std::string tipo; // "inteiro", "texto"
};

struct LibFunc {
    std::string nome;
    std::string retorno; // "inteiro", "texto"
    std::vector<LibParam> params;
};

struct LibDescriptor {
    std::string name;       // nome da lib (ex: "time")
    std::string tipo;       // "estatico" ou "dinamico"
    std::string dla_path;   // caminho completo do .dla
    std::string json_path;  // caminho completo do .json
    std::vector<LibFunc> funcoes;
};

// ============================================================================
// Parser JSON minimo (sem dependencias externas)
// ============================================================================

class LibLoader {
public:
    // Procura a lib em libs/<name>/<name>.json relativo ao diretorio base
    // base_dir = diretorio onde o .at esta, ou o diretorio do compilador
    static LibDescriptor load(const std::string& lib_name, const std::string& base_dir) {
        // Tentar: libs/<name>/<name>.json
        fs::path lib_dir = fs::path(base_dir) / "libs" / lib_name;
        fs::path json_path = lib_dir / (lib_name + ".json");
        fs::path dla_path = lib_dir / (lib_name + ".dla");

        if (!fs::exists(json_path)) {
            // Tentar tambem no diretorio do executavel
            auto exe_dir = get_exe_dir();
            lib_dir = fs::path(exe_dir) / "libs" / lib_name;
            json_path = lib_dir / (lib_name + ".json");
            dla_path = lib_dir / (lib_name + ".dla");
        }

        if (!fs::exists(json_path)) {
            throw std::runtime_error(
                "library '" + lib_name + "' not found (searched libs/" + lib_name + "/" + lib_name + ".json)");
        }

        if (!fs::exists(dla_path)) {
            throw std::runtime_error(
                "library '" + lib_name + "' .dla not found at: " + dla_path.string());
        }

        // Ler JSON
        std::ifstream file(json_path);
        if (!file.is_open()) {
            throw std::runtime_error("could not open: " + json_path.string());
        }
        std::stringstream ss;
        ss << file.rdbuf();
        std::string json_str = ss.str();

        // Parsear
        LibDescriptor desc;
        desc.name = lib_name;
        desc.json_path = json_path.string();
        desc.dla_path = fs::absolute(dla_path).string();
        desc.tipo = extract_string_field(json_str, "type");
        desc.funcoes = parse_funcoes(json_str);

        return desc;
    }

private:
    static std::string get_exe_dir() {
        #ifdef _WIN32
        char buf[260];
        GetModuleFileNameA(nullptr, buf, 260);
        return fs::path(buf).parent_path().string();
        #else
        return fs::canonical("/proc/self/exe").parent_path().string();
        #endif
    }

    // ========================================================================
    // Mini JSON parser (suficiente para o formato do descriptor)
    // ========================================================================

    static std::string extract_string_field(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\"";
        auto pos = json.find(search);
        if (pos == std::string::npos) return "";

        pos = json.find(':', pos + search.size());
        if (pos == std::string::npos) return "";

        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) return "";
        pos++; // skip opening "

        auto end = json.find('"', pos);
        if (end == std::string::npos) return "";

        return json.substr(pos, end - pos);
    }

    static std::vector<LibFunc> parse_funcoes(const std::string& json) {
        std::vector<LibFunc> funcs;

        // Encontrar o array "functions"
        auto pos = json.find("\"functions\"");
        if (pos == std::string::npos) return funcs;

        pos = json.find('[', pos);
        if (pos == std::string::npos) return funcs;
        pos++; // skip [

        // Iterar sobre os objetos {}
        while (pos < json.size()) {
            // Pular whitespace
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\n' ||
                   json[pos] == '\r' || json[pos] == '\t' || json[pos] == ',')) pos++;

            if (pos >= json.size() || json[pos] == ']') break;

            if (json[pos] == '{') {
                // Encontrar o } correspondente
                size_t depth = 1;
                size_t start = pos;
                pos++;
                while (pos < json.size() && depth > 0) {
                    if (json[pos] == '{') depth++;
                    else if (json[pos] == '}') depth--;
                    pos++;
                }
                std::string obj = json.substr(start, pos - start);

                LibFunc fn;
                fn.nome = extract_string_field(obj, "name");
                fn.retorno = extract_string_field(obj, "return");
                fn.params = parse_params(obj);
                funcs.push_back(std::move(fn));
            } else {
                pos++;
            }
        }

        return funcs;
    }

    static std::vector<LibParam> parse_params(const std::string& obj) {
        std::vector<LibParam> params;

        auto pos = obj.find("\"params\"");
        if (pos == std::string::npos) return params;

        pos = obj.find('[', pos);
        if (pos == std::string::npos) return params;
        pos++; // skip [

        // Ler strings dentro do array
        while (pos < obj.size()) {
            while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\n' ||
                   obj[pos] == '\r' || obj[pos] == '\t' || obj[pos] == ',')) pos++;

            if (pos >= obj.size() || obj[pos] == ']') break;

            if (obj[pos] == '"') {
                pos++; // skip "
                auto end = obj.find('"', pos);
                if (end == std::string::npos) break;
                LibParam p;
                p.tipo = obj.substr(pos, end - pos);
                params.push_back(std::move(p));
                pos = end + 1;
            } else {
                pos++;
            }
        }

        return params;
    }
};

} // namespace atomic
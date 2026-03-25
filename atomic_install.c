/* instalador_atomic.c */
/* Instalador da linguagem Atomic - Baixa a ultima tag do GitHub e instala no sistema */
/* Compile Windows: gcc -o instalador_atomic.exe instalador_atomic.c -lshell32 */
/* Compile Linux:   gcc -o instalador_atomic instalador_atomic.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
    #include <direct.h>
    #include <shlobj.h>
    #define IS_WINDOWS 1
    #define PATH_SEP "\\"
#else
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <pwd.h>
    #define IS_WINDOWS 0
    #define PATH_SEP "/"
#endif

/* GitHub repository */
#define GITHUB_REPO "Atomic-Lang/Atomic"
#define GITHUB_TAGS_URL "https://api.github.com/repos/" GITHUB_REPO "/tags"
#define GITHUB_ZIP_BASE "https://github.com/" GITHUB_REPO "/archive/refs/tags/"
#define GITHUB_MAIN_ZIP "https://github.com/" GITHUB_REPO "/archive/refs/heads/main.zip"

/* ============================================================================ */
/* Helpers                                                                      */
/* ============================================================================ */

void run(const char *cmd) {
    printf("  >> %s\n", cmd);
    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "  (Comando retornou codigo %d - continuando...)\n", ret);
    }
}

void print_header(const char *msg) {
    printf("\n");
    printf("  ============================================\n");
    printf("    %s\n", msg);
    printf("  ============================================\n\n");
}

void print_step(const char *msg) {
    printf("  [*] %s\n", msg);
}

/* ============================================================================ */
/* Windows                                                                      */
/* ============================================================================ */

#ifdef _WIN32

/* Retorna o caminho de instalacao: %LOCALAPPDATA%\Atomic */
void get_install_dir(char *buf, size_t len) {
    char *appdata = getenv("LOCALAPPDATA");
    if (appdata) {
        snprintf(buf, len, "%s\\Atomic", appdata);
    } else {
        /* Fallback */
        char *userprofile = getenv("USERPROFILE");
        snprintf(buf, len, "%s\\AppData\\Local\\Atomic", userprofile ? userprofile : "C:\\Atomic");
    }
}

/* Adiciona diretorio ao PATH do usuario (sem precisar de admin) */
void add_to_user_path(const char *dir) {
    print_step("Adicionando ao PATH do usuario...");

    char ps_cmd[2048];
    snprintf(ps_cmd, sizeof(ps_cmd),
        "powershell -command \""
        "$p = [Environment]::GetEnvironmentVariable('Path','User');"
        "if ($p -notlike '*%s*') {"
        "  [Environment]::SetEnvironmentVariable('Path', $p + ';%s', 'User');"
        "  Write-Host '  Adicionado ao PATH.'"
        "} else {"
        "  Write-Host '  Ja esta no PATH.'"
        "}\"",
        dir, dir);
    run(ps_cmd);
}

/* Remove diretorio do PATH do usuario */
void remove_from_user_path(const char *dir) {
    char ps_cmd[2048];
    snprintf(ps_cmd, sizeof(ps_cmd),
        "powershell -command \""
        "$p = [Environment]::GetEnvironmentVariable('Path','User');"
        "$p = ($p.Split(';') | Where-Object { $_ -ne '%s' }) -join ';';"
        "[Environment]::SetEnvironmentVariable('Path', $p, 'User');"
        "Write-Host '  Removido do PATH.'\"",
        dir);
    run(ps_cmd);
}

void install_windows() {
    char install_dir[512];
    get_install_dir(install_dir, sizeof(install_dir));

    char bin_dir[512];
    snprintf(bin_dir, sizeof(bin_dir), "%s\\bin", install_dir);

    char libs_dir[512];
    snprintf(libs_dir, sizeof(libs_dir), "%s\\libs", install_dir);

    print_header("Instalador Atomic - Windows");
    printf("  Diretorio: %s\n\n", install_dir);

    /* --- Buscar ultima tag --- */
    print_step("Buscando ultima versao no GitHub...");

    char ps_cmd[4096];
    snprintf(ps_cmd, sizeof(ps_cmd),
        "powershell -command \""
        "[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12;"
        "$headers = @{'User-Agent'='AtomicInstaller'};"
        "try {"
        "  $tags = Invoke-RestMethod '%s' -Headers $headers;"
        "  if ($tags -and $tags.Count -gt 0) {"
        "    $tag = $tags[0].name;"
        "    Write-Host \\\"Versao: $tag\\\";"
        "    $url = '%s' + $tag + '.zip'"
        "  } else {"
        "    Write-Host 'Nenhuma tag encontrada, usando branch main...';"
        "    $url = '%s'"
        "  }"
        "} catch {"
        "  Write-Host 'Erro ao buscar tags, usando branch main...';"
        "  $url = '%s'"
        "};"
        "Write-Host \\\"Baixando: $url\\\";"
        "Invoke-WebRequest -Uri $url -OutFile $env:TEMP\\atomic_latest.zip -Headers $headers"
        "\"",
        GITHUB_TAGS_URL, GITHUB_ZIP_BASE, GITHUB_MAIN_ZIP, GITHUB_MAIN_ZIP);
    run(ps_cmd);

    /* --- Extrair --- */
    print_step("Extraindo...");
    system("rmdir /S /Q \"%TEMP%\\atomic_temp_install\" 2>nul");
    run("powershell -command \"Expand-Archive -Force $env:TEMP\\atomic_latest.zip $env:TEMP\\atomic_temp_install\"");

    /* --- Limpar versao anterior --- */
    print_step("Removendo versao anterior...");
    char rm_cmd[512];
    snprintf(rm_cmd, sizeof(rm_cmd), "rmdir /S /Q \"%s\" 2>nul", install_dir);
    system(rm_cmd);

    /* --- Criar estrutura --- */
    print_step("Criando estrutura de diretorios...");
    char mkdir_cmd[512];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir \"%s\" 2>nul", install_dir);
    system(mkdir_cmd);
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir \"%s\" 2>nul", bin_dir);
    system(mkdir_cmd);
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir \"%s\" 2>nul", libs_dir);
    system(mkdir_cmd);

    /* --- Copiar arquivos --- */
    print_step("Copiando arquivos...");
    snprintf(ps_cmd, sizeof(ps_cmd),
        "powershell -command \""
        "$sub = Get-ChildItem $env:TEMP\\atomic_temp_install | Where-Object { $_.PSIsContainer } | Select-Object -First 1;"
        "if ($sub) { xcopy /E /Y ($sub.FullName + '\\*') '%s\\' }"
        "else { xcopy /E /Y $env:TEMP\\atomic_temp_install\\* '%s\\' }"
        "\"",
        install_dir, install_dir);
    run(ps_cmd);

    /* --- Criar launcher atom.cmd em bin/ --- */
    print_step("Criando launcher...");
    char launcher_path[512];
    snprintf(launcher_path, sizeof(launcher_path), "%s\\atom.cmd", bin_dir);

    FILE *launcher = fopen(launcher_path, "w");
    if (launcher) {
        fprintf(launcher,
            "@echo off\n"
            "setlocal\n"
            "\n"
            "set \"ATOM_HOME=%s\"\n"
            "set \"EXE=%%ATOM_HOME%%\\atom.exe\"\n"
            "\n"
            "if \"%%1\"==\"desinstalar\" (\n"
            "    call \"%%ATOM_HOME%%\\desinstalar.cmd\"\n"
            "    exit /b\n"
            ")\n"
            "\n"
            "if not exist \"%%EXE%%\" (\n"
            "    echo Erro: atom.exe nao encontrado em %%ATOM_HOME%%\n"
            "    exit /b 1\n"
            ")\n"
            "\n"
            "\"%%EXE%%\" %%*\n",
            install_dir);
        fclose(launcher);
    }

    /* --- Criar desinstalador --- */
    print_step("Criando desinstalador...");
    char uninstall_path[512];
    snprintf(uninstall_path, sizeof(uninstall_path), "%s\\desinstalar.cmd", install_dir);

    FILE *uninstall = fopen(uninstall_path, "w");
    if (uninstall) {
        fprintf(uninstall,
            "@echo off\n"
            "echo Desinstalando Atomic...\n"
            "\n"
            "REM Remover do PATH do usuario\n"
            "powershell -command \""
            "$p = [Environment]::GetEnvironmentVariable('Path','User');"
            "$p = ($p.Split(';') | Where-Object { $_ -ne '%s' }) -join ';';"
            "[Environment]::SetEnvironmentVariable('Path', $p, 'User')\"\n"
            "\n"
            "REM Remover arquivos\n"
            "rmdir /S /Q \"%s\"\n"
            "\n"
            "echo Atomic desinstalado com sucesso.\n"
            "pause\n",
            bin_dir, install_dir);
        fclose(uninstall);
    }

    /* --- Adicionar bin/ ao PATH --- */
    add_to_user_path(bin_dir);

    /* --- Limpeza --- */
    print_step("Limpando arquivos temporarios...");
    system("del \"%TEMP%\\atomic_latest.zip\" 2>nul");
    system("rmdir /S /Q \"%TEMP%\\atomic_temp_install\" 2>nul");
}

#endif /* _WIN32 */

/* ============================================================================ */
/* Linux                                                                        */
/* ============================================================================ */

#ifndef _WIN32

/* Retorna o caminho de instalacao: ~/.atomic */
void get_install_dir(char *buf, size_t len) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }
    snprintf(buf, len, "%s/.atomic", home);
}

/* Adiciona ao PATH via shell profile */
void add_to_shell_path(const char *dir) {
    print_step("Configurando PATH...");

    const char *home = getenv("HOME");
    if (!home) return;

    /* Detectar qual shell o usuario usa */
    const char *shell = getenv("SHELL");
    const char *rc_files[] = { NULL, NULL, NULL };
    int rc_count = 0;

    char bashrc[512], zshrc[512], profile[512];
    snprintf(bashrc, sizeof(bashrc), "%s/.bashrc", home);
    snprintf(zshrc, sizeof(zshrc), "%s/.zshrc", home);
    snprintf(profile, sizeof(profile), "%s/.profile", home);

    if (shell && strstr(shell, "zsh")) {
        rc_files[rc_count++] = zshrc;
    } else {
        rc_files[rc_count++] = bashrc;
    }
    rc_files[rc_count++] = profile;

    char export_line[512];
    snprintf(export_line, sizeof(export_line),
        "export PATH=\"%s/bin:$PATH\"", dir);

    for (int i = 0; i < rc_count; i++) {
        /* Checar se ja existe */
        FILE *check = fopen(rc_files[i], "r");
        int already_exists = 0;
        if (check) {
            char line[1024];
            while (fgets(line, sizeof(line), check)) {
                if (strstr(line, ".atomic/bin")) {
                    already_exists = 1;
                    break;
                }
            }
            fclose(check);
        }

        if (!already_exists) {
            FILE *f = fopen(rc_files[i], "a");
            if (f) {
                fprintf(f, "\n# Atomic language\n%s\n", export_line);
                fclose(f);
                printf("  Adicionado a %s\n", rc_files[i]);
            }
        } else {
            printf("  Ja configurado em %s\n", rc_files[i]);
        }
    }
}

void install_linux() {
    char install_dir[512];
    get_install_dir(install_dir, sizeof(install_dir));

    char bin_dir[512];
    snprintf(bin_dir, sizeof(bin_dir), "%s/bin", install_dir);

    char libs_dir[512];
    snprintf(libs_dir, sizeof(libs_dir), "%s/libs", install_dir);

    print_header("Instalador Atomic - Linux");
    printf("  Diretorio: %s\n\n", install_dir);

    /* --- Buscar ultima tag --- */
    print_step("Buscando ultima versao no GitHub...");

    char download_cmd[2048];
    snprintf(download_cmd, sizeof(download_cmd),
        "TAG=$(curl -s -H 'User-Agent: AtomicInstaller' %s | grep '\"name\"' | head -1 | cut -d '\"' -f 4) && "
        "if [ -n \"$TAG\" ]; then "
        "  echo \"  Versao: $TAG\" && "
        "  curl -L -H 'User-Agent: AtomicInstaller' -o /tmp/atomic_latest.zip \"%s${TAG}.zip\"; "
        "else "
        "  echo '  Nenhuma tag encontrada, usando branch main...' && "
        "  curl -L -H 'User-Agent: AtomicInstaller' -o /tmp/atomic_latest.zip '%s'; "
        "fi",
        GITHUB_TAGS_URL, GITHUB_ZIP_BASE, GITHUB_MAIN_ZIP);
    run(download_cmd);

    /* --- Extrair --- */
    print_step("Extraindo...");
    run("rm -rf /tmp/atomic_temp_install");
    run("mkdir -p /tmp/atomic_temp_install");
    run("unzip -o /tmp/atomic_latest.zip -d /tmp/atomic_temp_install");

    /* --- Limpar versao anterior --- */
    print_step("Removendo versao anterior...");
    char rm_cmd[512];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf \"%s\"", install_dir);
    run(rm_cmd);

    /* --- Criar estrutura --- */
    print_step("Criando estrutura de diretorios...");
    char mkdir_cmd[512];
    snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p \"%s\" \"%s\" \"%s\"",
        install_dir, bin_dir, libs_dir);
    run(mkdir_cmd);

    /* --- Copiar arquivos --- */
    print_step("Copiando arquivos...");
    char copy_cmd[1024];
    snprintf(copy_cmd, sizeof(copy_cmd),
        "SUB=$(ls -d /tmp/atomic_temp_install/*/ 2>/dev/null | head -1) && "
        "if [ -n \"$SUB\" ]; then cp -r \"$SUB\"* \"%s/\"; "
        "else cp -r /tmp/atomic_temp_install/* \"%s/\"; fi",
        install_dir, install_dir);
    run(copy_cmd);

    /* --- Permissoes --- */
    print_step("Configurando permissoes...");
    char chmod_cmd[512];

    /* Executavel principal */
    snprintf(chmod_cmd, sizeof(chmod_cmd), "chmod +x \"%s/atom\" 2>/dev/null", install_dir);
    system(chmod_cmd);
    snprintf(chmod_cmd, sizeof(chmod_cmd), "chmod +x \"%s/atom.elf\" 2>/dev/null", install_dir);
    system(chmod_cmd);

    /* --- Criar launcher em bin/ --- */
    print_step("Criando launcher...");
    char launcher_path[512];
    snprintf(launcher_path, sizeof(launcher_path), "%s/atom", bin_dir);

    FILE *launcher = fopen(launcher_path, "w");
    if (launcher) {
        fprintf(launcher,
            "#!/bin/bash\n"
            "ATOM_HOME=\"%s\"\n"
            "\n"
            "if [ -f \"$ATOM_HOME/atom.elf\" ]; then\n"
            "    ATOM_BIN=\"$ATOM_HOME/atom.elf\"\n"
            "else\n"
            "    ATOM_BIN=\"$ATOM_HOME/atom\"\n"
            "fi\n"
            "\n"
            "if [[ \"$1\" == \"desinstalar\" ]]; then\n"
            "    bash \"$ATOM_HOME/desinstalar.sh\"\n"
            "    exit 0\n"
            "fi\n"
            "\n"
            "exec \"$ATOM_BIN\" \"$@\"\n",
            install_dir);
        fclose(launcher);
        snprintf(chmod_cmd, sizeof(chmod_cmd), "chmod +x \"%s\"", launcher_path);
        run(chmod_cmd);
    }

    /* --- Criar desinstalador --- */
    print_step("Criando desinstalador...");
    char uninstall_path[512];
    snprintf(uninstall_path, sizeof(uninstall_path), "%s/desinstalar.sh", install_dir);

    FILE *uninstall = fopen(uninstall_path, "w");
    if (uninstall) {
        fprintf(uninstall,
            "#!/bin/bash\n"
            "echo \"Desinstalando Atomic...\"\n"
            "rm -rf \"%s\"\n"
            "\n"
            "# Limpar PATH dos shell configs\n"
            "for rc in ~/.bashrc ~/.zshrc ~/.profile; do\n"
            "    if [ -f \"$rc\" ]; then\n"
            "        sed -i '/\\.atomic\\/bin/d' \"$rc\"\n"
            "        sed -i '/# Atomic language/d' \"$rc\"\n"
            "    fi\n"
            "done\n"
            "\n"
            "echo \"Atomic desinstalado com sucesso.\"\n",
            install_dir);
        fclose(uninstall);
        snprintf(chmod_cmd, sizeof(chmod_cmd), "chmod +x \"%s\"", uninstall_path);
        run(chmod_cmd);
    }

    /* --- Adicionar ao PATH --- */
    add_to_shell_path(install_dir);

    /* --- Limpeza --- */
    print_step("Limpando arquivos temporarios...");
    run("rm -f /tmp/atomic_latest.zip");
    run("rm -rf /tmp/atomic_temp_install");
}

#endif /* !_WIN32 */

/* ============================================================================ */
/* Main                                                                         */
/* ============================================================================ */

int main() {
    print_header("Atomic Language Installer");

    #ifdef _WIN32
        install_windows();
    #else
        install_linux();
    #endif

    char install_dir[512];
    get_install_dir(install_dir, sizeof(install_dir));

    printf("\n");
    print_header("Instalacao concluida!");
    printf("  Diretorio: %s\n", install_dir);
    printf("  Bibliotecas: %s%slibs\n", install_dir, PATH_SEP);
    printf("\n");
    printf("  Para usar:\n");
    printf("    atom meu_programa.at\n");
    printf("\n");
    printf("  Para desinstalar:\n");
    #ifdef _WIN32
    printf("    atom desinstalar\n");
    #else
    printf("    atom desinstalar\n");
    printf("    (ou bash ~/.atomic/desinstalar.sh)\n");
    #endif
    printf("\n");
    printf("  IMPORTANTE: Abra um novo terminal para o comando 'atom' funcionar.\n");
    printf("\n");

    #ifdef _WIN32
        system("pause");
    #endif

    return 0;
}
// main.cpp
// Entry point for the Atomic compiler - reads .at source, tokenizes and parses

#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include "codegen.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <cstdlib>

namespace fs = std::filesystem;

// ============================================================================
// AST Printer (debug)
// ============================================================================

class ASTPrinter {
public:
    void print_program(const atomic::Program& prog) {
        std::cout << "=== Atomic AST: " << prog.filename << " ===\n\n";
        for (auto& stmt : prog.statements) {
            print_stmt(*stmt, 0);
        }
    }

private:
    void indent(int depth) {
        for (int i = 0; i < depth; i++) std::cout << "  ";
    }

    void print_stmt(const atomic::Stmt& stmt, int depth) {
        std::visit([&](auto& s) { print_stmt_impl(s, depth, stmt.loc); }, stmt.data);
    }

    void print_expr(const atomic::Expr& expr, int depth) {
        std::visit([&](auto& e) { print_expr_impl(e, depth, expr.loc); }, expr.data);
    }

    // --- Statements ---

    void print_stmt_impl(const atomic::ExprStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "ExprStmt:\n";
        if (s.expr) print_expr(*s.expr, depth + 1);
    }

    void print_stmt_impl(const atomic::VarDeclStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth);
        std::cout << (s.is_const ? "ConstDecl" : "VarDecl") << ": " << s.name << "\n";
        if (s.type_expr) {
            indent(depth + 1); std::cout << "type:\n";
            print_expr(*s.type_expr, depth + 2);
        }
        if (s.initializer) {
            indent(depth + 1); std::cout << "init:\n";
            print_expr(*s.initializer, depth + 2);
        }
    }

    void print_stmt_impl(const atomic::FnDeclStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth);
        std::cout << "FnDecl: ";
        if (s.is_pub) std::cout << "pub ";
        if (s.is_async) std::cout << "async ";
        std::cout << s.name;
        if (!s.generic_params.empty()) {
            std::cout << "[";
            for (size_t i = 0; i < s.generic_params.size(); i++) {
                if (i > 0) std::cout << ", ";
                std::cout << s.generic_params[i];
            }
            std::cout << "]";
        }
        std::cout << "(";
        for (size_t i = 0; i < s.params.size(); i++) {
            if (i > 0) std::cout << ", ";
            auto& p = s.params[i];
            if (p.is_self) {
                if (p.is_mut_self) std::cout << "ref mut self";
                else std::cout << "self";
            } else {
                std::cout << p.name;
            }
        }
        std::cout << ")\n";
        if (s.return_type) {
            indent(depth + 1); std::cout << "returns:\n";
            print_expr(*s.return_type, depth + 2);
        }
        indent(depth + 1); std::cout << "body:\n";
        for (auto& st : s.body) print_stmt(*st, depth + 2);
    }

    void print_stmt_impl(const atomic::ReturnStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Return\n";
        if (s.value) print_expr(*s.value, depth + 1);
    }

    void print_stmt_impl(const atomic::IfStmt& s, int depth, atomic::SourceLocation loc) {
        for (size_t i = 0; i < s.branches.size(); i++) {
            auto& br = s.branches[i];
            indent(depth);
            if (i == 0) std::cout << "If:\n";
            else if (br.condition) std::cout << "Elif:\n";
            else std::cout << "Else:\n";

            if (br.condition) {
                indent(depth + 1); std::cout << "cond:\n";
                print_expr(*br.condition, depth + 2);
            }
            indent(depth + 1); std::cout << "body:\n";
            for (auto& st : br.body) print_stmt(*st, depth + 2);
        }
    }

    void print_stmt_impl(const atomic::ForStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "For: " << s.var_name;
        if (!s.second_var.empty()) std::cout << ", " << s.second_var;
        std::cout << "\n";
        indent(depth + 1); std::cout << "in:\n";
        print_expr(*s.iterable, depth + 2);
        indent(depth + 1); std::cout << "body:\n";
        for (auto& st : s.body) print_stmt(*st, depth + 2);
    }

    void print_stmt_impl(const atomic::WhileStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "While:\n";
        indent(depth + 1); std::cout << "cond:\n";
        print_expr(*s.condition, depth + 2);
        indent(depth + 1); std::cout << "body:\n";
        for (auto& st : s.body) print_stmt(*st, depth + 2);
    }

    void print_stmt_impl(const atomic::LoopStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Loop:\n";
        for (auto& st : s.body) print_stmt(*st, depth + 2);
    }

    void print_stmt_impl(const atomic::BreakStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Break\n";
        if (s.value) print_expr(*s.value, depth + 1);
    }

    void print_stmt_impl(const atomic::ContinueStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Continue\n";
    }

    void print_stmt_impl(const atomic::MatchStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Match:\n";
        indent(depth + 1); std::cout << "subject:\n";
        print_expr(*s.subject, depth + 2);
        for (auto& arm : s.arms) {
            indent(depth + 1); std::cout << "arm:\n";
            indent(depth + 2); std::cout << "pattern:\n";
            print_expr(*arm.pattern, depth + 3);
            if (arm.guard) {
                indent(depth + 2); std::cout << "guard:\n";
                print_expr(*arm.guard, depth + 3);
            }
            indent(depth + 2); std::cout << "body:\n";
            for (auto& st : arm.body) print_stmt(*st, depth + 3);
        }
    }

    void print_stmt_impl(const atomic::StructDeclStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth);
        if (s.is_pub) std::cout << "pub ";
        std::cout << "Struct: " << s.name << "\n";
        for (auto& f : s.fields) {
            indent(depth + 1);
            if (f.is_pub) std::cout << "pub ";
            std::cout << f.name << ": ";
            if (f.type_expr) print_expr(*f.type_expr, 0);
            std::cout << "\n";
        }
    }

    void print_stmt_impl(const atomic::EnumDeclStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth);
        if (s.is_pub) std::cout << "pub ";
        std::cout << "Enum: " << s.name << "\n";
        for (auto& v : s.variants) {
            indent(depth + 1); std::cout << v.name;
            if (!v.fields.empty()) std::cout << "(...)";
            std::cout << "\n";
        }
    }

    void print_stmt_impl(const atomic::ImplBlock& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Impl: ";
        if (!s.trait_name.empty()) std::cout << s.trait_name << " for ";
        std::cout << s.target_type << "\n";
        for (auto& m : s.methods) print_stmt(*m, depth + 1);
    }

    void print_stmt_impl(const atomic::TraitDeclStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth);
        if (s.is_pub) std::cout << "pub ";
        std::cout << "Trait: " << s.name << "\n";
        for (auto& m : s.methods) {
            indent(depth + 1); std::cout << "fn " << m.name << "\n";
        }
    }

    void print_stmt_impl(const atomic::ImportStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth);
        if (s.is_extern) std::cout << "ImportExtern: " << s.module_path << "\n";
        else if (s.is_wildcard) std::cout << "FromImport: " << s.module_path << " import *\n";
        else if (s.items.empty()) std::cout << "Import: " << s.module_path << "\n";
        else {
            std::cout << "FromImport: " << s.module_path << " import ";
            for (size_t i = 0; i < s.items.size(); i++) {
                if (i > 0) std::cout << ", ";
                std::cout << s.items[i].first;
                if (s.items[i].first != s.items[i].second)
                    std::cout << " as " << s.items[i].second;
            }
            std::cout << "\n";
        }
    }

    void print_stmt_impl(const atomic::ModDeclStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Mod: " << s.name << "\n";
    }

    void print_stmt_impl(const atomic::TryStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Try:\n";
        for (auto& st : s.body) print_stmt(*st, depth + 1);
        for (auto& c : s.catches) {
            indent(depth); std::cout << "Catch";
            if (!c.error_type.empty()) std::cout << " " << c.error_type;
            if (!c.var_name.empty()) std::cout << " as " << c.var_name;
            std::cout << ":\n";
            for (auto& st : c.body) print_stmt(*st, depth + 1);
        }
    }

    void print_stmt_impl(const atomic::DeferStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Defer:\n";
        if (s.expr) print_expr(*s.expr, depth + 1);
    }

    void print_stmt_impl(const atomic::DropStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Drop:\n";
        if (s.expr) print_expr(*s.expr, depth + 1);
    }

    void print_stmt_impl(const atomic::UnsafeBlock& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Unsafe:\n";
        for (auto& st : s.body) print_stmt(*st, depth + 1);
    }

    void print_stmt_impl(const atomic::TypeAliasStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "TypeAlias: " << s.name << "\n";
        if (s.type_expr) print_expr(*s.type_expr, depth + 1);
    }

    void print_stmt_impl(const atomic::DeriveStmt& s, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Derive: ";
        for (size_t i = 0; i < s.traits.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << s.traits[i];
        }
        std::cout << "\n";
        if (s.target) print_stmt(*s.target, depth + 1);
    }

    // --- Expressions ---

    void print_expr_impl(const atomic::IntLiteralExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Int(" << e.value << ")\n";
    }

    void print_expr_impl(const atomic::FloatLiteralExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Float(" << e.value << ")\n";
    }

    void print_expr_impl(const atomic::StringLiteralExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "String(\"" << e.value << "\")\n";
    }

    void print_expr_impl(const atomic::InterpolatedStringExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "InterpolatedString[" << e.parts.size() << " parts]\n";
        for (auto& part : e.parts) print_expr(*part, depth + 1);
    }

    void print_expr_impl(const atomic::CharLiteralExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Char('" << e.value << "')\n";
    }

    void print_expr_impl(const atomic::BoolLiteralExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Bool(" << (e.value ? "true" : "false") << ")\n";
    }

    void print_expr_impl(const atomic::NilLiteralExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Nil\n";
    }

    void print_expr_impl(const atomic::IdentifierExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Ident(" << e.name << ")\n";
    }

    void print_expr_impl(const atomic::UnaryExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Unary(" << atomic::token_type_str(e.op) << ")\n";
        if (e.operand) print_expr(*e.operand, depth + 1);
    }

    void print_expr_impl(const atomic::BinaryExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Binary(" << atomic::token_type_str(e.op) << ")\n";
        if (e.left) print_expr(*e.left, depth + 1);
        if (e.right) print_expr(*e.right, depth + 1);
    }

    void print_expr_impl(const atomic::CallExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Call:\n";
        indent(depth + 1); std::cout << "callee:\n";
        if (e.callee) print_expr(*e.callee, depth + 2);
        if (!e.args.empty()) {
            indent(depth + 1); std::cout << "args:\n";
            for (auto& a : e.args) print_expr(*a, depth + 2);
        }
        if (!e.named_args.empty()) {
            indent(depth + 1); std::cout << "named_args:\n";
            for (auto& [n, v] : e.named_args) {
                indent(depth + 2); std::cout << n << "=\n";
                print_expr(*v, depth + 3);
            }
        }
    }

    void print_expr_impl(const atomic::IndexExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Index:\n";
        if (e.object) print_expr(*e.object, depth + 1);
        if (e.index) print_expr(*e.index, depth + 1);
    }

    void print_expr_impl(const atomic::SliceExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Slice:\n";
        if (e.object) print_expr(*e.object, depth + 1);
        if (e.start) { indent(depth + 1); std::cout << "start:\n"; print_expr(*e.start, depth + 2); }
        if (e.end) { indent(depth + 1); std::cout << "end:\n"; print_expr(*e.end, depth + 2); }
    }

    void print_expr_impl(const atomic::MemberExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Member(." << e.member << ")\n";
        if (e.object) print_expr(*e.object, depth + 1);
    }

    void print_expr_impl(const atomic::ScopeResolutionExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Scope(::" << e.member << ")\n";
        if (e.object) print_expr(*e.object, depth + 1);
    }

    void print_expr_impl(const atomic::AssignExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Assign(" << atomic::token_type_str(e.op) << ")\n";
        if (e.target) print_expr(*e.target, depth + 1);
        if (e.value) print_expr(*e.value, depth + 1);
    }

    void print_expr_impl(const atomic::IfExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "IfExpr:\n";
        if (e.condition) { indent(depth + 1); std::cout << "cond:\n"; print_expr(*e.condition, depth + 2); }
        if (e.then_expr) { indent(depth + 1); std::cout << "then:\n"; print_expr(*e.then_expr, depth + 2); }
        if (e.else_expr) { indent(depth + 1); std::cout << "else:\n"; print_expr(*e.else_expr, depth + 2); }
    }

    void print_expr_impl(const atomic::MatchExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "MatchExpr:\n";
        if (e.subject) print_expr(*e.subject, depth + 1);
    }

    void print_expr_impl(const atomic::LambdaExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Lambda(";
        for (size_t i = 0; i < e.params.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << e.params[i].name;
        }
        std::cout << ")\n";
    }

    void print_expr_impl(const atomic::ListExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "List[" << e.elements.size() << "]\n";
        for (auto& el : e.elements) print_expr(*el, depth + 1);
    }

    void print_expr_impl(const atomic::MapExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Map{" << e.entries.size() << "}\n";
    }

    void print_expr_impl(const atomic::SetExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Set{" << e.elements.size() << "}\n";
    }

    void print_expr_impl(const atomic::TupleExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Tuple(" << e.elements.size() << ")\n";
        for (auto& el : e.elements) print_expr(*el, depth + 1);
    }

    void print_expr_impl(const atomic::RangeExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Range(" << (e.inclusive ? "..=" : "..") << ")\n";
        if (e.start) print_expr(*e.start, depth + 1);
        if (e.end) print_expr(*e.end, depth + 1);
    }

    void print_expr_impl(const atomic::ComprehensionExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Comprehension: " << e.var_name << "\n";
    }

    void print_expr_impl(const atomic::PipeExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Pipe(|>)\n";
        if (e.left) print_expr(*e.left, depth + 1);
        if (e.right) print_expr(*e.right, depth + 1);
    }

    void print_expr_impl(const atomic::RefExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Ref(" << (e.is_mut ? "mut" : "immut") << ")\n";
        if (e.operand) print_expr(*e.operand, depth + 1);
    }

    void print_expr_impl(const atomic::CastExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Cast\n";
    }

    void print_expr_impl(const atomic::AwaitExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Await\n";
        if (e.operand) print_expr(*e.operand, depth + 1);
    }

    void print_expr_impl(const atomic::QuestionExpr& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Question(?)\n";
        if (e.operand) print_expr(*e.operand, depth + 1);
    }

    void print_expr_impl(const atomic::TypeAnnotation& e, int depth, atomic::SourceLocation loc) {
        indent(depth); std::cout << "Type(" << e.name;
        if (!e.generics.empty()) std::cout << "[...]";
        std::cout << ")\n";
    }
};

// ============================================================================
// File reading
// ============================================================================

std::string read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "error: could not open file '" << path << "'\n";
        std::exit(1);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

// ============================================================================
// Main
// ============================================================================

void print_usage(const char* program) {
    std::cout << "Atomic Compiler v0.1\n\n";
    std::cout << "Usage:\n";
    std::cout << "  " << program << " <file.at>                Run directly\n";
    std::cout << "  " << program << " build <file.at>          Compile to executable\n";
    std::cout << "  " << program << " build <file.at> -w       Compile without console (windowed)\n";
    std::cout << "  " << program << " check <file.at>          Type check only\n";
    std::cout << "  " << program << " tokens <file.at>         Show lexer output\n";
    std::cout << "  " << program << " ast <file.at>            Show parsed AST\n";
}

int cmd_tokens(const std::string& filepath) {
    auto source = read_file(filepath);
    auto filename = fs::path(filepath).filename().string();

    try {
        atomic::Lexer lexer(source, filename);
        auto tokens = lexer.tokenize();

        std::cout << "=== Tokens: " << filename << " ===\n\n";
        for (auto& tok : tokens) {
            std::cout << std::format("{:4d}:{:<3d}  {:<20s}  {}\n",
                tok.loc.line, tok.loc.column,
                atomic::token_type_str(tok.type),
                tok.value);
        }
        std::cout << "\nTotal: " << tokens.size() << " tokens\n";
    } catch (const atomic::LexerError& e) {
        std::cerr << filename << ":" << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_ast(const std::string& filepath) {
    auto source = read_file(filepath);
    auto filename = fs::path(filepath).filename().string();

    try {
        atomic::Lexer lexer(source, filename);
        auto tokens = lexer.tokenize();

        atomic::Parser parser(std::move(tokens), filename);
        auto program = parser.parse();

        ASTPrinter printer;
        printer.print_program(program);
    } catch (const atomic::LexerError& e) {
        std::cerr << filename << ":" << e.what() << "\n";
        return 1;
    } catch (const atomic::ParseError& e) {
        std::cerr << filename << ":" << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_build(const std::string& filepath, const std::string& output_name, bool windowed = false) {
    auto source = read_file(filepath);
    auto filename = fs::path(filepath).filename().string();
    auto stem = fs::path(filepath).stem().string();
    auto base_dir_raw = fs::path(filepath).parent_path(); auto base_dir = (base_dir_raw.empty() ? fs::current_path() : fs::absolute(base_dir_raw)).string();

    // Gerar na pasta build/<projeto>/<projeto>.exe
    fs::path build_dir = fs::path("build") / stem;
    fs::create_directories(build_dir);

    auto obj_path = (build_dir / (stem + ".obj")).string();
    auto exe_path = output_name.empty() ? (build_dir / (stem + ".exe")).string() : output_name;

    try {
        // Lex
        atomic::Lexer lexer(source, filename);
        auto tokens = lexer.tokenize();

        // Parse
        atomic::Parser parser(std::move(tokens), filename);
        auto program = parser.parse();

        // Semantic analysis
        atomic::Sema sema(program, base_dir);
        if (!sema.analyze()) {
            for (auto& err : sema.errors()) {
                std::cerr << filename << ":" << err.format() << "\n";
            }
            return 1;
        }

        // Codegen
        atomic::CodeGen codegen(program, base_dir);
        if (!codegen.generate(obj_path)) {
            std::cerr << "error: could not write '" << obj_path << "'\n";
            return 1;
        }

        std::cout << "compiled: " << filename << " -> " << obj_path << "\n";

        // Link with gcc — adicionar .dla importadas
        std::string link_cmd = "gcc -o " + exe_path + " " + obj_path + " -lmsvcrt -lkernel32";
        if (windowed) {
            link_cmd += " -mwindows";
        }
        for (auto& dla : codegen.dla_paths()) {
            link_cmd += " \"" + dla + "\"";
        }
        int ret = std::system(link_cmd.c_str());
        if (ret != 0) {
            std::cerr << "error: linking failed\n";
            return 1;
        }

        // Remove .obj
        fs::remove(obj_path);

        // Copiar .dla para o diretorio do exe (necessario em runtime)
        for (auto& dla : codegen.dla_paths()) {
            auto dla_name = fs::path(dla).filename().string();
            auto dest = fs::path(exe_path).parent_path() / dla_name;
            if (fs::path(dla) != dest) {
                fs::copy_file(dla, dest, fs::copy_options::overwrite_existing);
            }
        }

        std::cout << "linked:   " << exe_path << "\n";
    } catch (const atomic::LexerError& e) {
        std::cerr << filename << ":" << e.what() << "\n";
        return 1;
    } catch (const atomic::ParseError& e) {
        std::cerr << filename << ":" << e.what() << "\n";
        return 1;
    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    return 0;
}

int cmd_run(const std::string& filepath) {
    auto source = read_file(filepath);
    auto filename = fs::path(filepath).filename().string();
    auto stem = fs::path(filepath).stem().string();
    auto base_dir_raw = fs::path(filepath).parent_path(); auto base_dir = (base_dir_raw.empty() ? fs::current_path() : fs::absolute(base_dir_raw)).string();

    // Criar pasta temp no diretorio atual
    auto temp_dir = fs::current_path() / ".atomic_temp";
    fs::create_directories(temp_dir);

    auto obj_path = (temp_dir / (stem + ".obj")).string();
    auto exe_path = (temp_dir / (stem + ".exe")).string();

    int ret = 0;
    try {
        // Lex
        atomic::Lexer lexer(source, filename);
        auto tokens = lexer.tokenize();

        // Parse
        atomic::Parser parser(std::move(tokens), filename);
        auto program = parser.parse();

        // Semantic analysis
        atomic::Sema sema(program, base_dir);
        if (!sema.analyze()) {
            for (auto& err : sema.errors()) {
                std::cerr << filename << ":" << err.format() << "\n";
            }
            fs::remove_all(temp_dir);
            return 1;
        }

        // Codegen
        atomic::CodeGen codegen(program, base_dir);
        if (!codegen.generate(obj_path)) {
            std::cerr << "error: could not write '" << obj_path << "'\n";
            fs::remove_all(temp_dir);
            return 1;
        }

        // Link — adicionar .dla importadas
        std::string link_cmd = "gcc -o " + exe_path + " " + obj_path + " -lmsvcrt -lkernel32";
        for (auto& dla : codegen.dla_paths()) {
            link_cmd += " \"" + dla + "\"";
        }
        ret = std::system(link_cmd.c_str());
        if (ret != 0) {
            std::cerr << "error: linking failed\n";
            fs::remove_all(temp_dir);
            return 1;
        }

        // Copiar .dla para o diretorio temp (necessario em runtime)
        for (auto& dla : codegen.dla_paths()) {
            auto dla_name = fs::path(dla).filename().string();
            auto dest = temp_dir / dla_name;
            if (fs::path(dla) != dest) {
                fs::copy_file(dla, dest, fs::copy_options::overwrite_existing);
            }
        }

        // Executar
        ret = std::system(exe_path.c_str());

    } catch (const atomic::LexerError& e) {
        std::cerr << filename << ":" << e.what() << "\n";
        ret = 1;
    } catch (const atomic::ParseError& e) {
        std::cerr << filename << ":" << e.what() << "\n";
        ret = 1;
    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << "\n";
        ret = 1;
    }

    // Limpar pasta temp
    fs::remove_all(temp_dir);

    return ret;
}

int cmd_check(const std::string& filepath) {
    auto source = read_file(filepath);
    auto filename = fs::path(filepath).filename().string();
    auto base_dir_raw = fs::path(filepath).parent_path(); auto base_dir = (base_dir_raw.empty() ? fs::current_path() : fs::absolute(base_dir_raw)).string();

    try {
        atomic::Lexer lexer(source, filename);
        auto tokens = lexer.tokenize();

        atomic::Parser parser(std::move(tokens), filename);
        auto program = parser.parse();

        // Semantic analysis
        atomic::Sema sema(program, base_dir);
        if (!sema.analyze()) {
            for (auto& err : sema.errors()) {
                std::cerr << filename << ":" << err.format() << "\n";
            }
            return 1;
        }

        std::cout << "check: " << filename << " OK\n";
    } catch (const atomic::LexerError& e) {
        std::cerr << filename << ":" << e.what() << "\n";
        return 1;
    } catch (const atomic::ParseError& e) {
        std::cerr << filename << ":" << e.what() << "\n";
        return 1;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string first_arg = argv[1];

    // Se o primeiro argumento e um .at, executa direto (run)
    if (first_arg.ends_with(".at")) {
        if (!fs::exists(first_arg)) {
            std::cerr << "error: file not found '" << first_arg << "'\n";
            return 1;
        }
        return cmd_run(first_arg);
    }

    // Caso contrario, e um comando que precisa de arquivo
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    std::string command = first_arg;
    std::string filepath = argv[2];

    if (!fs::exists(filepath)) {
        std::cerr << "error: file not found '" << filepath << "'\n";
        return 1;
    }

    if (!filepath.ends_with(".at")) {
        std::cerr << "error: expected .at file, got '" << filepath << "'\n";
        return 1;
    }

    if (command == "tokens") {
        return cmd_tokens(filepath);
    } else if (command == "ast") {
        return cmd_ast(filepath);
    } else if (command == "build") {
        bool windowed = false;
        for (int i = 3; i < argc; i++) {
            if (std::string(argv[i]) == "-w") windowed = true;
        }
        return cmd_build(filepath, "", windowed);
    } else if (command == "run") {
        return cmd_run(filepath);
    } else if (command == "check") {
        return cmd_check(filepath);
    } else {
        std::cerr << "error: unknown command '" << command << "'\n";
        print_usage(argv[0]);
        return 1;
    }
}
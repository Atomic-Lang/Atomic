// sema.hpp
// Semantic analyzer for the Atomic programming language — validates AST before codegen

#pragma once

#include "ast.hpp"
#include "lib_loader.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <format>
#include <filesystem>

namespace atomic {

// ============================================================================
// Semantic Error
// ============================================================================

struct SemaError {
    std::string message;
    SourceLocation loc;

    std::string format() const {
        return std::format("{}:{}: {}", loc.line, loc.column, message);
    }
};

// ============================================================================
// Semantic Analyzer
// ============================================================================

class Sema {
public:
    explicit Sema(const Program& program, const std::string& base_dir = ".")
        : m_program(program), m_base_dir(base_dir) {}

    // Retorna true se nao houver erros
    bool analyze() {
        m_errors.clear();
        m_scopes.clear();
        m_functions.clear();
        m_structs.clear();
        m_imported_funcs.clear();

        push_scope();

        // Primeiro passo: coletar declaracoes de funcao (permite forward reference)
        for (auto& stmt : m_program.statements) {
            if (stmt->is<FnDeclStmt>()) {
                auto& fn = stmt->as<FnDeclStmt>();
                m_functions.insert(fn.name);
            }
            if (stmt->is<StructDeclStmt>()) {
                m_structs.insert(stmt->as<StructDeclStmt>().name);
            }
            if (stmt->is<EnumDeclStmt>()) {
                m_structs.insert(stmt->as<EnumDeclStmt>().name);
            }
            // Processar imports no primeiro passo para registrar funcoes da lib
            if (stmt->is<ImportStmt>()) {
                auto& imp = stmt->as<ImportStmt>();
                if (!imp.is_extern && !imp.is_wildcard) {
                    load_lib_functions(imp.module_path, stmt->loc);
                }
            }
        }

        // Segundo passo: analisar todos os statements
        for (auto& stmt : m_program.statements) {
            analyze_stmt(*stmt);
        }

        pop_scope();
        return m_errors.empty();
    }

    const std::vector<SemaError>& errors() const { return m_errors; }

private:
    const Program& m_program;
    std::string m_base_dir;
    std::vector<SemaError> m_errors;

    // Funcoes importadas de bibliotecas .dla
    std::unordered_set<std::string> m_imported_funcs;

    // Escopos: cada escopo e um mapa de nome -> { is_const, loc }
    struct VarEntry {
        bool is_const = false;
        SourceLocation loc;
    };
    std::vector<std::unordered_map<std::string, VarEntry>> m_scopes;

    // Funcoes declaradas (nomes)
    std::unordered_set<std::string> m_functions;

    // Structs e enums declarados
    std::unordered_set<std::string> m_structs;

    // Carrega funcoes de uma biblioteca a partir do JSON
    void load_lib_functions(const std::string& lib_name, SourceLocation loc) {
        try {
            auto desc = LibLoader::load(lib_name, m_base_dir);
            for (auto& fn : desc.funcoes) {
                m_imported_funcs.insert(fn.nome);
            }
        } catch (const std::runtime_error& e) {
            error(loc, e.what());
        }
    }

    // Builtins que nao precisam de declaracao
    bool is_known_fn(const std::string& name) const {
        static const std::unordered_set<std::string> builtins = {
            "print", "println", "printl", "input", "len", "str", "int", "float",
            "print_y", "print_r", "print_b", "print_g",
            "printl_y", "printl_r", "printl_b", "printl_g",
            "bool", "type", "range", "enumerate", "zip", "map", "filter",
            "reduce", "sorted", "reversed", "min", "max", "sum", "abs",
            "round", "chr", "ord", "hex", "oct", "bin",
            "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
            "f32", "f64", "push", "pop", "append", "insert", "remove",
            "exit", "panic", "assert", "sizeof", "typeof",
            "malloc", "free", "realloc",
            "printf", "sprintf", "strlen", "strcpy", "strcmp",
        };
        return builtins.count(name) > 0 || m_imported_funcs.count(name) > 0;
    }

    // ========================================================================
    // Escopo
    // ========================================================================

    void push_scope() {
        m_scopes.push_back({});
    }

    void pop_scope() {
        m_scopes.pop_back();
    }

    // Declara variavel no escopo atual
    void declare_var(const std::string& name, bool is_const, SourceLocation loc) {
        auto& scope = m_scopes.back();
        // Permitir redeclaracao no mesmo escopo (shadowing)
        scope[name] = VarEntry{is_const, loc};
    }

    // Busca variavel em todos os escopos (do mais interno ao mais externo)
    VarEntry* find_var(const std::string& name) {
        for (int i = static_cast<int>(m_scopes.size()) - 1; i >= 0; i--) {
            auto it = m_scopes[i].find(name);
            if (it != m_scopes[i].end()) return &it->second;
        }
        return nullptr;
    }

    // ========================================================================
    // Erro
    // ========================================================================

    void error(SourceLocation loc, const std::string& msg) {
        m_errors.push_back({msg, loc});
    }

    // ========================================================================
    // Analise de Statements
    // ========================================================================

    void analyze_stmt(const Stmt& stmt) {
        std::visit([&](auto& s) { analyze_stmt_impl(s, stmt.loc); }, stmt.data);
    }

    void analyze_stmt_impl(const ExprStmt& s, SourceLocation) {
        if (s.expr) analyze_expr(*s.expr);
    }

    void analyze_stmt_impl(const VarDeclStmt& s, SourceLocation loc) {
        if (s.type_expr) analyze_expr(*s.type_expr);
        if (s.initializer) analyze_expr(*s.initializer);

        // Verificar atribuicao a const existente
        auto* existing = find_var(s.name);
        if (existing && existing->is_const && !s.is_const) {
            error(loc, std::format("cannot reassign constant '{}'", s.name));
        }

        declare_var(s.name, s.is_const, loc);
    }

    void analyze_stmt_impl(const FnDeclStmt& s, SourceLocation) {
        m_functions.insert(s.name);

        push_scope();

        // Declarar parametros no escopo da funcao
        for (auto& p : s.params) {
            if (!p.name.empty() && !p.is_self) {
                declare_var(p.name, false, {});
            }
            if (p.type_expr) analyze_expr(*p.type_expr);
            if (p.default_value) analyze_expr(*p.default_value);
        }

        if (s.return_type) analyze_expr(*s.return_type);

        for (auto& st : s.body) {
            analyze_stmt(*st);
        }

        pop_scope();
    }

    void analyze_stmt_impl(const ReturnStmt& s, SourceLocation) {
        if (s.value) analyze_expr(*s.value);
    }

    void analyze_stmt_impl(const IfStmt& s, SourceLocation) {
        for (auto& br : s.branches) {
            if (br.condition) analyze_expr(*br.condition);
            push_scope();
            for (auto& st : br.body) analyze_stmt(*st);
            pop_scope();
        }
    }

    void analyze_stmt_impl(const ForStmt& s, SourceLocation) {
        if (s.iterable) analyze_expr(*s.iterable);

        push_scope();
        declare_var(s.var_name, false, {});
        if (!s.second_var.empty()) {
            declare_var(s.second_var, false, {});
        }
        for (auto& st : s.body) analyze_stmt(*st);
        pop_scope();
    }

    void analyze_stmt_impl(const WhileStmt& s, SourceLocation) {
        if (s.condition) analyze_expr(*s.condition);
        push_scope();
        for (auto& st : s.body) analyze_stmt(*st);
        pop_scope();
    }

    void analyze_stmt_impl(const LoopStmt& s, SourceLocation) {
        push_scope();
        for (auto& st : s.body) analyze_stmt(*st);
        pop_scope();
    }

    void analyze_stmt_impl(const BreakStmt& s, SourceLocation) {
        if (s.value) analyze_expr(*s.value);
    }

    void analyze_stmt_impl(const ContinueStmt&, SourceLocation) {}

    void analyze_stmt_impl(const MatchStmt& s, SourceLocation) {
        if (s.subject) analyze_expr(*s.subject);
        for (auto& arm : s.arms) {
            if (arm.pattern) analyze_expr(*arm.pattern);
            if (arm.guard) analyze_expr(*arm.guard);
            push_scope();
            for (auto& st : arm.body) analyze_stmt(*st);
            pop_scope();
        }
    }

    void analyze_stmt_impl(const StructDeclStmt& s, SourceLocation) {
        m_structs.insert(s.name);
        for (auto& f : s.fields) {
            if (f.type_expr) analyze_expr(*f.type_expr);
        }
    }

    void analyze_stmt_impl(const EnumDeclStmt& s, SourceLocation) {
        m_structs.insert(s.name);
    }

    void analyze_stmt_impl(const ImplBlock& s, SourceLocation) {
        for (auto& m : s.methods) analyze_stmt(*m);
    }

    void analyze_stmt_impl(const TraitDeclStmt& s, SourceLocation) {
        for (auto& m : s.methods) {
            push_scope();
            for (auto& p : m.params) {
                if (!p.name.empty() && !p.is_self) {
                    declare_var(p.name, false, {});
                }
            }
            for (auto& st : m.default_body) analyze_stmt(*st);
            pop_scope();
        }
    }

    void analyze_stmt_impl(const ImportStmt&, SourceLocation) {}
    void analyze_stmt_impl(const ModDeclStmt&, SourceLocation) {}

    void analyze_stmt_impl(const TryStmt& s, SourceLocation) {
        push_scope();
        for (auto& st : s.body) analyze_stmt(*st);
        pop_scope();

        for (auto& c : s.catches) {
            push_scope();
            if (!c.var_name.empty()) {
                declare_var(c.var_name, false, {});
            }
            for (auto& st : c.body) analyze_stmt(*st);
            pop_scope();
        }
    }

    void analyze_stmt_impl(const DeferStmt& s, SourceLocation) {
        if (s.expr) analyze_expr(*s.expr);
    }

    void analyze_stmt_impl(const DropStmt& s, SourceLocation) {
        if (s.expr) analyze_expr(*s.expr);
    }

    void analyze_stmt_impl(const UnsafeBlock& s, SourceLocation) {
        push_scope();
        for (auto& st : s.body) analyze_stmt(*st);
        pop_scope();
    }

    void analyze_stmt_impl(const TypeAliasStmt& s, SourceLocation) {
        if (s.type_expr) analyze_expr(*s.type_expr);
    }

    void analyze_stmt_impl(const DeriveStmt& s, SourceLocation) {
        if (s.target) analyze_stmt(*s.target);
    }

    // ========================================================================
    // Analise de Expressoes
    // ========================================================================

    void analyze_expr(const Expr& expr) {
        std::visit([&](auto& e) { analyze_expr_impl(e, expr.loc); }, expr.data);
    }

    void analyze_expr_impl(const IdentifierExpr& e, SourceLocation loc) {
        // Verificar se a variavel existe
        if (!find_var(e.name) &&
            !m_functions.count(e.name) &&
            !m_structs.count(e.name) &&
            !is_known_fn(e.name) &&
            e.name != "self" && e.name != "Self") {
            error(loc, std::format("undefined variable '{}'", e.name));
        }
    }

    void analyze_expr_impl(const BinaryExpr& e, SourceLocation) {
        if (e.left) analyze_expr(*e.left);
        if (e.right) analyze_expr(*e.right);
    }

    void analyze_expr_impl(const UnaryExpr& e, SourceLocation) {
        if (e.operand) analyze_expr(*e.operand);
    }

    void analyze_expr_impl(const CallExpr& e, SourceLocation loc) {
        if (e.callee) {
            // Para chamadas de funcao, verificar se o nome existe
            if (e.callee->is<IdentifierExpr>()) {
                auto& name = e.callee->as<IdentifierExpr>().name;
                if (!m_functions.count(name) &&
                    !m_structs.count(name) &&
                    !is_known_fn(name) &&
                    !find_var(name)) {
                    error(loc, std::format("undefined function '{}'", name));
                }
            } else {
                // Chamada a membro, index, etc — analisar normalmente
                analyze_expr(*e.callee);
            }
        }
        for (auto& arg : e.args) analyze_expr(*arg);
        for (auto& [name, val] : e.named_args) {
            if (val) analyze_expr(*val);
        }
    }

    void analyze_expr_impl(const AssignExpr& e, SourceLocation loc) {
        if (e.value) analyze_expr(*e.value);

        if (e.target && e.target->is<IdentifierExpr>()) {
            auto& name = e.target->as<IdentifierExpr>().name;
            auto* var = find_var(name);

            if (var && var->is_const) {
                error(loc, std::format("cannot reassign constant '{}'", name));
            }

            // Se e atribuicao simples e a variavel nao existe, sera criada
            // (o parser transforma isso em VarDeclStmt, mas por seguranca)
            if (!var && e.op != TokenType::Assign) {
                error(loc, std::format("undefined variable '{}'", name));
            }
        } else if (e.target) {
            analyze_expr(*e.target);
        }
    }

    void analyze_expr_impl(const IndexExpr& e, SourceLocation) {
        if (e.object) analyze_expr(*e.object);
        if (e.index) analyze_expr(*e.index);
    }

    void analyze_expr_impl(const SliceExpr& e, SourceLocation) {
        if (e.object) analyze_expr(*e.object);
        if (e.start) analyze_expr(*e.start);
        if (e.end) analyze_expr(*e.end);
    }

    void analyze_expr_impl(const MemberExpr& e, SourceLocation) {
        if (e.object) analyze_expr(*e.object);
    }

    void analyze_expr_impl(const ScopeResolutionExpr& e, SourceLocation) {
        if (e.object) analyze_expr(*e.object);
    }

    void analyze_expr_impl(const IfExpr& e, SourceLocation) {
        if (e.condition) analyze_expr(*e.condition);
        if (e.then_expr) analyze_expr(*e.then_expr);
        if (e.else_expr) analyze_expr(*e.else_expr);
    }

    void analyze_expr_impl(const MatchExpr& e, SourceLocation) {
        if (e.subject) analyze_expr(*e.subject);
        for (auto& arm : e.arms) {
            if (arm.pattern) analyze_expr(*arm.pattern);
            if (arm.guard) analyze_expr(*arm.guard);
            if (arm.body) analyze_expr(*arm.body);
        }
    }

    void analyze_expr_impl(const LambdaExpr& e, SourceLocation) {
        push_scope();
        for (auto& p : e.params) {
            declare_var(p.name, false, {});
            if (p.type_expr) analyze_expr(*p.type_expr);
        }
        if (auto* expr = std::get_if<ExprPtr>(&e.body)) {
            if (*expr) analyze_expr(**expr);
        } else if (auto* stmts = std::get_if<StmtList>(&e.body)) {
            for (auto& st : *stmts) analyze_stmt(*st);
        }
        pop_scope();
    }

    void analyze_expr_impl(const ListExpr& e, SourceLocation) {
        for (auto& el : e.elements) analyze_expr(*el);
    }

    void analyze_expr_impl(const MapExpr& e, SourceLocation) {
        for (auto& entry : e.entries) {
            if (entry.key) analyze_expr(*entry.key);
            if (entry.value) analyze_expr(*entry.value);
        }
    }

    void analyze_expr_impl(const SetExpr& e, SourceLocation) {
        for (auto& el : e.elements) analyze_expr(*el);
    }

    void analyze_expr_impl(const TupleExpr& e, SourceLocation) {
        for (auto& el : e.elements) analyze_expr(*el);
    }

    void analyze_expr_impl(const RangeExpr& e, SourceLocation) {
        if (e.start) analyze_expr(*e.start);
        if (e.end) analyze_expr(*e.end);
    }

    void analyze_expr_impl(const ComprehensionExpr& e, SourceLocation) {
        if (e.iterable) analyze_expr(*e.iterable);
        push_scope();
        declare_var(e.var_name, false, {});
        if (e.element) analyze_expr(*e.element);
        if (e.condition) analyze_expr(*e.condition);
        pop_scope();
    }

    void analyze_expr_impl(const PipeExpr& e, SourceLocation) {
        if (e.left) analyze_expr(*e.left);
        if (e.right) analyze_expr(*e.right);
    }

    void analyze_expr_impl(const RefExpr& e, SourceLocation) {
        if (e.operand) analyze_expr(*e.operand);
    }

    void analyze_expr_impl(const CastExpr& e, SourceLocation) {
        if (e.target_type) analyze_expr(*e.target_type);
        for (auto& arg : e.args) analyze_expr(*arg);
    }

    void analyze_expr_impl(const AwaitExpr& e, SourceLocation) {
        if (e.operand) analyze_expr(*e.operand);
    }

    void analyze_expr_impl(const QuestionExpr& e, SourceLocation) {
        if (e.operand) analyze_expr(*e.operand);
    }

    // Literais e tipos — nada a verificar
    void analyze_expr_impl(const IntLiteralExpr&, SourceLocation) {}
    void analyze_expr_impl(const FloatLiteralExpr&, SourceLocation) {}
    void analyze_expr_impl(const StringLiteralExpr&, SourceLocation) {}
    void analyze_expr_impl(const InterpolatedStringExpr& e, SourceLocation) {
        for (auto& part : e.parts) analyze_expr(*part);
    }
    void analyze_expr_impl(const CharLiteralExpr&, SourceLocation) {}
    void analyze_expr_impl(const BoolLiteralExpr&, SourceLocation) {}
    void analyze_expr_impl(const NilLiteralExpr&, SourceLocation) {}
    void analyze_expr_impl(const TypeAnnotation&, SourceLocation) {}
};

} // namespace atomic
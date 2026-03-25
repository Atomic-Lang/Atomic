// codegen_scope.hpp
// Gerenciamento de escopo, variaveis na stack e inferencia de tipo para o codegen Atomic

#ifndef ATOMIC_CODEGEN_SCOPE_HPP
#define ATOMIC_CODEGEN_SCOPE_HPP

#include "../parser.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace atomic {

// ============================================================================
// Tipos de variavel
// ============================================================================

enum class VarType : uint8_t {
    Int,
    Float,
    String,
    Bool,
    Unknown,
};

// ============================================================================
// Informacao de variavel na stack
// ============================================================================

struct VarInfo {
    int32_t stack_offset;       // offset a partir de RBP (negativo)
    bool    is_const = false;
    VarType type     = VarType::Unknown;
};

// ============================================================================
// Informacao de funcao
// ============================================================================

struct FuncInfo {
    size_t  code_offset  = 0;
    size_t  code_size    = 0;
    VarType return_type  = VarType::Unknown;
};

// ============================================================================
// Scope Manager
// ============================================================================

class ScopeManager {
public:
    ScopeManager() = default;

    // ========================================================================
    // Escopo
    // ========================================================================

    void push_scope() {
        m_scopes.push_back({});
    }

    void pop_scope() {
        m_scopes.pop_back();
    }

    void clear_scopes() {
        m_scopes.clear();
    }

    // ========================================================================
    // Variaveis
    // ========================================================================

    VarInfo* find_var(const std::string& name) {
        for (int i = static_cast<int>(m_scopes.size()) - 1; i >= 0; i--) {
            auto it = m_scopes[i].find(name);
            if (it != m_scopes[i].end()) return &it->second;
        }
        return nullptr;
    }

    const VarInfo* find_var(const std::string& name) const {
        for (int i = static_cast<int>(m_scopes.size()) - 1; i >= 0; i--) {
            auto it = m_scopes[i].find(name);
            if (it != m_scopes[i].end()) return &it->second;
        }
        return nullptr;
    }

    VarInfo& declare_var(const std::string& name, bool is_const = false,
                         VarType type = VarType::Unknown) {
        m_stack_offset -= 8;
        if (-m_stack_offset > m_max_stack) m_max_stack = -m_stack_offset;

        VarInfo info;
        info.stack_offset = m_stack_offset;
        info.is_const = is_const;
        info.type = type;
        m_scopes.back()[name] = info;
        return m_scopes.back()[name];
    }

    // ========================================================================
    // Stack tracking
    // ========================================================================

    int32_t stack_offset() const { return m_stack_offset; }
    int32_t max_stack() const { return m_max_stack; }

    void set_stack_offset(int32_t off) { m_stack_offset = off; }

    // Reserva bytes na stack (retorna o offset RBP do inicio do bloco reservado)
    int32_t reserve_stack(int32_t bytes) {
        m_stack_offset -= bytes;
        if (-m_stack_offset > m_max_stack) m_max_stack = -m_stack_offset;
        return m_stack_offset;
    }

    // Reset para inicio de nova funcao
    void reset() {
        m_scopes.clear();
        m_stack_offset = 0;
        m_max_stack = 0;
    }

    // ========================================================================
    // Funcoes
    // ========================================================================

    void register_function(const std::string& name, const FuncInfo& info) {
        m_functions[name] = info;
    }

    FuncInfo* find_function(const std::string& name) {
        auto it = m_functions.find(name);
        if (it != m_functions.end()) return &it->second;
        return nullptr;
    }

    const FuncInfo* find_function(const std::string& name) const {
        auto it = m_functions.find(name);
        if (it != m_functions.end()) return &it->second;
        return nullptr;
    }

    // ========================================================================
    // Inferencia de tipo
    // ========================================================================

    VarType infer_expr_type(const Expr& expr) const {
        return std::visit([this](auto& e) -> VarType {
            return infer_type_impl(e);
        }, expr.data);
    }

private:
    std::vector<std::unordered_map<std::string, VarInfo>> m_scopes;
    int32_t m_stack_offset = 0;
    int32_t m_max_stack    = 0;

    std::unordered_map<std::string, FuncInfo> m_functions;

    // ========================================================================
    // Implementacoes de inferencia de tipo
    // ========================================================================

    VarType infer_type_impl(const IntLiteralExpr&) const { return VarType::Int; }
    VarType infer_type_impl(const FloatLiteralExpr&) const { return VarType::Float; }
    VarType infer_type_impl(const StringLiteralExpr&) const { return VarType::String; }
    VarType infer_type_impl(const CharLiteralExpr&) const { return VarType::Int; }
    VarType infer_type_impl(const BoolLiteralExpr&) const { return VarType::Bool; }
    VarType infer_type_impl(const NilLiteralExpr&) const { return VarType::Int; }

    VarType infer_type_impl(const IdentifierExpr& e) const {
        auto* var = find_var(e.name);
        if (var) return var->type;
        return VarType::Unknown;
    }

    VarType infer_type_impl(const BinaryExpr& e) const {
        auto lt = infer_expr_type(*e.left);
        auto rt = infer_expr_type(*e.right);
        if (e.op >= TokenType::Equal && e.op <= TokenType::GreaterEqual)
            return VarType::Bool;
        // Divisao sempre produz Float (estilo Python 3)
        if (e.op == TokenType::Slash)
            return VarType::Float;
        // Float contamina: se qualquer operando e Float, resultado e Float
        if (lt == VarType::Float || rt == VarType::Float)
            return VarType::Float;
        return lt;
    }

    VarType infer_type_impl(const UnaryExpr& e) const {
        return infer_expr_type(*e.operand);
    }

    VarType infer_type_impl(const CallExpr& e) const {
        if (e.callee->is<IdentifierExpr>()) {
            auto& name = e.callee->as<IdentifierExpr>().name;
            if (name == "str") return VarType::String;
            if (name == "i32" || name == "i64" ||
                name == "u32" || name == "u64") return VarType::Int;
            if (name == "f32" || name == "f64") return VarType::Float;

            // Consultar tipo de retorno de funcoes locais registradas
            auto* fn = find_function(name);
            if (fn && fn->return_type != VarType::Unknown)
                return fn->return_type;
        }
        return VarType::Unknown;
    }

    VarType infer_type_impl(const InterpolatedStringExpr&) const {
        return VarType::String;
    }

    // Fallback generico para todos os outros tipos de expressao
    template<typename T>
    VarType infer_type_impl(const T&) const { return VarType::Unknown; }
};

} // namespace atomic

#endif // ATOMIC_CODEGEN_SCOPE_HPP
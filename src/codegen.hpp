// codegen.hpp
// Code generator for the Atomic programming language — emits x86-64 COFF object files

#pragma once

#include "parser.hpp"
#include "lib_loader.hpp"
#include "codegen/codegen_x86.hpp"
#include "codegen/codegen_scope.hpp"
#include "platform_defs/coff_emitter.hpp"
#include "platform_defs/platform_defs_windows.hpp"
#include <cstring>
#include <algorithm>

namespace atomic {

using namespace reg;

// ============================================================================
// Code Generator
// ============================================================================

class CodeGen {
public:
    explicit CodeGen(const Program& program, const std::string& base_dir = ".")
        : m_program(program)
        , m_base_dir(base_dir)
        , m_emitter(make_emitter())
        , m_x86(m_emitter.section(TEXT_SEC_IDX))
    {
        PlatformDefs::add_common_externs(m_emitter);
        PlatformDefs::add_platform_externs(m_emitter);
    }

    // Retorna os caminhos das .dla que precisam ser linkadas
    const std::vector<std::string>& dla_paths() const { return m_dla_paths; }

private:
    // Cria o emitter com as secoes .text e .rdata ja alocadas
    static CoffEmitter make_emitter() {
        CoffEmitter e;
        e.create_text_section();
        e.create_rdata_section();
        return e;
    }

public:

    bool generate(const std::string& obj_path) {
        // Primeiro passo: coletar declaracoes de funcao e statements top-level
        std::vector<const FnDeclStmt*> fn_decls;
        std::vector<const Stmt*> top_level_stmts;

        for (auto& stmt : m_program.statements) {
            if (stmt->is<FnDeclStmt>()) {
                fn_decls.push_back(&stmt->as<FnDeclStmt>());
            } else {
                top_level_stmts.push_back(stmt.get());
            }
        }

        // Pre-passo: processar imports para registrar simbolos e tipos de retorno
        for (auto* stmt : top_level_stmts) {
            if (stmt->is<ImportStmt>()) {
                gen_stmt_impl(stmt->as<ImportStmt>());
            }
        }

        // Pre-passo: inferir tipos dos parametros a partir dos call sites
        for (auto* fn : fn_decls) {
            infer_param_types_from_callsites(*fn, top_level_stmts);
        }

        // Gerar codigo das funcoes
        for (auto* fn : fn_decls) {
            gen_fn_decl(*fn);
        }

        // Gerar main implicito a partir dos statements top-level
        gen_implicit_main(top_level_stmts);

        // Escrever .obj
        return m_emitter.write(obj_path);
    }

private:
    const Program&  m_program;
    std::string     m_base_dir;
    CoffEmitter     m_emitter;
    X86Emitter      m_x86;
    ScopeManager    m_scope;

    // Caminhos das .dla importadas (para passar ao linker)
    std::vector<std::string> m_dla_paths;

    // Tipo de retorno das funcoes importadas de .dla
    // chave: nome da funcao, valor: "int" or "string"
    std::unordered_map<std::string, std::string> m_lib_func_return;

    static constexpr size_t TEXT_SEC_IDX  = 0;
    static constexpr size_t RDATA_SEC_IDX = 1;

    Section& text_sec()  { return m_emitter.section(TEXT_SEC_IDX); }
    Section& rdata_sec() { return m_emitter.section(RDATA_SEC_IDX); }

    struct StringLiteral {
        std::string value;
        uint32_t rdata_offset;
    };
    std::vector<StringLiteral> m_string_literals;

    // Tipos de parametros inferidos a partir dos call sites
    // chave: "fn_name:param_index", valor: VarType
    std::unordered_map<std::string, VarType> m_param_types;

    static constexpr uint8_t ARG_REGS[] = {RCX, RDX, R8, R9};

    // Precisao float temporaria propagada do print(Nf: ...) para interpolated strings
    int m_float_precision = -1;

    // Stack de break/continue: cada loop empurra um vetor de offsets para patch
    std::vector<std::vector<size_t>> m_break_targets;
    std::vector<size_t> m_continue_targets;

    // ========================================================================
    // String literal em .rdata
    // ========================================================================

    uint32_t add_string_literal(const std::string& value) {
        for (auto& sl : m_string_literals) {
            if (sl.value == value) return sl.rdata_offset;
        }
        uint32_t offset = static_cast<uint32_t>(rdata_sec().pos());
        rdata_sec().emit_string(value);
        m_string_literals.push_back({value, offset});
        return offset;
    }

    // ========================================================================
    // Helpers: relocacoes e chamadas
    // ========================================================================

    void emit_lea_rdata(uint8_t r, uint32_t rdata_off) {
        size_t reloc_off = m_x86.emit_lea_reg_rip_rel32(r);
        text_sec().patch_u32(reloc_off, rdata_off);
        text_sec().add_relocation(
            static_cast<uint32_t>(reloc_off),
            m_emitter.section_symbol(RDATA_SEC_IDX),
            IMAGE_REL_AMD64_REL32);
    }

    void emit_extern_call(const std::string& name) {
        uint32_t sym_idx = m_emitter.add_extern_symbol(name);
        m_x86.emit_call_rel32(0);
        text_sec().add_relocation(
            static_cast<uint32_t>(m_x86.pos() - 4),
            sym_idx,
            IMAGE_REL_AMD64_REL32);
    }

    void emit_local_call(const FuncInfo& fn) {
        int32_t rel = static_cast<int32_t>(fn.code_offset - (m_x86.pos() + 5));
        m_x86.emit_call_rel32(static_cast<uint32_t>(rel));
    }

    // ========================================================================
    // Implicit main
    // ========================================================================

    void gen_implicit_main(const std::vector<const Stmt*>& stmts) {
        m_x86.align_code(16);
        size_t fn_start = m_x86.pos();

        m_scope.reset();
        m_scope.push_scope();

        size_t stack_patch = m_x86.emit_prologue();

        // Setar console para UTF-8 (chcp 65001)
        m_x86.emit_mov_reg_imm32(RCX, 65001);
        emit_extern_call("SetConsoleOutputCP");
        m_x86.emit_mov_reg_imm32(RCX, 65001);
        emit_extern_call("SetConsoleCP");

        for (auto* stmt : stmts) {
            gen_stmt(*stmt);
        }

        m_x86.emit_xor_reg_reg(RAX, RAX);
        m_x86.emit_epilogue();
        m_x86.patch_stack_size(stack_patch, m_scope.max_stack());

        FuncInfo info;
        info.code_offset = fn_start;
        info.code_size = m_x86.pos() - fn_start;
        m_scope.register_function("main", info);
        m_emitter.add_global_symbol("main", TEXT_SEC_IDX,
            static_cast<uint32_t>(fn_start), true);

        m_scope.pop_scope();
    }

    // ========================================================================
    // Declaracao de funcao
    // ========================================================================

    void gen_fn_decl(const FnDeclStmt& fn) {
        VarType ret_type = infer_fn_return_type(fn);

        m_x86.align_code(16);
        size_t fn_start = m_x86.pos();

        m_scope.reset();
        m_scope.push_scope();

        size_t stack_patch = m_x86.emit_prologue();

        for (size_t i = 0; i < fn.params.size() && i < 4; i++) {
            auto& p = fn.params[i];
            if (p.is_self) continue;

            VarType ptype = VarType::Unknown;
            auto key = fn.name + ":" + std::to_string(i);
            auto it = m_param_types.find(key);
            if (it != m_param_types.end()) ptype = it->second;

            auto& var = m_scope.declare_var(p.name, false, ptype);
            m_x86.emit_mov_rbp_offset_reg(var.stack_offset, ARG_REGS[i]);
        }

        for (auto& stmt : fn.body) {
            gen_stmt(*stmt);
        }

        m_x86.emit_xor_reg_reg(RAX, RAX);
        m_x86.emit_epilogue();
        m_x86.patch_stack_size(stack_patch, m_scope.max_stack());

        FuncInfo info;
        info.code_offset = fn_start;
        info.code_size = m_x86.pos() - fn_start;
        info.return_type = ret_type;
        m_scope.register_function(fn.name, info);
        m_emitter.add_global_symbol(fn.name, TEXT_SEC_IDX,
            static_cast<uint32_t>(fn_start), true);

        m_scope.pop_scope();
    }

    // ========================================================================
    // Inferencia de tipos de parametros a partir dos call sites
    // ========================================================================

    void infer_param_types_from_callsites(const FnDeclStmt& fn,
                                           const std::vector<const Stmt*>& stmts) {
        for (auto* stmt : stmts) {
            scan_callsites_in_stmt(fn.name, *stmt);
        }
    }

    void scan_callsites_in_stmt(const std::string& fn_name, const Stmt& stmt) {
        if (stmt.is<ExprStmt>()) {
            auto& es = stmt.as<ExprStmt>();
            if (es.expr) scan_callsites_in_expr(fn_name, *es.expr);
        } else if (stmt.is<VarDeclStmt>()) {
            auto& vd = stmt.as<VarDeclStmt>();
            if (vd.initializer) scan_callsites_in_expr(fn_name, *vd.initializer);
        } else if (stmt.is<IfStmt>()) {
            auto& ifs = stmt.as<IfStmt>();
            for (auto& br : ifs.branches) {
                if (br.condition) scan_callsites_in_expr(fn_name, *br.condition);
                for (auto& s : br.body) scan_callsites_in_stmt(fn_name, *s);
            }
        } else if (stmt.is<WhileStmt>()) {
            auto& ws = stmt.as<WhileStmt>();
            if (ws.condition) scan_callsites_in_expr(fn_name, *ws.condition);
            for (auto& s : ws.body) scan_callsites_in_stmt(fn_name, *s);
        } else if (stmt.is<ForStmt>()) {
            auto& fs = stmt.as<ForStmt>();
            for (auto& s : fs.body) scan_callsites_in_stmt(fn_name, *s);
        }
    }

    void scan_callsites_in_expr(const std::string& fn_name, const Expr& expr) {
        if (expr.is<CallExpr>()) {
            auto& call = expr.as<CallExpr>();
            // Checar se e uma chamada a fn_name
            if (call.callee->is<IdentifierExpr>()) {
                auto& callee_name = call.callee->as<IdentifierExpr>().name;
                if (callee_name == fn_name) {
                    // Inferir tipo de cada argumento
                    for (size_t i = 0; i < call.args.size(); i++) {
                        VarType at = infer_return_expr_type(*call.args[i]);
                        auto key = fn_name + ":" + std::to_string(i);
                        if (m_param_types.find(key) == m_param_types.end() ||
                            m_param_types[key] == VarType::Unknown) {
                            m_param_types[key] = at;
                        }
                    }
                }
                // Tambem escanear argumentos recursivamente
                for (auto& arg : call.args) {
                    scan_callsites_in_expr(fn_name, *arg);
                }
            }
        } else if (expr.is<BinaryExpr>()) {
            auto& bin = expr.as<BinaryExpr>();
            scan_callsites_in_expr(fn_name, *bin.left);
            scan_callsites_in_expr(fn_name, *bin.right);
        } else if (expr.is<UnaryExpr>()) {
            scan_callsites_in_expr(fn_name, *expr.as<UnaryExpr>().operand);
        } else if (expr.is<InterpolatedStringExpr>()) {
            auto& interp = expr.as<InterpolatedStringExpr>();
            for (auto& part : interp.parts) {
                scan_callsites_in_expr(fn_name, *part);
            }
        }
    }

    // ========================================================================
    // Inferencia de tipo de retorno de funcao
    // ========================================================================

    // Analisa os ReturnStmt no body da funcao pra inferir o tipo
    VarType infer_fn_return_type(const FnDeclStmt& fn) {
        VarType result = VarType::Unknown;
        for (auto& stmt : fn.body) {
            VarType t = scan_return_type(*stmt);
            if (t != VarType::Unknown) {
                result = t;
                break; // usa o primeiro return encontrado
            }
        }
        return result;
    }

    // Escaneia recursivamente um statement procurando ReturnStmt
    VarType scan_return_type(const Stmt& stmt) {
        if (stmt.is<ReturnStmt>()) {
            auto& ret = stmt.as<ReturnStmt>();
            if (ret.value) {
                return infer_return_expr_type(*ret.value);
            }
            return VarType::Unknown;
        }
        // Procurar dentro de if/elif/else
        if (stmt.is<IfStmt>()) {
            auto& ifs = stmt.as<IfStmt>();
            for (auto& br : ifs.branches) {
                for (auto& s : br.body) {
                    VarType t = scan_return_type(*s);
                    if (t != VarType::Unknown) return t;
                }
            }
        }
        return VarType::Unknown;
    }

    // Inferencia de tipo para expressoes de retorno (antes do codegen rodar)
    VarType infer_return_expr_type(const Expr& expr) {
        if (expr.is<IntLiteralExpr>()) return VarType::Int;
        if (expr.is<FloatLiteralExpr>()) return VarType::Float;
        if (expr.is<StringLiteralExpr>()) return VarType::String;
        if (expr.is<InterpolatedStringExpr>()) return VarType::String;
        if (expr.is<BoolLiteralExpr>()) return VarType::Bool;
        if (expr.is<BinaryExpr>()) {
            auto& bin = expr.as<BinaryExpr>();
            // Comparacoes retornam bool, aritmetica retorna o tipo do operando
            if (bin.op >= TokenType::Equal && bin.op <= TokenType::GreaterEqual)
                return VarType::Bool;
            // Divisao sempre produz Float (estilo Python 3)
            if (bin.op == TokenType::Slash)
                return VarType::Float;
            auto lt = infer_return_expr_type(*bin.left);
            auto rt = infer_return_expr_type(*bin.right);
            if (lt == VarType::Float || rt == VarType::Float)
                return VarType::Float;
            return lt;
        }
        if (expr.is<UnaryExpr>()) {
            return infer_return_expr_type(*expr.as<UnaryExpr>().operand);
        }
        if (expr.is<CallExpr>()) {
            auto& call = expr.as<CallExpr>();
            if (call.callee->is<IdentifierExpr>()) {
                auto& name = call.callee->as<IdentifierExpr>().name;
                auto* fn = m_scope.find_function(name);
                if (fn) return fn->return_type;
            }
        }
        // Nao conseguiu inferir — assume Int (comportamento anterior)
        return VarType::Int;
    }

    // ========================================================================
    // Statements
    // ========================================================================

    void gen_stmt(const Stmt& stmt) {
        std::visit([this](auto& s) { gen_stmt_impl(s); }, stmt.data);
    }

    void gen_stmt_impl(const ExprStmt& s) {
        if (s.expr) gen_expr(*s.expr);
    }

    void gen_stmt_impl(const VarDeclStmt& s) {
        VarType type = VarType::Unknown;
        if (s.initializer) {
            type = m_scope.infer_expr_type(*s.initializer);
            gen_expr(*s.initializer);
        } else {
            m_x86.emit_xor_reg_reg(RAX, RAX);
        }

        auto* existing = m_scope.find_var(s.name);
        if (existing && !s.is_const) {
            existing->type = type;
            m_x86.emit_mov_rbp_offset_reg(existing->stack_offset, RAX);
        } else {
            auto& var = m_scope.declare_var(s.name, s.is_const, type);
            m_x86.emit_mov_rbp_offset_reg(var.stack_offset, RAX);
        }
    }

    void gen_stmt_impl(const ReturnStmt& s) {
        if (s.value) {
            bool needs_heap_copy = false;
            if (s.value->is<InterpolatedStringExpr>()) {
                needs_heap_copy = true;
            } else if (!s.value->is<StringLiteralExpr>()) {
                VarType vt = m_scope.infer_expr_type(*s.value);
                if (vt == VarType::String) {
                    needs_heap_copy = true;
                }
            }

            gen_expr(*s.value);

            if (needs_heap_copy) {
                // RAX = ponteiro pra string na stack
                // Usar variaveis na stack pra nao desalinhar RSP com push/pop
                int32_t src_off = m_scope.reserve_stack(8);
                int32_t dst_off = m_scope.reserve_stack(8);

                // Salvar ponteiro original (src)
                m_x86.emit_mov_rbp_offset_reg(src_off, RAX);

                // strlen(src)
                m_x86.emit_mov_reg_reg(RCX, RAX);
                emit_extern_call("strlen");

                // malloc(len + 1)
                m_x86.emit_inc_reg(RAX);
                m_x86.emit_mov_reg_reg(RCX, RAX);
                emit_extern_call("malloc");

                // Salvar ponteiro heap (dest)
                m_x86.emit_mov_rbp_offset_reg(dst_off, RAX);

                // strcpy(dest, src)
                m_x86.emit_mov_reg_reg(RCX, RAX);
                m_x86.emit_mov_reg_rbp_offset(RDX, src_off);
                emit_extern_call("strcpy");

                // Retornar ponteiro do heap em RAX
                m_x86.emit_mov_reg_rbp_offset(RAX, dst_off);
            }
        } else {
            m_x86.emit_xor_reg_reg(RAX, RAX);
        }
        m_x86.emit_epilogue();
    }

    void gen_stmt_impl(const IfStmt& s) {
        std::vector<size_t> end_jumps;

        for (size_t i = 0; i < s.branches.size(); i++) {
            auto& br = s.branches[i];

            if (br.condition) {
                gen_expr(*br.condition);
                m_x86.emit_test_reg_reg(RAX, RAX);
                size_t skip_off = m_x86.emit_jcc_rel32(cc::E);

                m_scope.push_scope();
                for (auto& st : br.body) gen_stmt(*st);
                m_scope.pop_scope();

                end_jumps.push_back(m_x86.emit_jmp_rel32());
                m_x86.patch_jump(skip_off, m_x86.pos());
            } else {
                m_scope.push_scope();
                for (auto& st : br.body) gen_stmt(*st);
                m_scope.pop_scope();
            }
        }

        for (auto off : end_jumps) {
            m_x86.patch_jump(off, m_x86.pos());
        }
    }

    void gen_stmt_impl(const WhileStmt& s) {
        size_t loop_start = m_x86.pos();

        m_break_targets.push_back({});
        m_continue_targets.push_back(loop_start);

        gen_expr(*s.condition);
        m_x86.emit_test_reg_reg(RAX, RAX);
        size_t end_off = m_x86.emit_jcc_rel32(cc::E);

        m_scope.push_scope();
        for (auto& st : s.body) gen_stmt(*st);
        m_scope.pop_scope();

        size_t back_off = m_x86.emit_jmp_rel32();
        m_x86.patch_jump(back_off, loop_start);
        m_x86.patch_jump(end_off, m_x86.pos());

        // Patch todos os breaks para apontar ao fim do loop
        for (auto off : m_break_targets.back()) {
            m_x86.patch_jump(off, m_x86.pos());
        }
        m_break_targets.pop_back();
        m_continue_targets.pop_back();
    }

    void gen_stmt_impl(const LoopStmt& s) {
        size_t loop_start = m_x86.pos();

        m_break_targets.push_back({});
        m_continue_targets.push_back(loop_start);

        m_scope.push_scope();
        for (auto& st : s.body) gen_stmt(*st);
        m_scope.pop_scope();

        size_t back_off = m_x86.emit_jmp_rel32();
        m_x86.patch_jump(back_off, loop_start);

        for (auto off : m_break_targets.back()) {
            m_x86.patch_jump(off, m_x86.pos());
        }
        m_break_targets.pop_back();
        m_continue_targets.pop_back();
    }

    void gen_stmt_impl(const ForStmt& s) {
        if (!s.iterable || !s.iterable->is<RangeExpr>()) return;
        auto& range = s.iterable->as<RangeExpr>();

        m_scope.push_scope();

        if (range.start) gen_expr(*range.start);
        else m_x86.emit_xor_reg_reg(RAX, RAX);
        auto& iter_var = m_scope.declare_var(s.var_name);
        m_x86.emit_mov_rbp_offset_reg(iter_var.stack_offset, RAX);

        if (range.end) gen_expr(*range.end);
        else m_x86.emit_xor_reg_reg(RAX, RAX);
        auto& end_var = m_scope.declare_var("__end__");
        m_x86.emit_mov_rbp_offset_reg(end_var.stack_offset, RAX);

        size_t loop_start = m_x86.pos();

        m_break_targets.push_back({});
        m_continue_targets.push_back(loop_start);

        m_x86.emit_mov_reg_rbp_offset(RAX, iter_var.stack_offset);
        m_x86.emit_mov_reg_rbp_offset(RCX, end_var.stack_offset);
        m_x86.emit_cmp_reg_reg(RAX, RCX);
        uint8_t cond = range.inclusive ? cc::G : cc::GE;
        size_t end_off = m_x86.emit_jcc_rel32(cond);

        for (auto& st : s.body) gen_stmt(*st);

        m_x86.emit_mov_reg_rbp_offset(RAX, iter_var.stack_offset);
        m_x86.emit_inc_reg(RAX);
        m_x86.emit_mov_rbp_offset_reg(iter_var.stack_offset, RAX);

        size_t back_off = m_x86.emit_jmp_rel32();
        m_x86.patch_jump(back_off, loop_start);
        m_x86.patch_jump(end_off, m_x86.pos());

        for (auto off : m_break_targets.back()) {
            m_x86.patch_jump(off, m_x86.pos());
        }
        m_break_targets.pop_back();
        m_continue_targets.pop_back();

        m_scope.pop_scope();
    }

    // Stubs
    void gen_stmt_impl(const BreakStmt&) {
        if (!m_break_targets.empty()) {
            size_t jmp_off = m_x86.emit_jmp_rel32();
            m_break_targets.back().push_back(jmp_off);
        }
    }
    void gen_stmt_impl(const ContinueStmt&) {
        if (!m_continue_targets.empty()) {
            size_t jmp_off = m_x86.emit_jmp_rel32();
            m_x86.patch_jump(jmp_off, m_continue_targets.back());
        }
    }
    void gen_stmt_impl(const MatchStmt&) {}
    void gen_stmt_impl(const StructDeclStmt&) {}
    void gen_stmt_impl(const EnumDeclStmt&) {}
    void gen_stmt_impl(const ImplBlock&) {}
    void gen_stmt_impl(const TraitDeclStmt&) {}
    void gen_stmt_impl(const ImportStmt& s) {
        if (s.is_extern || s.module_path.empty()) return;

        // Evitar processar o mesmo import duas vezes
        if (m_lib_func_return.count(s.module_path + ":loaded")) return;

        try {
            auto desc = LibLoader::load(s.module_path, m_base_dir);

            // Guardar caminho da .dla para o linker
            m_dla_paths.push_back(desc.dla_path);

            // Registrar cada funcao da lib como simbolo externo e guardar tipo de retorno
            for (auto& fn : desc.funcoes) {
                m_emitter.add_extern_symbol(fn.nome);
                m_lib_func_return[fn.nome] = fn.retorno;
            }

            // Marcar como ja processado
            m_lib_func_return[s.module_path + ":loaded"] = "1";
        } catch (const std::runtime_error&) {
            // Erro ja reportado pelo sema, ignorar aqui
        }
    }
    void gen_stmt_impl(const ModDeclStmt&) {}
    void gen_stmt_impl(const TryStmt&) {}
    void gen_stmt_impl(const DeferStmt&) {}
    void gen_stmt_impl(const DropStmt&) {}
    void gen_stmt_impl(const UnsafeBlock&) {}
    void gen_stmt_impl(const TypeAliasStmt&) {}
    void gen_stmt_impl(const DeriveStmt&) {}
    void gen_stmt_impl(const FnDeclStmt&) {}

    // ========================================================================
    // Expressoes (resultado em RAX)
    // ========================================================================

    void gen_expr(const Expr& expr) {
        std::visit([this](auto& e) { gen_expr_impl(e); }, expr.data);
    }

    void gen_expr_impl(const IntLiteralExpr& e) {
        int64_t val = 0;
        auto& s = e.value;
        if (s.starts_with("0x") || s.starts_with("0X")) {
            val = std::stoll(s.substr(2), nullptr, 16);
        } else if (s.starts_with("0b") || s.starts_with("0B")) {
            val = std::stoll(s.substr(2), nullptr, 2);
        } else if (s.starts_with("0o") || s.starts_with("0O")) {
            val = std::stoll(s.substr(2), nullptr, 8);
        } else {
            std::string clean;
            for (char c : s) if (c != '_') clean += c;
            val = std::stoll(clean);
        }

        if (val >= INT32_MIN && val <= INT32_MAX) {
            m_x86.emit_mov_reg_imm32(RAX, static_cast<int32_t>(val));
        } else {
            m_x86.emit_mov_reg_imm64(RAX, static_cast<uint64_t>(val));
        }
    }

    void gen_expr_impl(const FloatLiteralExpr& e) {
        std::string clean;
        for (char c : e.value) if (c != '_') clean += c;
        double val = std::stod(clean);
        uint64_t bits;
        std::memcpy(&bits, &val, 8);
        m_x86.emit_mov_reg_imm64(RAX, bits);
    }

    void gen_expr_impl(const StringLiteralExpr& e) {
        uint32_t rdata_off = add_string_literal(e.value);
        emit_lea_rdata(RAX, rdata_off);
    }

    void gen_expr_impl(const CharLiteralExpr& e) {
        int32_t val = e.value.empty() ? 0 : static_cast<int32_t>(static_cast<uint8_t>(e.value[0]));
        m_x86.emit_mov_reg_imm32(RAX, val);
    }

    void gen_expr_impl(const BoolLiteralExpr& e) {
        m_x86.emit_mov_reg_imm32(RAX, e.value ? 1 : 0);
    }

    void gen_expr_impl(const NilLiteralExpr&) {
        m_x86.emit_xor_reg_reg(RAX, RAX);
    }

    void gen_expr_impl(const IdentifierExpr& e) {
        auto* var = m_scope.find_var(e.name);
        if (var) {
            m_x86.emit_mov_reg_rbp_offset(RAX, var->stack_offset);
        } else {
            m_x86.emit_xor_reg_reg(RAX, RAX);
        }
    }

    void gen_expr_impl(const UnaryExpr& e) {
        gen_expr(*e.operand);
        switch (e.op) {
            case TokenType::Minus:  m_x86.emit_neg_reg(RAX); break;
            case TokenType::KwNot:
                m_x86.emit_test_reg_reg(RAX, RAX);
                m_x86.emit_setcc(cc::E);
                m_x86.emit_movzx_rax_al();
                break;
            case TokenType::Tilde:  m_x86.emit_not_reg(RAX); break;
            default: break;
        }
    }

    void gen_expr_impl(const BinaryExpr& e) {
        // Inferir tipos dos operandos para decidir se usa SSE
        VarType lt = m_scope.infer_expr_type(*e.left);
        VarType rt = m_scope.infer_expr_type(*e.right);
        bool is_float_op = (lt == VarType::Float || rt == VarType::Float);

        // Divisao (/) sempre produz float, mesmo entre inteiros (estilo Python 3)
        if (e.op == TokenType::Slash) is_float_op = true;

        // Quando m_float_precision ativo (dentro de print(Nf: ...)),
        // forcar aritmetica float mesmo entre inteiros
        if (m_float_precision >= 0 && !is_float_op &&
            (e.op == TokenType::Plus || e.op == TokenType::Minus ||
             e.op == TokenType::Star ||
             e.op == TokenType::Percent)) {
            is_float_op = true;
        }

        // Para operacoes aritmeticas com float, usar SSE
        if (is_float_op && (e.op == TokenType::Plus || e.op == TokenType::Minus ||
                            e.op == TokenType::Star || e.op == TokenType::Slash ||
                            e.op == TokenType::Percent ||
                            e.op == TokenType::Equal || e.op == TokenType::NotEqual ||
                            e.op == TokenType::Less || e.op == TokenType::Greater ||
                            e.op == TokenType::LessEqual || e.op == TokenType::GreaterEqual)) {
            gen_float_binary(e, lt, rt);
            return;
        }

        gen_expr(*e.left);
        m_x86.emit_push(RAX);
        gen_expr(*e.right);
        m_x86.emit_mov_reg_reg(RCX, RAX);
        m_x86.emit_pop(RAX);

        switch (e.op) {
            case TokenType::Plus:    m_x86.emit_add_reg_reg(RAX, RCX); break;
            case TokenType::Minus:   m_x86.emit_sub_reg_reg(RAX, RCX); break;
            case TokenType::Star:    m_x86.emit_imul_reg_reg(RAX, RCX); break;
            case TokenType::Percent:
                m_x86.emit_cqo(); m_x86.emit_idiv_reg(RCX);
                m_x86.emit_mov_reg_reg(RAX, RDX);
                break;
            case TokenType::DoubleStar: {
                m_x86.emit_push(RBX);
                m_x86.emit_mov_reg_reg(RBX, RAX);
                m_x86.emit_mov_reg_imm32(RAX, 1);
                size_t loop_start = m_x86.pos();
                m_x86.emit_test_reg_reg(RCX, RCX);
                size_t done_off = m_x86.emit_jcc_rel32(cc::E);
                m_x86.emit_imul_reg_reg(RAX, RBX);
                m_x86.emit_dec_reg(RCX);
                size_t back_off = m_x86.emit_jmp_rel32();
                m_x86.patch_jump(back_off, loop_start);
                m_x86.patch_jump(done_off, m_x86.pos());
                m_x86.emit_pop(RBX);
                break;
            }
            case TokenType::Ampersand:  m_x86.emit_and_reg_reg(RAX, RCX); break;
            case TokenType::Pipe:       m_x86.emit_or_reg_reg(RAX, RCX); break;
            case TokenType::Caret:      m_x86.emit_bxor_reg_reg(RAX, RCX); break;
            case TokenType::ShiftLeft:  m_x86.emit_shl_reg_cl(RAX); break;
            case TokenType::ShiftRight: m_x86.emit_sar_reg_cl(RAX); break;

            case TokenType::Equal:        m_x86.emit_cmp_reg_reg(RAX, RCX); m_x86.emit_setcc(cc::E);  m_x86.emit_movzx_rax_al(); break;
            case TokenType::NotEqual:     m_x86.emit_cmp_reg_reg(RAX, RCX); m_x86.emit_setcc(cc::NE); m_x86.emit_movzx_rax_al(); break;
            case TokenType::Less:         m_x86.emit_cmp_reg_reg(RAX, RCX); m_x86.emit_setcc(cc::L);  m_x86.emit_movzx_rax_al(); break;
            case TokenType::Greater:      m_x86.emit_cmp_reg_reg(RAX, RCX); m_x86.emit_setcc(cc::G);  m_x86.emit_movzx_rax_al(); break;
            case TokenType::LessEqual:    m_x86.emit_cmp_reg_reg(RAX, RCX); m_x86.emit_setcc(cc::LE); m_x86.emit_movzx_rax_al(); break;
            case TokenType::GreaterEqual: m_x86.emit_cmp_reg_reg(RAX, RCX); m_x86.emit_setcc(cc::GE); m_x86.emit_movzx_rax_al(); break;

            case TokenType::KwAnd:
                m_x86.emit_test_reg_reg(RAX, RAX);
                m_x86.emit_setcc(cc::NE);
                m_x86.emit_movzx_rax_al();
                m_x86.emit_test_reg_reg(RCX, RCX);
                m_x86.emit_push(RAX);
                m_x86.emit_setcc(cc::NE);
                m_x86.emit_movzx_rax_al();
                m_x86.emit_pop(RCX);
                m_x86.emit_and_reg_reg(RAX, RCX);
                break;
            case TokenType::KwOr:
                m_x86.emit_or_reg_reg(RAX, RCX);
                break;

            default: break;
        }
    }

    // ========================================================================
    // Operacoes binarias com float (SSE2)
    // ========================================================================

    // Carrega operando em XMM, convertendo de int se necessario.
    // O valor ja deve estar em RAX (bits raw).
    void emit_load_xmm_from_rax(uint8_t xmm_reg, VarType type) {
        if (type == VarType::Float) {
            // RAX contem bits IEEE 754 — mover diretamente para XMM
            m_x86.emit_movq_xmm_reg(xmm_reg, RAX);
        } else {
            // RAX contem inteiro — converter para double
            m_x86.emit_cvtsi2sd_xmm_reg(xmm_reg, RAX);
        }
    }

    void gen_float_binary(const BinaryExpr& e, VarType lt, VarType rt) {
        // Avaliar operando esquerdo → RAX
        gen_expr(*e.left);
        m_x86.emit_push(RAX);

        // Avaliar operando direito → RAX
        gen_expr(*e.right);

        // Carregar direito em XMM1
        emit_load_xmm_from_rax(xmm::XMM1, rt);

        // Recuperar esquerdo e carregar em XMM0
        m_x86.emit_pop(RAX);
        emit_load_xmm_from_rax(xmm::XMM0, lt);

        bool is_comparison = false;

        switch (e.op) {
            case TokenType::Plus:
                m_x86.emit_addsd_xmm_xmm(xmm::XMM0, xmm::XMM1);
                break;
            case TokenType::Minus:
                m_x86.emit_subsd_xmm_xmm(xmm::XMM0, xmm::XMM1);
                break;
            case TokenType::Star:
                m_x86.emit_mulsd_xmm_xmm(xmm::XMM0, xmm::XMM1);
                break;
            case TokenType::Slash:
                m_x86.emit_divsd_xmm_xmm(xmm::XMM0, xmm::XMM1);
                break;
            case TokenType::Percent: {
                // fmod: a - trunc(a/b) * b
                // XMM0 = a, XMM1 = b
                // XMM2 = a / b
                m_x86.emit_movsd_xmm_xmm(xmm::XMM2, xmm::XMM0);
                m_x86.emit_divsd_xmm_xmm(xmm::XMM2, xmm::XMM1);
                // Truncar para inteiro e voltar a double
                m_x86.emit_cvttsd2si_reg_xmm(RAX, xmm::XMM2);
                m_x86.emit_cvtsi2sd_xmm_reg(xmm::XMM2, RAX);
                // XMM2 = trunc(a/b) * b
                m_x86.emit_mulsd_xmm_xmm(xmm::XMM2, xmm::XMM1);
                // XMM0 = a - trunc(a/b) * b
                m_x86.emit_subsd_xmm_xmm(xmm::XMM0, xmm::XMM2);
                break;
            }
            case TokenType::Equal:
            case TokenType::NotEqual:
            case TokenType::Less:
            case TokenType::Greater:
            case TokenType::LessEqual:
            case TokenType::GreaterEqual:
                is_comparison = true;
                // ucomisd seta flags CF e ZF
                m_x86.emit_ucomisd_xmm_xmm(xmm::XMM0, xmm::XMM1);
                break;
            default: break;
        }

        if (is_comparison) {
            // ucomisd usa flags unsigned: CF=1 se a < b, ZF=1 se a == b
            // Mapeamento para setcc com condition codes unsigned:
            // a == b  → ZF=1          → sete  (0x04)
            // a != b  → ZF=0          → setne (0x05)
            // a < b   → CF=1          → setb  (0x02)
            // a >= b  → CF=0          → setae (0x03)
            // a <= b  → CF=1 or ZF=1  → setbe (0x06)
            // a > b   → CF=0 and ZF=0 → seta  (0x07)
            uint8_t float_cc;
            switch (e.op) {
                case TokenType::Equal:        float_cc = 0x04; break; // sete
                case TokenType::NotEqual:     float_cc = 0x05; break; // setne
                case TokenType::Less:         float_cc = 0x02; break; // setb
                case TokenType::GreaterEqual: float_cc = 0x03; break; // setae
                case TokenType::LessEqual:    float_cc = 0x06; break; // setbe
                case TokenType::Greater:      float_cc = 0x07; break; // seta
                default: float_cc = 0x04; break;
            }
            m_x86.emit_setcc(float_cc);
            m_x86.emit_movzx_rax_al();
            return;
        }

        // Mover resultado float de XMM0 de volta para RAX (como bits IEEE 754)
        m_x86.emit_movq_reg_xmm(RAX, xmm::XMM0);
    }

    void gen_expr_impl(const CallExpr& e) {
        std::string fn_name;
        if (e.callee->is<IdentifierExpr>()) {
            fn_name = e.callee->as<IdentifierExpr>().name;
        } else if (e.callee->is<MemberExpr>()) {
            fn_name = e.callee->as<MemberExpr>().member;
        }

        if (fn_name == "print" || fn_name == "printl" ||
            fn_name == "print_y" || fn_name == "print_r" ||
            fn_name == "print_b" || fn_name == "print_g" ||
            fn_name == "printl_y" || fn_name == "printl_r" ||
            fn_name == "printl_b" || fn_name == "printl_g") {
            gen_print_call(e, fn_name);
            return;
        }

        for (size_t i = 0; i < e.args.size() && i < 4; i++) {
            gen_expr(*e.args[i]);
            m_x86.emit_push(RAX);
        }
        size_t nargs = std::min(e.args.size(), size_t(4));
        for (size_t i = nargs; i > 0; i--) {
            m_x86.emit_pop(ARG_REGS[i - 1]);
        }

        auto* fn = m_scope.find_function(fn_name);
        if (fn) {
            emit_local_call(*fn);
        } else {
            emit_extern_call(fn_name);
        }
    }

    // ========================================================================
    // Print
    // ========================================================================

    // Imprime float com trim de zeros a direita: sprintf(buf,"%.6f",val) + trim + printf
    // RAX deve conter bits IEEE 754 do double. has_newline controla \n no final.
    void emit_float_smart_print(uint8_t val_reg, bool has_newline) {
        // Reservar buffer de 64 bytes na stack
        int32_t buf_off = m_scope.reserve_stack(64);
        // Salvar valor double na stack
        int32_t val_off = m_scope.reserve_stack(8);
        m_x86.emit_mov_rbp_offset_reg(val_off, val_reg);

        // sprintf(buf, "%.6f", val)
        m_x86.emit_lea_reg_rbp_offset(RCX, buf_off);           // RCX = buf
        emit_lea_rdata(RDX, add_string_literal("%.6f"));        // RDX = fmt
        m_x86.emit_mov_reg_rbp_offset(R8, val_off);            // R8 = double bits
        m_x86.emit_movq_xmm_reg(xmm::XMM2, R8);               // XMM2 = double (varargs ABI)
        emit_extern_call("sprintf");

        // strlen(buf) → RAX = len
        m_x86.emit_lea_reg_rbp_offset(RCX, buf_off);
        emit_extern_call("strlen");

        // RAX = len. Agora trim: percorrer de buf[len-1] ate buf[0]
        // while (len > 0 && buf[len-1] == '0') buf[--len] = '\0'
        // if (len > 0 && buf[len-1] == '.') buf[--len] = '\0'

        // Usar RCX = ponteiro base do buf, RAX = len
        m_x86.emit_lea_reg_rbp_offset(RCX, buf_off);

        // Loop: trim '0'
        size_t trim_loop = m_x86.pos();
        // if (len == 0) break
        m_x86.emit_test_reg_reg(RAX, RAX);
        size_t trim_done = m_x86.emit_jcc_rel32(cc::E);
        // dl = buf[len-1]
        // lea rdx, [rcx + rax - 1] → carrega byte
        m_x86.emit_dec_reg(RAX);              // rax = len-1
        // Carregar byte: movzx edx, byte [rcx + rax]
        // REX.W=0 para movzx 8→32
        // 0F B6 14 01 = movzx edx, [rcx + rax] (SIB: base=rcx, index=rax, scale=1)
        m_x86.text().emit_u8(0x0F);
        m_x86.text().emit_u8(0xB6);
        m_x86.modrm(0b00, 2, 4);  // mod=00, reg=edx(2), rm=SIB(4)
        m_x86.sib(0, 0, 1);       // scale=1, index=rax(0), base=rcx(1)

        // cmp dl, '0' (0x30)
        // 80 FA 30
        m_x86.text().emit_u8(0x80);
        m_x86.text().emit_u8(0xFA);
        m_x86.text().emit_u8(0x30);
        size_t not_zero = m_x86.emit_jcc_rel32(cc::NE);

        // buf[len-1] = '\0': mov byte [rcx + rax], 0
        // C6 04 01 00
        m_x86.text().emit_u8(0xC6);
        m_x86.modrm(0b00, 0, 4);  // mod=00, reg=0, rm=SIB
        m_x86.sib(0, 0, 1);       // index=rax, base=rcx
        m_x86.text().emit_u8(0x00);

        // rax ainda e len-1, loop de volta (sem incrementar, pois dec ja foi feito)
        size_t back = m_x86.emit_jmp_rel32();
        m_x86.patch_jump(back, trim_loop);

        m_x86.patch_jump(not_zero, m_x86.pos());
        // Restaurar rax = len (desfazer o dec, ja que nao cortamos este char)
        m_x86.emit_inc_reg(RAX);

        m_x86.patch_jump(trim_done, m_x86.pos());

        // Trim '.': checar se buf[len-1] == '.'
        m_x86.emit_test_reg_reg(RAX, RAX);
        size_t skip_dot = m_x86.emit_jcc_rel32(cc::E);
        m_x86.emit_dec_reg(RAX);
        // movzx edx, byte [rcx + rax]
        m_x86.text().emit_u8(0x0F);
        m_x86.text().emit_u8(0xB6);
        m_x86.modrm(0b00, 2, 4);
        m_x86.sib(0, 0, 1);
        // cmp dl, '.' (0x2E)
        m_x86.text().emit_u8(0x80);
        m_x86.text().emit_u8(0xFA);
        m_x86.text().emit_u8(0x2E);
        size_t not_dot = m_x86.emit_jcc_rel32(cc::NE);
        // buf[len-1] = '\0'
        m_x86.text().emit_u8(0xC6);
        m_x86.modrm(0b00, 0, 4);
        m_x86.sib(0, 0, 1);
        m_x86.text().emit_u8(0x00);
        m_x86.patch_jump(not_dot, m_x86.pos());
        m_x86.patch_jump(skip_dot, m_x86.pos());

        // printf("%s\n", buf) ou printf("%s", buf)
        std::string fmt = has_newline ? "%s\n" : "%s";
        m_x86.emit_lea_reg_rbp_offset(RDX, buf_off);
        emit_lea_rdata(RCX, add_string_literal(fmt));
        emit_extern_call("printf");
    }

    // Verifica se uma expressao e uma chamada a funcao importada com retorno "string"
    bool is_lib_text_call(const Expr& expr) const {
        if (!expr.is<CallExpr>()) return false;
        auto& call = expr.as<CallExpr>();
        if (!call.callee || !call.callee->is<IdentifierExpr>()) return false;
        auto& name = call.callee->as<IdentifierExpr>().name;
        auto it = m_lib_func_return.find(name);
        return it != m_lib_func_return.end() && it->second == "string";
    }

    void gen_print_call(const CallExpr& e, const std::string& fn_name) {
        if (e.args.empty()) return;

        // Determinar se tem newline (print = com \n, printl = sem \n)
        // print_x = com \n, printl_x = sem \n
        bool has_newline = !fn_name.starts_with("printl");

        // Determinar cor ANSI
        // _y = yellow \033[33m, _r = red \033[31m, _b = blue \033[34m, _g = green \033[32m
        std::string color_prefix;
        std::string color_suffix;
        if (fn_name.ends_with("_y")) {
            color_prefix = "\033[33m";
            color_suffix = "\033[0m";
        } else if (fn_name.ends_with("_r")) {
            color_prefix = "\033[31m";
            color_suffix = "\033[0m";
        } else if (fn_name.ends_with("_b")) {
            color_prefix = "\033[34m";
            color_suffix = "\033[0m";
        } else if (fn_name.ends_with("_g")) {
            color_prefix = "\033[32m";
            color_suffix = "\033[0m";
        }

        bool has_color = !color_prefix.empty();

        // Emitir prefixo de cor se necessario
        if (has_color) {
            emit_lea_rdata(RCX, add_string_literal(color_prefix));
            emit_extern_call("printf");
        }

        // Gerar o argumento
        auto arg_type = m_scope.infer_expr_type(*e.args[0]);
        bool is_string = (arg_type == VarType::String ||
                          e.args[0]->is<StringLiteralExpr>() ||
                          e.args[0]->is<InterpolatedStringExpr>() ||
                          is_lib_text_call(*e.args[0]));
        bool is_float = (arg_type == VarType::Float ||
                         e.args[0]->is<FloatLiteralExpr>());

        // Prefixo Nf: forca formatacao float com N casas decimais
        bool has_float_fmt = (e.float_precision >= 0);
        // Interpolated strings ja aplicam float_precision internamente no sprintf,
        // entao o print externo deve tratar como string
        bool is_interp = e.args[0]->is<InterpolatedStringExpr>();
        if (has_float_fmt && !is_interp) is_float = true;

        // Propagar precisao para interpolated strings
        int old_precision = m_float_precision;
        if (has_float_fmt) m_float_precision = e.float_precision;
        gen_expr(*e.args[0]);
        m_float_precision = old_precision;

        if (is_string && (!has_float_fmt || is_interp)) {
            std::string fmt = has_newline ? "%s\n" : "%s";
            m_x86.emit_push(RAX);
            emit_lea_rdata(RCX, add_string_literal(fmt));
            m_x86.emit_pop(RDX);
        } else if (is_float) {
            // RAX contem bits IEEE 754 do double, ou um inteiro que precisa ser convertido.
            if (arg_type != VarType::Float && !e.args[0]->is<FloatLiteralExpr>()) {
                bool expr_produced_float = (has_float_fmt && e.args[0]->is<BinaryExpr>());
                if (!expr_produced_float) {
                    m_x86.emit_cvtsi2sd_xmm_reg(xmm::XMM0, RAX);
                    m_x86.emit_movq_reg_xmm(RAX, xmm::XMM0);
                }
            }

            if (has_float_fmt) {
                // Formato explicito Nf: — usar printf direto com %.Nf
                std::string fmt = "%." + std::to_string(e.float_precision) + "f";
                if (has_newline) fmt += "\n";
                m_x86.emit_mov_reg_reg(RDX, RAX);
                m_x86.emit_movq_xmm_reg(xmm::XMM1, RAX);
                emit_lea_rdata(RCX, add_string_literal(fmt));
            } else {
                // Formato padrao: sprintf com %.6f, depois trim zeros
                emit_float_smart_print(RAX, has_newline && !has_color);
                // Emitir sufixo de cor (reset) se necessario
                if (has_color) {
                    emit_lea_rdata(RCX, add_string_literal(color_suffix));
                    emit_extern_call("printf");
                    if (has_newline) {
                        emit_lea_rdata(RCX, add_string_literal("\n"));
                        emit_extern_call("printf");
                    }
                }
                return;
            }
        } else {
            std::string fmt = has_newline ? "%lld\n" : "%lld";
            m_x86.emit_mov_reg_reg(RDX, RAX);
            emit_lea_rdata(RCX, add_string_literal(fmt));
        }

        emit_extern_call("printf");

        // Emitir sufixo de cor (reset) se necessario
        if (has_color) {
            emit_lea_rdata(RCX, add_string_literal(color_suffix));
            emit_extern_call("printf");
        }
    }

    // ========================================================================
    // Assign
    // ========================================================================

    void gen_expr_impl(const AssignExpr& e) {
        if (!e.target->is<IdentifierExpr>()) return;

        auto& name = e.target->as<IdentifierExpr>().name;
        auto* var = m_scope.find_var(name);
        if (!var) {
            VarType vt = m_scope.infer_expr_type(*e.value);
            gen_expr(*e.value);
            auto& new_var = m_scope.declare_var(name, false, vt);
            m_x86.emit_mov_rbp_offset_reg(new_var.stack_offset, RAX);
            return;
        }

        if (e.op == TokenType::Assign) {
            VarType vt = m_scope.infer_expr_type(*e.value);
            gen_expr(*e.value);
            if (vt != VarType::Unknown) var->type = vt;
            m_x86.emit_mov_rbp_offset_reg(var->stack_offset, RAX);
        } else {
            bool is_float = (var->type == VarType::Float);

            // /= sempre produz float (estilo Python 3)
            if (e.op == TokenType::SlashAssign) is_float = true;

            m_x86.emit_mov_reg_rbp_offset(RAX, var->stack_offset);
            m_x86.emit_push(RAX);
            gen_expr(*e.value);
            VarType val_type = m_scope.infer_expr_type(*e.value);
            if (val_type == VarType::Float) is_float = true;

            if (is_float) {
                // RAX = valor direito, stack top = valor esquerdo (var)
                emit_load_xmm_from_rax(xmm::XMM1, val_type);
                m_x86.emit_pop(RAX);
                emit_load_xmm_from_rax(xmm::XMM0, var->type);

                switch (e.op) {
                    case TokenType::PlusAssign:    m_x86.emit_addsd_xmm_xmm(xmm::XMM0, xmm::XMM1); break;
                    case TokenType::MinusAssign:   m_x86.emit_subsd_xmm_xmm(xmm::XMM0, xmm::XMM1); break;
                    case TokenType::StarAssign:    m_x86.emit_mulsd_xmm_xmm(xmm::XMM0, xmm::XMM1); break;
                    case TokenType::SlashAssign:   m_x86.emit_divsd_xmm_xmm(xmm::XMM0, xmm::XMM1); break;
                    case TokenType::PercentAssign: {
                        m_x86.emit_movsd_xmm_xmm(xmm::XMM2, xmm::XMM0);
                        m_x86.emit_divsd_xmm_xmm(xmm::XMM2, xmm::XMM1);
                        m_x86.emit_cvttsd2si_reg_xmm(RAX, xmm::XMM2);
                        m_x86.emit_cvtsi2sd_xmm_reg(xmm::XMM2, RAX);
                        m_x86.emit_mulsd_xmm_xmm(xmm::XMM2, xmm::XMM1);
                        m_x86.emit_subsd_xmm_xmm(xmm::XMM0, xmm::XMM2);
                        break;
                    }
                    default: break;
                }
                m_x86.emit_movq_reg_xmm(RAX, xmm::XMM0);
                var->type = VarType::Float;
            } else {
                m_x86.emit_mov_reg_reg(RCX, RAX);
                m_x86.emit_pop(RAX);

                switch (e.op) {
                    case TokenType::PlusAssign:    m_x86.emit_add_reg_reg(RAX, RCX); break;
                    case TokenType::MinusAssign:   m_x86.emit_sub_reg_reg(RAX, RCX); break;
                    case TokenType::StarAssign:    m_x86.emit_imul_reg_reg(RAX, RCX); break;
                    case TokenType::PercentAssign:
                        m_x86.emit_cqo(); m_x86.emit_idiv_reg(RCX);
                        m_x86.emit_mov_reg_reg(RAX, RDX);
                        break;
                    default: break;
                }
            }
            m_x86.emit_mov_rbp_offset_reg(var->stack_offset, RAX);
        }
    }

    // ========================================================================
    // Interpolated string
    // ========================================================================

    void gen_expr_impl(const InterpolatedStringExpr& e) {
        if (e.parts.empty()) {
            emit_lea_rdata(RAX, add_string_literal(""));
            return;
        }

        std::string fmt;
        std::vector<size_t> expr_indices;

        for (size_t i = 0; i < e.parts.size(); i++) {
            auto& part = *e.parts[i];
            if (part.is<StringLiteralExpr>()) {
                auto& text = part.as<StringLiteralExpr>().value;
                for (char c : text) {
                    if (c == '%') fmt += "%%";
                    else fmt += c;
                }
            } else {
                VarType vt = m_scope.infer_expr_type(part);
                if (vt == VarType::String) {
                    fmt += "%s";
                } else if (vt == VarType::Float || (m_float_precision >= 0 && vt != VarType::String)) {
                    if (m_float_precision >= 0)
                        fmt += "%." + std::to_string(m_float_precision) + "f";
                    else
                        fmt += "%.6g";
                } else {
                    fmt += "%lld";
                }
                expr_indices.push_back(i);
            }
        }

        int32_t buf_rbp_offset = m_scope.reserve_stack(512);
        size_t nexprs = expr_indices.size();
        int32_t args_base = m_scope.reserve_stack(static_cast<int32_t>(nexprs * 8));

        // Guardar tipos dos args para saber quais precisam de XMM (varargs ABI)
        std::vector<VarType> expr_types;
        for (size_t i = 0; i < nexprs; i++) {
            expr_types.push_back(m_scope.infer_expr_type(*e.parts[expr_indices[i]]));
        }

        for (size_t i = 0; i < nexprs; i++) {
            gen_expr(*e.parts[expr_indices[i]]);
            // Se float_precision ativo e tipo e int, converter para double (bits IEEE 754)
            if (m_float_precision >= 0 && expr_types[i] != VarType::Float && expr_types[i] != VarType::String) {
                m_x86.emit_cvtsi2sd_xmm_reg(xmm::XMM0, RAX);
                m_x86.emit_movq_reg_xmm(RAX, xmm::XMM0);
                expr_types[i] = VarType::Float; // atualizar tipo para XMM handling
            }
            m_x86.emit_mov_rbp_offset_reg(args_base + static_cast<int32_t>(i * 8), RAX);
        }

        if (nexprs > 2) {
            // Windows x64 ABI: sprintf(RCX=buf, RDX=fmt, R8=a0, R9=a1, [rsp+32]=a2, [rsp+40]=a3...)
            // Total de args para sprintf = 2 (buf, fmt) + nexprs
            // Os primeiros 4 vao nos regs, os restantes na stack a partir de [rsp+32]
            size_t total_args = 2 + nexprs;
            size_t stack_args = (total_args > 4) ? (total_args - 4) : 0;
            size_t frame = 32 + stack_args * 8; // shadow(32) + overflow args
            frame = (frame + 15) & ~15;
            m_x86.emit_sub_rsp_imm32(static_cast<int32_t>(frame));

            // Args que transbordam (a2, a3, ...) vao em [rsp+32], [rsp+40], ...
            for (size_t i = 2; i < nexprs; i++) {
                m_x86.emit_mov_reg_rbp_offset(RAX, args_base + static_cast<int32_t>(i * 8));
                m_x86.emit_mov_rsp_offset_reg(static_cast<int32_t>(32 + (i - 2) * 8), RAX);
            }

            // R8 = a0, R9 = a1
            m_x86.emit_mov_reg_rbp_offset(R8, args_base);
            // Windows varargs: float em R8 tambem precisa estar em XMM2
            if (nexprs >= 1 && expr_types[0] == VarType::Float) {
                m_x86.emit_movq_xmm_reg(xmm::XMM2, R8);
            }
            if (nexprs >= 2) {
                m_x86.emit_mov_reg_rbp_offset(R9, args_base + 8);
                // Windows varargs: float em R9 tambem precisa estar em XMM3
                if (expr_types[1] == VarType::Float) {
                    m_x86.emit_movq_xmm_reg(xmm::XMM3, R9);
                }
            }

            emit_lea_rdata(RDX, add_string_literal(fmt));
            m_x86.emit_lea_reg_rbp_offset(RCX, buf_rbp_offset);
            emit_extern_call("sprintf");
            m_x86.emit_add_rsp_imm32(static_cast<int32_t>(frame));
        } else {
            if (nexprs == 2) {
                m_x86.emit_mov_reg_rbp_offset(R9, args_base + 8);
                if (expr_types[1] == VarType::Float) {
                    m_x86.emit_movq_xmm_reg(xmm::XMM3, R9);
                }
                m_x86.emit_mov_reg_rbp_offset(R8, args_base);
                if (expr_types[0] == VarType::Float) {
                    m_x86.emit_movq_xmm_reg(xmm::XMM2, R8);
                }
            } else if (nexprs == 1) {
                m_x86.emit_mov_reg_rbp_offset(R8, args_base);
                if (expr_types[0] == VarType::Float) {
                    m_x86.emit_movq_xmm_reg(xmm::XMM2, R8);
                }
            }

            emit_lea_rdata(RDX, add_string_literal(fmt));
            m_x86.emit_lea_reg_rbp_offset(RCX, buf_rbp_offset);
            emit_extern_call("sprintf");
        }

        m_x86.emit_lea_reg_rbp_offset(RAX, buf_rbp_offset);
    }

    // Stubs expressoes
    void gen_expr_impl(const RangeExpr&) {}
    void gen_expr_impl(const IndexExpr&) {}
    void gen_expr_impl(const SliceExpr&) {}
    void gen_expr_impl(const MemberExpr&) {}
    void gen_expr_impl(const ScopeResolutionExpr&) {}
    void gen_expr_impl(const IfExpr&) {}
    void gen_expr_impl(const MatchExpr&) {}
    void gen_expr_impl(const LambdaExpr&) {}
    void gen_expr_impl(const ListExpr&) {}
    void gen_expr_impl(const MapExpr&) {}
    void gen_expr_impl(const SetExpr&) {}
    void gen_expr_impl(const TupleExpr&) {}
    void gen_expr_impl(const ComprehensionExpr&) {}
    void gen_expr_impl(const PipeExpr&) {}
    void gen_expr_impl(const RefExpr&) {}
    void gen_expr_impl(const CastExpr&) {}
    void gen_expr_impl(const AwaitExpr&) {}
    void gen_expr_impl(const QuestionExpr&) {}
    void gen_expr_impl(const TypeAnnotation&) {}
};

} // namespace atomic
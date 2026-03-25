// parser.hpp
// Parser for the Atomic programming language - builds AST from token stream

#pragma once

#include "ast.hpp"
#include <functional>

namespace atomic {

// ============================================================================
// Parser Error
// ============================================================================

struct ParseError : std::runtime_error {
    SourceLocation loc;

    ParseError(const std::string& msg, SourceLocation loc)
        : std::runtime_error(std::format("{}:{}: {}", loc.line, loc.column, msg))
        , loc(loc) {}
};

// ============================================================================
// Parser
// ============================================================================

class Parser {
public:
    explicit Parser(std::vector<Token> tokens, std::string filename = "<stdin>")
        : m_tokens(std::move(tokens))
        , m_filename(std::move(filename)) {}

    Program parse() {
        Program prog;
        prog.filename = m_filename;

        skip_newlines();
        while (!at_end()) {
            prog.statements.push_back(parse_statement());
            skip_newlines();
        }

        return prog;
    }

private:
    std::vector<Token> m_tokens;
    std::string        m_filename;
    uint32_t           m_pos = 0;

    // ========================================================================
    // Token helpers
    // ========================================================================

    bool at_end() const {
        return m_pos >= m_tokens.size() || m_tokens[m_pos].is(TokenType::Eof);
    }

    const Token& current() const {
        if (m_pos >= m_tokens.size()) return m_tokens.back();
        return m_tokens[m_pos];
    }

    const Token& peek_token(int offset = 0) const {
        uint32_t idx = m_pos + offset;
        if (idx >= m_tokens.size()) return m_tokens.back();
        return m_tokens[idx];
    }

    const Token& advance_token() {
        const Token& tok = current();
        if (!at_end()) m_pos++;
        return tok;
    }

    bool check(TokenType type) const {
        return !at_end() && current().is(type);
    }

    bool match(TokenType type) {
        if (check(type)) {
            advance_token();
            return true;
        }
        return false;
    }

    const Token& expect(TokenType type, const std::string& msg = "") {
        if (check(type)) {
            return advance_token();
        }
        std::string err = msg.empty()
            ? std::format("expected '{}', got '{}'", token_type_str(type), token_type_str(current().type))
            : msg;
        error(err);
    }

    void skip_newlines() {
        while (check(TokenType::Newline)) advance_token();
    }

    [[noreturn]] void error(const std::string& msg) {
        throw ParseError(msg, current().loc);
    }

    // ========================================================================
    // Expression helpers
    // ========================================================================

    template<typename T>
    ExprPtr make_expr(SourceLocation loc, T&& data) {
        auto e = std::make_unique<Expr>();
        e->loc = loc;
        e->data = std::forward<T>(data);
        return e;
    }

    template<typename T>
    StmtPtr make_stmt(SourceLocation loc, T&& data) {
        auto s = std::make_unique<Stmt>();
        s->loc = loc;
        s->data = std::forward<T>(data);
        return s;
    }

    // ========================================================================
    // Statement parsing
    // ========================================================================

    StmtPtr parse_statement() {
        skip_newlines();
        auto loc = current().loc;

        // Derive attribute
        if (check(TokenType::Hash) && peek_token(1).is(TokenType::LeftBracket)) {
            return parse_derive();
        }

        // pub modifier
        bool is_pub = false;
        if (check(TokenType::KwPub)) {
            is_pub = true;
            advance_token();
        }

        // async modifier
        bool is_async = false;
        if (check(TokenType::KwAsync)) {
            is_async = true;
            advance_token();
        }

        switch (current().type) {
            case TokenType::KwFn:       return parse_fn_decl(is_pub, is_async);
            case TokenType::KwStruct:   return parse_struct_decl(is_pub);
            case TokenType::KwEnum:     return parse_enum_decl(is_pub);
            case TokenType::KwTrait:    return parse_trait_decl(is_pub);
            case TokenType::KwImpl:     return parse_impl_block();
            case TokenType::KwIf:       return parse_if_stmt();
            case TokenType::KwFor:      return parse_for_stmt();
            case TokenType::KwWhile:    return parse_while_stmt();
            case TokenType::KwLoop:     return parse_loop_stmt();
            case TokenType::KwMatch:    return parse_match_stmt();
            case TokenType::KwReturn:   return parse_return_stmt();
            case TokenType::KwBreak:    return parse_break_stmt();
            case TokenType::KwContinue: return parse_continue_stmt();
            case TokenType::KwImport:   return parse_import_stmt();
            case TokenType::KwFrom:     return parse_from_import_stmt();
            case TokenType::KwMod:      return parse_mod_decl();
            case TokenType::KwTry:      return parse_try_stmt();
            case TokenType::KwDefer:    return parse_defer_stmt();
            case TokenType::KwDrop:     return parse_drop_stmt();
            case TokenType::KwUnsafe:   return parse_unsafe_block();
            case TokenType::KwType:     return parse_type_alias(is_pub);
            case TokenType::KwConst:    return parse_const_decl();
            default:
                if (is_pub) error("'pub' can only precede fn, struct, enum, trait, or type");
                if (is_async) error("'async' can only precede fn");
                return parse_expr_or_assign_stmt();
        }
    }

    // --- Variable / assignment ---
    StmtPtr parse_expr_or_assign_stmt() {
        auto loc = current().loc;
        auto expr = parse_expression();

        // Check for type annotation: name: type = value
        if (expr->is<IdentifierExpr>() && check(TokenType::Colon)) {
            auto name = expr->as<IdentifierExpr>().name;
            advance_token(); // :
            auto type_expr = parse_type_expression();

            ExprPtr init = nullptr;
            if (match(TokenType::Assign)) {
                init = parse_expression();
            }
            expect_line_end();

            VarDeclStmt decl;
            decl.name = std::move(name);
            decl.type_expr = std::move(type_expr);
            decl.initializer = std::move(init);
            decl.is_const = false;
            return make_stmt(loc, std::move(decl));
        }

        // Check for assignment operators
        if (is_assign_op(current().type)) {
            auto op = current().type;
            advance_token();
            auto value = parse_expression();
            expect_line_end();

            // Simple assignment to new name → variable declaration
            if (op == TokenType::Assign && expr->is<IdentifierExpr>()) {
                VarDeclStmt decl;
                decl.name = expr->as<IdentifierExpr>().name;
                decl.type_expr = nullptr;
                decl.initializer = std::move(value);
                decl.is_const = false;
                return make_stmt(loc, std::move(decl));
            }

            // Compound assignment or assignment to member/index
            AssignExpr assign;
            assign.target = std::move(expr);
            assign.op = op;
            assign.value = std::move(value);
            auto assign_expr = make_expr(loc, std::move(assign));

            ExprStmt es;
            es.expr = std::move(assign_expr);
            return make_stmt(loc, std::move(es));
        }

        expect_line_end();
        ExprStmt es;
        es.expr = std::move(expr);
        return make_stmt(loc, std::move(es));
    }

    bool is_assign_op(TokenType t) const {
        switch (t) {
            case TokenType::Assign:
            case TokenType::PlusAssign:
            case TokenType::MinusAssign:
            case TokenType::StarAssign:
            case TokenType::SlashAssign:
            case TokenType::PercentAssign:
            case TokenType::DoubleStarAssign:
            case TokenType::AmpersandAssign:
            case TokenType::PipeAssign:
            case TokenType::CaretAssign:
            case TokenType::ShiftLeftAssign:
            case TokenType::ShiftRightAssign:
                return true;
            default:
                return false;
        }
    }

    void expect_line_end() {
        if (check(TokenType::Newline) || check(TokenType::Eof) || check(TokenType::Dedent)) {
            if (check(TokenType::Newline)) advance_token();
        }
    }

    // --- const declaration ---
    StmtPtr parse_const_decl() {
        auto loc = current().loc;
        expect(TokenType::KwConst);
        auto name = expect(TokenType::Identifier).value;

        ExprPtr type_expr = nullptr;
        if (match(TokenType::Colon)) {
            type_expr = parse_type_expression();
        }

        expect(TokenType::Assign);
        auto init = parse_expression();
        expect_line_end();

        VarDeclStmt decl;
        decl.name = std::move(name);
        decl.type_expr = std::move(type_expr);
        decl.initializer = std::move(init);
        decl.is_const = true;
        return make_stmt(loc, std::move(decl));
    }

    // --- fn declaration ---
    StmtPtr parse_fn_decl(bool is_pub, bool is_async) {
        auto loc = current().loc;
        expect(TokenType::KwFn);
        auto name = expect(TokenType::Identifier).value;

        // Generic params [T, U: Trait]
        auto [gen_params, gen_bounds] = parse_optional_generics();

        // Parameters
        expect(TokenType::LeftParen);
        auto params = parse_fn_params();
        expect(TokenType::RightParen);

        // Return type
        ExprPtr ret_type = nullptr;
        if (match(TokenType::Arrow)) {
            ret_type = parse_type_expression();
        }

        expect(TokenType::Colon);
        auto body = parse_block();

        FnDeclStmt fn;
        fn.name = std::move(name);
        fn.params = std::move(params);
        fn.return_type = std::move(ret_type);
        fn.body = std::move(body);
        fn.is_pub = is_pub;
        fn.is_async = is_async;
        fn.generic_params = std::move(gen_params);
        fn.generic_bounds = std::move(gen_bounds);
        return make_stmt(loc, std::move(fn));
    }

    std::vector<FnParam> parse_fn_params() {
        std::vector<FnParam> params;
        if (check(TokenType::RightParen)) return params;

        do {
            FnParam p;
            // self or mut self
            if (check(TokenType::KwSelf)) {
                p.is_self = true;
                p.name = "self";
                advance_token();
                params.push_back(std::move(p));
                continue;
            }
            if (check(TokenType::KwRef) && peek_token(1).is(TokenType::KwSelf)) {
                advance_token(); // ref
                p.is_self = true;
                p.name = "self";
                advance_token(); // self
                params.push_back(std::move(p));
                continue;
            }
            if (check(TokenType::KwRef) && peek_token(1).is(TokenType::Identifier)
                && peek_token(1).value == "mut" && peek_token(2).is(TokenType::KwSelf)) {
                advance_token(); // ref
                advance_token(); // mut
                p.is_self = true;
                p.is_mut_self = true;
                p.name = "self";
                advance_token(); // self
                params.push_back(std::move(p));
                continue;
            }

            // Regular param: name [: type] [= default]
            p.name = expect(TokenType::Identifier).value;

            // Tipo e opcional: fn soma(a, b) ou fn soma(a: i32, b: i32)
            if (match(TokenType::Colon)) {
                p.type_expr = parse_type_expression();
            }

            if (match(TokenType::Assign)) {
                p.default_value = parse_expression();
            }

            params.push_back(std::move(p));
        } while (match(TokenType::Comma));

        return params;
    }

    std::pair<std::vector<std::string>, std::vector<ExprPtr>> parse_optional_generics() {
        std::vector<std::string> params;
        std::vector<ExprPtr> bounds;

        if (!match(TokenType::LeftBracket)) return {std::move(params), std::move(bounds)};

        do {
            params.push_back(expect(TokenType::Identifier).value);
            if (match(TokenType::Colon)) {
                bounds.push_back(parse_type_expression());
            } else {
                bounds.push_back(nullptr);
            }
        } while (match(TokenType::Comma));

        expect(TokenType::RightBracket);
        return {std::move(params), std::move(bounds)};
    }

    // --- Block (indented) ---
    StmtList parse_block() {
        StmtList stmts;
        skip_newlines();
        expect(TokenType::Indent);
        skip_newlines();

        while (!check(TokenType::Dedent) && !at_end()) {
            stmts.push_back(parse_statement());
            skip_newlines();
        }

        if (check(TokenType::Dedent)) advance_token();
        return stmts;
    }

    // --- if statement ---
    StmtPtr parse_if_stmt() {
        auto loc = current().loc;
        IfStmt if_stmt;

        // if branch
        expect(TokenType::KwIf);
        auto cond = parse_expression();
        expect(TokenType::Colon);

        IfBranch branch;
        branch.condition = std::move(cond);
        branch.body = parse_block();
        if_stmt.branches.push_back(std::move(branch));

        // elif branches
        skip_newlines();
        while (check(TokenType::KwElif)) {
            advance_token();
            auto elif_cond = parse_expression();
            expect(TokenType::Colon);

            IfBranch elif_branch;
            elif_branch.condition = std::move(elif_cond);
            elif_branch.body = parse_block();
            if_stmt.branches.push_back(std::move(elif_branch));
            skip_newlines();
        }

        // else branch
        if (check(TokenType::KwElse)) {
            advance_token();
            expect(TokenType::Colon);

            IfBranch else_branch;
            else_branch.condition = nullptr;
            else_branch.body = parse_block();
            if_stmt.branches.push_back(std::move(else_branch));
        }

        return make_stmt(loc, std::move(if_stmt));
    }

    // --- for statement ---
    StmtPtr parse_for_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwFor);

        ForStmt for_stmt;

        // Check for tuple destructuring: (a, b) in ...
        if (match(TokenType::LeftParen)) {
            for_stmt.var_name = expect(TokenType::Identifier).value;
            expect(TokenType::Comma);
            for_stmt.second_var = expect(TokenType::Identifier).value;
            expect(TokenType::RightParen);
        } else {
            for_stmt.var_name = expect(TokenType::Identifier).value;
        }

        expect(TokenType::KwIn);
        for_stmt.iterable = parse_expression();
        expect(TokenType::Colon);
        for_stmt.body = parse_block();

        return make_stmt(loc, std::move(for_stmt));
    }

    // --- while statement ---
    StmtPtr parse_while_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwWhile);

        WhileStmt ws;
        ws.condition = parse_expression();
        expect(TokenType::Colon);
        ws.body = parse_block();

        return make_stmt(loc, std::move(ws));
    }

    // --- loop statement ---
    StmtPtr parse_loop_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwLoop);
        expect(TokenType::Colon);

        LoopStmt ls;
        ls.body = parse_block();

        return make_stmt(loc, std::move(ls));
    }

    // --- match statement ---
    StmtPtr parse_match_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwMatch);
        auto subject = parse_expression();
        expect(TokenType::Colon);

        MatchStmt ms;
        ms.subject = std::move(subject);

        skip_newlines();
        expect(TokenType::Indent);
        skip_newlines();

        while (!check(TokenType::Dedent) && !at_end()) {
            MatchArm arm;
            arm.pattern = parse_expression();

            // Guard: if condition
            if (match(TokenType::KwIf)) {
                arm.guard = parse_expression();
            }

            expect(TokenType::Colon);

            // Single-line or block
            if (check(TokenType::Newline) || check(TokenType::Indent)) {
                arm.body = parse_block();
            } else {
                auto stmt = parse_statement();
                arm.body.push_back(std::move(stmt));
            }

            ms.arms.push_back(std::move(arm));
            skip_newlines();
        }

        if (check(TokenType::Dedent)) advance_token();

        return make_stmt(loc, std::move(ms));
    }

    // --- return statement ---
    StmtPtr parse_return_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwReturn);

        ReturnStmt rs;
        if (!check(TokenType::Newline) && !check(TokenType::Eof) && !check(TokenType::Dedent)) {
            rs.value = parse_expression();
        }
        expect_line_end();

        return make_stmt(loc, std::move(rs));
    }

    // --- break statement ---
    StmtPtr parse_break_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwBreak);

        BreakStmt bs;
        if (!check(TokenType::Newline) && !check(TokenType::Eof) && !check(TokenType::Dedent)) {
            bs.value = parse_expression();
        }
        expect_line_end();

        return make_stmt(loc, std::move(bs));
    }

    // --- continue statement ---
    StmtPtr parse_continue_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwContinue);
        expect_line_end();
        return make_stmt(loc, ContinueStmt{});
    }

    // --- struct declaration ---
    StmtPtr parse_struct_decl(bool is_pub) {
        auto loc = current().loc;
        expect(TokenType::KwStruct);
        auto name = expect(TokenType::Identifier).value;
        auto [gen_params, gen_bounds] = parse_optional_generics();
        expect(TokenType::Colon);

        StructDeclStmt sd;
        sd.name = std::move(name);
        sd.is_pub = is_pub;
        sd.generic_params = std::move(gen_params);

        skip_newlines();
        expect(TokenType::Indent);
        skip_newlines();

        while (!check(TokenType::Dedent) && !at_end()) {
            StructField field;
            if (match(TokenType::KwPub)) {
                field.is_pub = true;
            }
            field.name = expect(TokenType::Identifier).value;
            expect(TokenType::Colon);
            field.type_expr = parse_type_expression();
            expect_line_end();
            sd.fields.push_back(std::move(field));
            skip_newlines();
        }

        if (check(TokenType::Dedent)) advance_token();
        return make_stmt(loc, std::move(sd));
    }

    // --- enum declaration ---
    StmtPtr parse_enum_decl(bool is_pub) {
        auto loc = current().loc;
        expect(TokenType::KwEnum);
        auto name = expect(TokenType::Identifier).value;
        auto [gen_params, gen_bounds] = parse_optional_generics();
        expect(TokenType::Colon);

        EnumDeclStmt ed;
        ed.name = std::move(name);
        ed.is_pub = is_pub;
        ed.generic_params = std::move(gen_params);

        skip_newlines();
        expect(TokenType::Indent);
        skip_newlines();

        while (!check(TokenType::Dedent) && !at_end()) {
            EnumVariant variant;
            variant.name = expect(TokenType::Identifier).value;

            if (match(TokenType::LeftParen)) {
                if (!check(TokenType::RightParen)) {
                    do {
                        variant.fields.push_back(parse_type_expression());
                    } while (match(TokenType::Comma));
                }
                expect(TokenType::RightParen);
            }

            expect_line_end();
            ed.variants.push_back(std::move(variant));
            skip_newlines();
        }

        if (check(TokenType::Dedent)) advance_token();
        return make_stmt(loc, std::move(ed));
    }

    // --- impl block ---
    StmtPtr parse_impl_block() {
        auto loc = current().loc;
        expect(TokenType::KwImpl);

        ImplBlock ib;
        auto [gen_params, gen_bounds] = parse_optional_generics();
        ib.generic_params = std::move(gen_params);

        auto first_name = expect(TokenType::Identifier).value;

        // impl Trait for Type:
        if (match(TokenType::KwFor)) {
            ib.trait_name = std::move(first_name);
            ib.target_type = expect(TokenType::Identifier).value;
        } else {
            ib.target_type = std::move(first_name);
        }

        expect(TokenType::Colon);

        skip_newlines();
        expect(TokenType::Indent);
        skip_newlines();

        while (!check(TokenType::Dedent) && !at_end()) {
            bool fn_pub = false;
            bool fn_async = false;
            if (match(TokenType::KwPub)) fn_pub = true;
            if (match(TokenType::KwAsync)) fn_async = true;
            ib.methods.push_back(parse_fn_decl(fn_pub, fn_async));
            skip_newlines();
        }

        if (check(TokenType::Dedent)) advance_token();
        return make_stmt(loc, std::move(ib));
    }

    // --- trait declaration ---
    StmtPtr parse_trait_decl(bool is_pub) {
        auto loc = current().loc;
        expect(TokenType::KwTrait);
        auto name = expect(TokenType::Identifier).value;
        auto [gen_params, gen_bounds] = parse_optional_generics();

        TraitDeclStmt td;
        td.name = std::move(name);
        td.is_pub = is_pub;
        td.generic_params = std::move(gen_params);

        expect(TokenType::Colon);

        skip_newlines();
        expect(TokenType::Indent);
        skip_newlines();

        while (!check(TokenType::Dedent) && !at_end()) {
            bool fn_async = false;
            if (match(TokenType::KwAsync)) fn_async = true;
            expect(TokenType::KwFn);
            auto method_name = expect(TokenType::Identifier).value;

            expect(TokenType::LeftParen);
            auto params = parse_fn_params();
            expect(TokenType::RightParen);

            ExprPtr ret_type = nullptr;
            if (match(TokenType::Arrow)) {
                ret_type = parse_type_expression();
            }

            TraitMethod tm;
            tm.name = std::move(method_name);
            tm.params = std::move(params);
            tm.return_type = std::move(ret_type);
            tm.is_async = fn_async;

            // Default implementation or just declaration
            if (match(TokenType::Colon)) {
                tm.default_body = parse_block();
            } else {
                expect_line_end();
            }

            td.methods.push_back(std::move(tm));
            skip_newlines();
        }

        if (check(TokenType::Dedent)) advance_token();
        return make_stmt(loc, std::move(td));
    }

    // --- import statement ---
    StmtPtr parse_import_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwImport);

        ImportStmt is;

        // import extern "lib"
        if (match(TokenType::KwExtern)) {
            is.is_extern = true;
            is.module_path = expect(TokenType::StringLiteral).value;
            expect_line_end();
            return make_stmt(loc, std::move(is));
        }

        is.module_path = parse_dotted_name();
        expect_line_end();
        return make_stmt(loc, std::move(is));
    }

    // --- from ... import ... ---
    StmtPtr parse_from_import_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwFrom);

        ImportStmt is;
        is.module_path = parse_dotted_name();

        expect(TokenType::KwImport);

        // from mod import *
        if (match(TokenType::Star)) {
            is.is_wildcard = true;
            expect_line_end();
            return make_stmt(loc, std::move(is));
        }

        // from mod import A, B as C, D
        do {
            auto item_name = expect(TokenType::Identifier).value;
            std::string alias = item_name;
            if (match(TokenType::KwAs)) {
                alias = expect(TokenType::Identifier).value;
            }
            is.items.push_back({std::move(item_name), std::move(alias)});
        } while (match(TokenType::Comma));

        expect_line_end();
        return make_stmt(loc, std::move(is));
    }

    std::string parse_dotted_name() {
        std::string name = expect(TokenType::Identifier).value;
        while (match(TokenType::Dot)) {
            name += ".";
            name += expect(TokenType::Identifier).value;
        }
        return name;
    }

    // --- mod declaration ---
    StmtPtr parse_mod_decl() {
        auto loc = current().loc;
        expect(TokenType::KwMod);

        ModDeclStmt md;
        md.name = parse_dotted_name();
        expect_line_end();

        return make_stmt(loc, std::move(md));
    }

    // --- try/catch ---
    StmtPtr parse_try_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwTry);
        expect(TokenType::Colon);

        TryStmt ts;
        ts.body = parse_block();

        skip_newlines();
        while (check(TokenType::KwCatch)) {
            advance_token();
            TryStmt::CatchClause clause;

            if (!check(TokenType::Colon)) {
                if (check(TokenType::Identifier)) {
                    clause.error_type = expect(TokenType::Identifier).value;
                    if (match(TokenType::KwAs)) {
                        clause.var_name = expect(TokenType::Identifier).value;
                    }
                }
            }

            expect(TokenType::Colon);
            clause.body = parse_block();
            ts.catches.push_back(std::move(clause));
            skip_newlines();
        }

        return make_stmt(loc, std::move(ts));
    }

    // --- defer ---
    StmtPtr parse_defer_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwDefer);

        DeferStmt ds;
        ds.expr = parse_expression();
        expect_line_end();

        return make_stmt(loc, std::move(ds));
    }

    // --- drop ---
    StmtPtr parse_drop_stmt() {
        auto loc = current().loc;
        expect(TokenType::KwDrop);

        DropStmt ds;
        ds.expr = parse_expression();
        expect_line_end();

        return make_stmt(loc, std::move(ds));
    }

    // --- unsafe block ---
    StmtPtr parse_unsafe_block() {
        auto loc = current().loc;
        expect(TokenType::KwUnsafe);
        expect(TokenType::Colon);

        UnsafeBlock ub;
        ub.body = parse_block();

        return make_stmt(loc, std::move(ub));
    }

    // --- type alias ---
    StmtPtr parse_type_alias(bool is_pub) {
        auto loc = current().loc;
        expect(TokenType::KwType);
        auto name = expect(TokenType::Identifier).value;
        expect(TokenType::Assign);
        auto type_expr = parse_type_expression();
        expect_line_end();

        TypeAliasStmt ta;
        ta.name = std::move(name);
        ta.type_expr = std::move(type_expr);
        ta.is_pub = is_pub;

        return make_stmt(loc, std::move(ta));
    }

    // --- derive ---
    StmtPtr parse_derive() {
        auto loc = current().loc;
        expect(TokenType::Hash);
        expect(TokenType::LeftBracket);
        expect(TokenType::Identifier); // "derive"

        expect(TokenType::LeftParen);
        DeriveStmt ds;
        do {
            ds.traits.push_back(expect(TokenType::Identifier).value);
        } while (match(TokenType::Comma));
        expect(TokenType::RightParen);
        expect(TokenType::RightBracket);
        expect_line_end();

        skip_newlines();
        ds.target = parse_statement();

        return make_stmt(loc, std::move(ds));
    }

    // ========================================================================
    // Type expression parsing
    // ========================================================================

    ExprPtr parse_type_expression() {
        auto loc = current().loc;

        // ref type or ref mut type
        if (check(TokenType::KwRef)) {
            advance_token();
            bool is_mut = false;
            if (check(TokenType::Identifier) && current().value == "mut") {
                is_mut = true;
                advance_token();
            }
            RefExpr re;
            re.operand = parse_type_expression();
            re.is_mut = is_mut;
            return make_expr(loc, std::move(re));
        }

        // ptr[T]
        if (check(TokenType::Identifier) && current().value == "ptr") {
            auto name = advance_token().value;
            if (match(TokenType::LeftBracket)) {
                TypeAnnotation ta;
                ta.name = std::move(name);
                ta.generics.push_back(parse_type_expression());
                expect(TokenType::RightBracket);
                return make_expr(loc, std::move(ta));
            }
            return make_expr(loc, IdentifierExpr{std::move(name)});
        }

        // fn(...) -> T
        if (check(TokenType::KwFn)) {
            advance_token();
            expect(TokenType::LeftParen);
            TypeAnnotation ta;
            ta.name = "fn";
            if (!check(TokenType::RightParen)) {
                do {
                    ta.generics.push_back(parse_type_expression());
                } while (match(TokenType::Comma));
            }
            expect(TokenType::RightParen);
            if (match(TokenType::Arrow)) {
                ta.generics.push_back(parse_type_expression());
            }
            return make_expr(loc, std::move(ta));
        }

        // Tuple type: (T, U)
        if (check(TokenType::LeftParen)) {
            advance_token();
            TypeAnnotation ta;
            ta.name = "tuple";
            do {
                ta.generics.push_back(parse_type_expression());
            } while (match(TokenType::Comma));
            expect(TokenType::RightParen);
            return make_expr(loc, std::move(ta));
        }

        // Array type: [T; N]
        if (check(TokenType::LeftBracket)) {
            advance_token();
            auto elem = parse_type_expression();
            TypeAnnotation ta;
            if (match(TokenType::Comma)) {
                // [T; N] style — using comma for now
                ta.name = "array";
                ta.generics.push_back(std::move(elem));
                ta.generics.push_back(parse_expression());
            } else {
                ta.name = "slice";
                ta.generics.push_back(std::move(elem));
            }
            expect(TokenType::RightBracket);
            return make_expr(loc, std::move(ta));
        }

        // Named type with optional generics: Type[T, U]
        auto name = expect(TokenType::Identifier).value;
        TypeAnnotation ta;
        ta.name = std::move(name);

        if (match(TokenType::LeftBracket)) {
            do {
                ta.generics.push_back(parse_type_expression());
            } while (match(TokenType::Comma));
            expect(TokenType::RightBracket);
        }

        return make_expr(loc, std::move(ta));
    }

    // ========================================================================
    // Expression parsing (Pratt parser)
    // ========================================================================

    enum class Precedence : int {
        None = 0,
        Pipe,           // |>
        Or,             // or
        And,            // and
        Not,            // not (prefix)
        Comparison,     // == != < > <= >= is in
        BitwiseOr,      // |
        BitwiseXor,     // ^
        BitwiseAnd,     // &
        Shift,          // << >>
        Range,          // .. ..=
        Addition,       // + -
        Multiplication, // * / %
        Power,          // **
        Unary,          // - ~ ref await
        Postfix,        // () [] . :: ?
    };

    Precedence get_precedence(TokenType t) const {
        switch (t) {
            case TokenType::PipeArrow:      return Precedence::Pipe;
            case TokenType::KwOr:           return Precedence::Or;
            case TokenType::KwAnd:          return Precedence::And;
            case TokenType::Equal:
            case TokenType::NotEqual:
            case TokenType::Less:
            case TokenType::Greater:
            case TokenType::LessEqual:
            case TokenType::GreaterEqual:
            case TokenType::KwIs:
            case TokenType::KwIn:           return Precedence::Comparison;
            case TokenType::Pipe:           return Precedence::BitwiseOr;
            case TokenType::Caret:          return Precedence::BitwiseXor;
            case TokenType::Ampersand:      return Precedence::BitwiseAnd;
            case TokenType::ShiftLeft:
            case TokenType::ShiftRight:     return Precedence::Shift;
            case TokenType::Range:
            case TokenType::RangeInclusive: return Precedence::Range;
            case TokenType::Plus:
            case TokenType::Minus:          return Precedence::Addition;
            case TokenType::Star:
            case TokenType::Slash:
            case TokenType::Percent:        return Precedence::Multiplication;
            case TokenType::DoubleStar:     return Precedence::Power;
            case TokenType::LeftParen:
            case TokenType::LeftBracket:
            case TokenType::Dot:
            case TokenType::DoubleColon:
            case TokenType::Question:       return Precedence::Postfix;
            default:                        return Precedence::None;
        }
    }

    ExprPtr parse_expression(Precedence min_prec = Precedence::None) {
        auto left = parse_prefix();
        return parse_infix(std::move(left), min_prec);
    }

    ExprPtr parse_infix(ExprPtr left, Precedence min_prec) {
        while (true) {
            auto prec = get_precedence(current().type);
            if (prec <= min_prec && prec != Precedence::Postfix) break;
            if (prec == Precedence::Postfix && prec <= min_prec) break;
            if (prec < min_prec) break;
            // Handle right-to-left for **
            if (prec == min_prec && current().type != TokenType::DoubleStar) break;

            left = parse_infix_expr(std::move(left));
        }
        return left;
    }

    ExprPtr parse_infix_expr(ExprPtr left) {
        auto loc = current().loc;
        auto type = current().type;

        // Postfix
        if (type == TokenType::LeftParen)   return parse_call_expr(std::move(left));
        if (type == TokenType::LeftBracket) return parse_index_expr(std::move(left));
        if (type == TokenType::Dot)         return parse_member_expr(std::move(left));
        if (type == TokenType::DoubleColon) return parse_scope_expr(std::move(left));
        if (type == TokenType::Question) {
            advance_token();
            QuestionExpr qe;
            qe.operand = std::move(left);
            return make_expr(loc, std::move(qe));
        }

        // Pipe
        if (type == TokenType::PipeArrow) {
            advance_token();
            auto right = parse_expression(Precedence::Pipe);
            PipeExpr pe;
            pe.left = std::move(left);
            pe.right = std::move(right);
            return make_expr(loc, std::move(pe));
        }

        // Range
        if (type == TokenType::Range || type == TokenType::RangeInclusive) {
            advance_token();
            bool inclusive = (type == TokenType::RangeInclusive);
            ExprPtr end = nullptr;
            if (!check(TokenType::Newline) && !check(TokenType::Eof) &&
                !check(TokenType::RightBracket) && !check(TokenType::RightParen) &&
                !check(TokenType::Comma) && !check(TokenType::Colon)) {
                end = parse_expression(Precedence::Range);
            }
            RangeExpr re;
            re.start = std::move(left);
            re.end = std::move(end);
            re.inclusive = inclusive;
            return make_expr(loc, std::move(re));
        }

        // Binary operators
        advance_token();
        auto prec = get_precedence(type);
        // Right-associative for **
        auto next_prec = (type == TokenType::DoubleStar)
            ? Precedence(static_cast<int>(prec) - 1)
            : prec;

        auto right = parse_expression(next_prec);

        BinaryExpr be;
        be.op = type;
        be.left = std::move(left);
        be.right = std::move(right);
        return make_expr(loc, std::move(be));
    }

    // --- Call expression ---
    ExprPtr parse_call_expr(ExprPtr callee) {
        auto loc = current().loc;
        expect(TokenType::LeftParen);

        CallExpr ce;
        ce.callee = std::move(callee);

        if (!check(TokenType::RightParen)) {
            // Detectar prefixo de formato float: Nf: (ex: 2f:, 4f:)
            // Tokens: IntLiteral("N") Identifier("f") Colon
            if (check(TokenType::IntLiteral) &&
                peek_token(1).is(TokenType::Identifier) &&
                peek_token(1).value == "f" &&
                peek_token(2).is(TokenType::Colon)) {
                ce.float_precision = std::stoi(current().value);
                advance_token(); // IntLiteral
                advance_token(); // Identifier("f")
                advance_token(); // Colon
                skip_newlines();
            }

            do {
                skip_newlines();
                // Check for named argument: name=value
                if (check(TokenType::Identifier) && peek_token(1).is(TokenType::Assign)) {
                    auto name = advance_token().value;
                    advance_token(); // =
                    auto val = parse_expression();
                    ce.named_args.push_back({std::move(name), std::move(val)});
                } else {
                    ce.args.push_back(parse_expression());
                }
                skip_newlines();
            } while (match(TokenType::Comma));
        }

        skip_newlines();
        expect(TokenType::RightParen);
        return make_expr(loc, std::move(ce));
    }

    // --- Index / slice expression ---
    ExprPtr parse_index_expr(ExprPtr object) {
        auto loc = current().loc;
        expect(TokenType::LeftBracket);
        auto first = parse_expression();

        // Slice: obj[start..end]
        if (first->is<RangeExpr>()) {
            expect(TokenType::RightBracket);
            auto& range = first->as<RangeExpr>();
            SliceExpr se;
            se.object = std::move(object);
            se.start = std::move(range.start);
            se.end = std::move(range.end);
            return make_expr(loc, std::move(se));
        }

        expect(TokenType::RightBracket);
        IndexExpr ie;
        ie.object = std::move(object);
        ie.index = std::move(first);
        return make_expr(loc, std::move(ie));
    }

    // --- Member access ---
    ExprPtr parse_member_expr(ExprPtr object) {
        auto loc = current().loc;
        expect(TokenType::Dot);
        auto member = expect(TokenType::Identifier).value;

        MemberExpr me;
        me.object = std::move(object);
        me.member = std::move(member);
        return make_expr(loc, std::move(me));
    }

    // --- Scope resolution ---
    ExprPtr parse_scope_expr(ExprPtr object) {
        auto loc = current().loc;
        expect(TokenType::DoubleColon);
        auto member = expect(TokenType::Identifier).value;

        ScopeResolutionExpr sre;
        sre.object = std::move(object);
        sre.member = std::move(member);
        return make_expr(loc, std::move(sre));
    }

    // ========================================================================
    // Prefix expressions
    // ========================================================================

    ExprPtr parse_prefix() {
        auto loc = current().loc;

        switch (current().type) {
            // Literals
            case TokenType::IntLiteral: {
                auto val = advance_token().value;
                return make_expr(loc, IntLiteralExpr{std::move(val)});
            }
            case TokenType::FloatLiteral: {
                auto val = advance_token().value;
                return make_expr(loc, FloatLiteralExpr{std::move(val)});
            }
            case TokenType::StringLiteral: {
                auto val = advance_token().value;
                // Check for interpolation: any {expr} inside
                if (!val.empty() && val.find('{') != std::string::npos) {
                    return parse_interpolated_string(val, loc);
                }
                return make_expr(loc, StringLiteralExpr{std::move(val), false});
            }
            case TokenType::RawStringLiteral: {
                auto val = advance_token().value;
                return make_expr(loc, StringLiteralExpr{std::move(val), true});
            }
            case TokenType::CharLiteral: {
                auto val = advance_token().value;
                return make_expr(loc, CharLiteralExpr{std::move(val)});
            }
            case TokenType::KwTrue: {
                advance_token();
                return make_expr(loc, BoolLiteralExpr{true});
            }
            case TokenType::KwFalse: {
                advance_token();
                return make_expr(loc, BoolLiteralExpr{false});
            }
            case TokenType::KwNil:
            case TokenType::KwNull: {
                advance_token();
                return make_expr(loc, NilLiteralExpr{});
            }

            // Identifiers and type casts: name or Type(args)
            case TokenType::Identifier:
            case TokenType::KwSelf:
            case TokenType::KwSelfType: {
                auto name = advance_token().value;
                return make_expr(loc, IdentifierExpr{std::move(name)});
            }

            // Unary operators
            case TokenType::Minus: {
                advance_token();
                auto operand = parse_expression(Precedence::Unary);
                return make_expr(loc, UnaryExpr{TokenType::Minus, std::move(operand)});
            }
            case TokenType::Tilde: {
                advance_token();
                auto operand = parse_expression(Precedence::Unary);
                return make_expr(loc, UnaryExpr{TokenType::Tilde, std::move(operand)});
            }
            case TokenType::KwNot: {
                advance_token();
                auto operand = parse_expression(Precedence::Not);
                return make_expr(loc, UnaryExpr{TokenType::KwNot, std::move(operand)});
            }

            // ref / ref mut
            case TokenType::KwRef: {
                advance_token();
                bool is_mut = false;
                if (check(TokenType::Identifier) && current().value == "mut") {
                    is_mut = true;
                    advance_token();
                }
                auto operand = parse_expression(Precedence::Unary);
                RefExpr re;
                re.operand = std::move(operand);
                re.is_mut = is_mut;
                return make_expr(loc, std::move(re));
            }

            // await
            case TokenType::KwAwait: {
                advance_token();
                auto operand = parse_expression(Precedence::Unary);
                AwaitExpr ae;
                ae.operand = std::move(operand);
                return make_expr(loc, std::move(ae));
            }

            // Grouping / tuple: (expr) or (a, b, c)
            case TokenType::LeftParen: {
                return parse_paren_expr();
            }

            // List / comprehension: [1, 2, 3] or [x for x in ...]
            case TokenType::LeftBracket: {
                return parse_list_expr();
            }

            // Lambda: |params| expr or |params|: block
            case TokenType::Pipe: {
                return parse_lambda_expr();
            }

            // If expression: if cond: a else: b
            case TokenType::KwIf: {
                return parse_if_expr();
            }

            // Match expression
            case TokenType::KwMatch: {
                return parse_match_expression();
            }

            default:
                error(std::format("unexpected token: '{}'", token_type_str(current().type)));
        }
    }

    // --- Parenthesized expression or tuple ---
    ExprPtr parse_paren_expr() {
        auto loc = current().loc;
        expect(TokenType::LeftParen);

        if (match(TokenType::RightParen)) {
            return make_expr(loc, TupleExpr{{}});
        }

        skip_newlines();
        auto first = parse_expression();

        // Tuple
        if (match(TokenType::Comma)) {
            TupleExpr te;
            te.elements.push_back(std::move(first));
            if (!check(TokenType::RightParen)) {
                do {
                    skip_newlines();
                    te.elements.push_back(parse_expression());
                    skip_newlines();
                } while (match(TokenType::Comma));
            }
            skip_newlines();
            expect(TokenType::RightParen);
            return make_expr(loc, std::move(te));
        }

        skip_newlines();
        expect(TokenType::RightParen);
        return first; // just grouping
    }

    // --- List or comprehension ---
    ExprPtr parse_list_expr() {
        auto loc = current().loc;
        expect(TokenType::LeftBracket);

        if (match(TokenType::RightBracket)) {
            return make_expr(loc, ListExpr{{}});
        }

        skip_newlines();
        auto first = parse_expression();

        // Comprehension: [expr for var in iterable if cond]
        if (check(TokenType::KwFor)) {
            advance_token();
            auto var = expect(TokenType::Identifier).value;
            expect(TokenType::KwIn);
            auto iterable = parse_expression();

            ExprPtr cond = nullptr;
            if (match(TokenType::KwIf)) {
                cond = parse_expression();
            }

            expect(TokenType::RightBracket);

            ComprehensionExpr ce;
            ce.element = std::move(first);
            ce.var_name = std::move(var);
            ce.iterable = std::move(iterable);
            ce.condition = std::move(cond);
            return make_expr(loc, std::move(ce));
        }

        // Regular list
        ListExpr le;
        le.elements.push_back(std::move(first));
        while (match(TokenType::Comma)) {
            skip_newlines();
            if (check(TokenType::RightBracket)) break;
            le.elements.push_back(parse_expression());
            skip_newlines();
        }

        skip_newlines();
        expect(TokenType::RightBracket);
        return make_expr(loc, std::move(le));
    }

    // --- Lambda ---
    ExprPtr parse_lambda_expr() {
        auto loc = current().loc;
        expect(TokenType::Pipe);

        LambdaExpr le;

        if (!check(TokenType::Pipe)) {
            do {
                LambdaParam lp;
                lp.name = expect(TokenType::Identifier).value;
                if (match(TokenType::Colon)) {
                    lp.type_expr = parse_type_expression();
                }
                le.params.push_back(std::move(lp));
            } while (match(TokenType::Comma));
        }

        expect(TokenType::Pipe);

        if (match(TokenType::Arrow)) {
            le.return_type = parse_type_expression();
        }

        // Multi-line lambda with colon + block
        if (match(TokenType::Colon)) {
            if (check(TokenType::Newline) || check(TokenType::Indent)) {
                le.body = parse_block();
            } else {
                le.body = parse_expression();
            }
        } else {
            // Single expression lambda: |x| x * 2
            le.body = parse_expression();
        }

        return make_expr(loc, std::move(le));
    }

    // --- If expression ---
    ExprPtr parse_if_expr() {
        auto loc = current().loc;
        expect(TokenType::KwIf);
        auto cond = parse_expression();
        expect(TokenType::Colon);
        auto then_expr = parse_expression();
        expect(TokenType::KwElse);
        expect(TokenType::Colon);
        auto else_expr = parse_expression();

        IfExpr ie;
        ie.condition = std::move(cond);
        ie.then_expr = std::move(then_expr);
        ie.else_expr = std::move(else_expr);
        return make_expr(loc, std::move(ie));
    }

    // --- Match expression ---
    ExprPtr parse_match_expression() {
        auto loc = current().loc;
        expect(TokenType::KwMatch);
        auto subject = parse_expression();
        expect(TokenType::Colon);

        MatchExpr me;
        me.subject = std::move(subject);

        skip_newlines();
        expect(TokenType::Indent);
        skip_newlines();

        while (!check(TokenType::Dedent) && !at_end()) {
            MatchExprArm arm;
            arm.pattern = parse_expression();

            if (match(TokenType::KwIf)) {
                arm.guard = parse_expression();
            }

            expect(TokenType::Colon);
            arm.body = parse_expression();
            expect_line_end();

            me.arms.push_back(std::move(arm));
            skip_newlines();
        }

        if (check(TokenType::Dedent)) advance_token();
        return make_expr(loc, std::move(me));
    }

    // --- Interpolated string ---
    ExprPtr parse_interpolated_string(const std::string& raw, SourceLocation loc) {
        InterpolatedStringExpr interp;
        std::string current_text;
        size_t i = 0;

        while (i < raw.size()) {
            if (raw[i] == '{') {
                // Check for escaped {{ → literal {
                if (i + 1 < raw.size() && raw[i + 1] == '{') {
                    current_text += '{';
                    i += 2;
                    continue;
                }

                // Emit accumulated text
                if (!current_text.empty()) {
                    interp.parts.push_back(make_expr(loc, StringLiteralExpr{current_text, false}));
                    current_text.clear();
                }

                // Find matching }
                i++; // skip {
                std::string expr_str;
                int depth = 1;
                while (i < raw.size() && depth > 0) {
                    if (raw[i] == '{') depth++;
                    else if (raw[i] == '}') {
                        depth--;
                        if (depth == 0) break;
                    }
                    expr_str += raw[i];
                    i++;
                }
                if (i < raw.size()) i++; // skip }

                // Parse the expression inside {}
                // We lex and parse the inner expression
                atomic::Lexer inner_lexer(expr_str + "\n", "<interpolation>");
                auto inner_tokens = inner_lexer.tokenize();
                atomic::Parser inner_parser(std::move(inner_tokens), "<interpolation>");
                auto inner_program = inner_parser.parse();

                if (!inner_program.statements.empty()) {
                    auto& stmt = inner_program.statements[0];
                    if (stmt->is<ExprStmt>()) {
                        interp.parts.push_back(std::move(stmt->as<ExprStmt>().expr));
                    } else if (stmt->is<VarDeclStmt>()) {
                        // Simple identifier reference parsed as VarDecl
                        auto& decl = stmt->as<VarDeclStmt>();
                        interp.parts.push_back(make_expr(loc, IdentifierExpr{decl.name}));
                    }
                }
            } else if (raw[i] == '}' && i + 1 < raw.size() && raw[i + 1] == '}') {
                // Escaped }} → literal }
                current_text += '}';
                i += 2;
            } else {
                current_text += raw[i];
                i++;
            }
        }

        // Remaining text
        if (!current_text.empty()) {
            interp.parts.push_back(make_expr(loc, StringLiteralExpr{current_text, false}));
        }

        // If only one part and it's a string, just return it
        if (interp.parts.size() == 1 && interp.parts[0]->is<StringLiteralExpr>()) {
            return std::move(interp.parts[0]);
        }

        return make_expr(loc, std::move(interp));
    }
};

} // namespace atomic
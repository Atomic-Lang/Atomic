// ast.hpp
// AST node definitions for the Atomic programming language

#pragma once

#include "lexer.hpp"
#include <memory>
#include <optional>
#include <variant>

namespace atomic {

// ============================================================================
// Forward Declarations
// ============================================================================

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using ExprList = std::vector<ExprPtr>;
using StmtList = std::vector<StmtPtr>;

// ============================================================================
// AST Node Types - Expressions
// ============================================================================

struct IntLiteralExpr {
    std::string value;
};

struct FloatLiteralExpr {
    std::string value;
};

struct StringLiteralExpr {
    std::string value;
    bool raw = false;
};

// "hello {name}, you are {age} years old"
// parts: ["hello ", name, ", you are ", age, " years old"]
struct InterpolatedStringExpr {
    std::vector<ExprPtr> parts; // alternating string literals and expressions
};

struct CharLiteralExpr {
    std::string value;
};

struct BoolLiteralExpr {
    bool value;
};

struct NilLiteralExpr {};

struct IdentifierExpr {
    std::string name;
};

struct UnaryExpr {
    TokenType op;
    ExprPtr operand;
};

struct BinaryExpr {
    TokenType op;
    ExprPtr left;
    ExprPtr right;
};

struct CallExpr {
    ExprPtr callee;
    std::vector<ExprPtr> args;
    std::vector<std::pair<std::string, ExprPtr>> named_args;
    int float_precision = -1; // -1 = sem formato, >= 0 = casas decimais (ex: 2f:)
};

struct IndexExpr {
    ExprPtr object;
    ExprPtr index;
};

struct SliceExpr {
    ExprPtr object;
    ExprPtr start;   // may be nullptr
    ExprPtr end;     // may be nullptr
};

struct MemberExpr {
    ExprPtr object;
    std::string member;
};

struct ScopeResolutionExpr {
    ExprPtr object;
    std::string member;
};

struct AssignExpr {
    ExprPtr target;
    TokenType op;    // Assign, PlusAssign, etc.
    ExprPtr value;
};

struct IfExpr {
    ExprPtr condition;
    ExprPtr then_expr;
    ExprPtr else_expr;
};

struct MatchExprArm {
    ExprPtr pattern;
    ExprPtr guard;   // may be nullptr
    ExprPtr body;
};

struct MatchExpr {
    ExprPtr subject;
    std::vector<MatchExprArm> arms;
};

struct LambdaParam {
    std::string name;
    ExprPtr type_expr; // may be nullptr
};

struct LambdaExpr {
    std::vector<LambdaParam> params;
    ExprPtr return_type; // may be nullptr
    std::variant<ExprPtr, StmtList> body;
};

struct ListExpr {
    ExprList elements;
};

struct MapEntry {
    ExprPtr key;
    ExprPtr value;
};

struct MapExpr {
    std::vector<MapEntry> entries;
};

struct SetExpr {
    ExprList elements;
};

struct TupleExpr {
    ExprList elements;
};

struct RangeExpr {
    ExprPtr start;
    ExprPtr end;
    bool inclusive = false;
};

struct ComprehensionExpr {
    ExprPtr element;
    std::string var_name;
    ExprPtr iterable;
    ExprPtr condition; // may be nullptr
};

struct PipeExpr {
    ExprPtr left;
    ExprPtr right;
};

struct RefExpr {
    ExprPtr operand;
    bool is_mut = false;
};

struct CastExpr {
    ExprPtr target_type; // type name as identifier
    ExprList args;
};

struct AwaitExpr {
    ExprPtr operand;
};

struct QuestionExpr {
    ExprPtr operand;
};

struct TypeAnnotation {
    std::string name;
    std::vector<ExprPtr> generics; // may be empty
};

// ============================================================================
// Expression Variant
// ============================================================================

struct Expr {
    SourceLocation loc;

    std::variant<
        IntLiteralExpr,
        FloatLiteralExpr,
        StringLiteralExpr,
        InterpolatedStringExpr,
        CharLiteralExpr,
        BoolLiteralExpr,
        NilLiteralExpr,
        IdentifierExpr,
        UnaryExpr,
        BinaryExpr,
        CallExpr,
        IndexExpr,
        SliceExpr,
        MemberExpr,
        ScopeResolutionExpr,
        AssignExpr,
        IfExpr,
        MatchExpr,
        LambdaExpr,
        ListExpr,
        MapExpr,
        SetExpr,
        TupleExpr,
        RangeExpr,
        ComprehensionExpr,
        PipeExpr,
        RefExpr,
        CastExpr,
        AwaitExpr,
        QuestionExpr,
        TypeAnnotation
    > data;

    template<typename T>
    bool is() const { return std::holds_alternative<T>(data); }

    template<typename T>
    T& as() { return std::get<T>(data); }

    template<typename T>
    const T& as() const { return std::get<T>(data); }
};

// ============================================================================
// AST Node Types - Statements
// ============================================================================

struct ExprStmt {
    ExprPtr expr;
};

struct VarDeclStmt {
    std::string name;
    ExprPtr type_expr;   // may be nullptr (inferred)
    ExprPtr initializer;
    bool is_const = false;
};

struct FnParam {
    std::string name;
    ExprPtr type_expr;
    ExprPtr default_value; // may be nullptr
    bool is_mut_self = false;
    bool is_self = false;
};

struct FnDeclStmt {
    std::string name;
    std::vector<FnParam> params;
    ExprPtr return_type;    // may be nullptr (void)
    StmtList body;
    bool is_pub = false;
    bool is_async = false;
    std::vector<std::string> generic_params;
    std::vector<ExprPtr> generic_bounds;
};

struct ReturnStmt {
    ExprPtr value; // may be nullptr
};

struct IfBranch {
    ExprPtr condition; // nullptr for else
    StmtList body;
};

struct IfStmt {
    std::vector<IfBranch> branches; // if, elif..., else
};

struct ForStmt {
    std::string var_name;
    std::string second_var; // for (k, v) style — empty if single
    ExprPtr iterable;
    StmtList body;
};

struct WhileStmt {
    ExprPtr condition;
    StmtList body;
};

struct LoopStmt {
    StmtList body;
};

struct BreakStmt {
    ExprPtr value; // may be nullptr
};

struct ContinueStmt {};

struct MatchArm {
    ExprPtr pattern;
    ExprPtr guard;   // may be nullptr (if condition)
    StmtList body;
};

struct MatchStmt {
    ExprPtr subject;
    std::vector<MatchArm> arms;
};

struct StructField {
    std::string name;
    ExprPtr type_expr;
    bool is_pub = false;
};

struct StructDeclStmt {
    std::string name;
    std::vector<StructField> fields;
    bool is_pub = false;
    std::vector<std::string> generic_params;
};

struct EnumVariant {
    std::string name;
    std::vector<ExprPtr> fields; // empty for unit variants
};

struct EnumDeclStmt {
    std::string name;
    std::vector<EnumVariant> variants;
    bool is_pub = false;
    std::vector<std::string> generic_params;
};

struct ImplBlock {
    std::string target_type;
    std::string trait_name; // empty if not trait impl
    StmtList methods;
    std::vector<std::string> generic_params;
};

struct TraitMethod {
    std::string name;
    std::vector<FnParam> params;
    ExprPtr return_type;
    StmtList default_body; // empty if no default
    bool is_async = false;
};

struct TraitDeclStmt {
    std::string name;
    std::vector<TraitMethod> methods;
    bool is_pub = false;
    std::vector<std::string> generic_params;
    std::vector<std::string> super_traits;
};

struct ImportStmt {
    std::string module_path;
    std::vector<std::pair<std::string, std::string>> items; // (name, alias) — empty for whole module
    bool is_wildcard = false;
    bool is_extern = false;
};

struct ModDeclStmt {
    std::string name;
};

struct TryStmt {
    StmtList body;
    struct CatchClause {
        std::string error_type; // empty for catch-all
        std::string var_name;   // empty if not bound
        StmtList body;
    };
    std::vector<CatchClause> catches;
};

struct DeferStmt {
    ExprPtr expr;
};

struct DropStmt {
    ExprPtr expr;
};

struct UnsafeBlock {
    StmtList body;
};

struct TypeAliasStmt {
    std::string name;
    ExprPtr type_expr;
    bool is_pub = false;
};

struct DeriveStmt {
    std::vector<std::string> traits;
    StmtPtr target; // struct or enum that follows
};

// ============================================================================
// Statement Variant
// ============================================================================

struct Stmt {
    SourceLocation loc;

    std::variant<
        ExprStmt,
        VarDeclStmt,
        FnDeclStmt,
        ReturnStmt,
        IfStmt,
        ForStmt,
        WhileStmt,
        LoopStmt,
        BreakStmt,
        ContinueStmt,
        MatchStmt,
        StructDeclStmt,
        EnumDeclStmt,
        ImplBlock,
        TraitDeclStmt,
        ImportStmt,
        ModDeclStmt,
        TryStmt,
        DeferStmt,
        DropStmt,
        UnsafeBlock,
        TypeAliasStmt,
        DeriveStmt
    > data;

    template<typename T>
    bool is() const { return std::holds_alternative<T>(data); }

    template<typename T>
    T& as() { return std::get<T>(data); }

    template<typename T>
    const T& as() const { return std::get<T>(data); }
};

// ============================================================================
// AST Program (root node)
// ============================================================================

struct Program {
    StmtList statements;
    std::string filename;
};

} // namespace atomic
// lexer.hpp
// Lexer (tokenizer) for the Atomic programming language (.atom files)

#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <stdexcept>
#include <format>

namespace atomic {

// ============================================================================
// Token Types
// ============================================================================

enum class TokenType : uint8_t {
    // Literals
    IntLiteral,         // 42, 0xFF, 0b1010, 0o77
    FloatLiteral,       // 3.14, 1.0e10
    StringLiteral,      // "hello", """multi"""
    RawStringLiteral,   // r"no\escapes"
    CharLiteral,        // 'A'
    
    // Identifiers
    Identifier,         // variable/function names

    // Keywords - Control Flow
    KwFn,               // fn
    KwReturn,           // return
    KwIf,               // if
    KwElse,             // else
    KwElif,             // elif
    KwFor,              // for
    KwWhile,            // while
    KwLoop,             // loop
    KwBreak,            // break
    KwContinue,         // continue
    KwMatch,            // match
    KwIn,               // in

    // Keywords - Data
    KwStruct,           // struct
    KwEnum,             // enum
    KwImpl,             // impl
    KwTrait,            // trait
    KwType,             // type
    KwSelf,             // self
    KwSelfType,         // Self

    // Keywords - Modules
    KwImport,           // import
    KwFrom,             // from
    KwAs,               // as
    KwPub,              // pub
    KwMod,              // mod

    // Keywords - Memory & Safety
    KwConst,            // const
    KwRef,              // ref
    KwMove,             // move
    KwCopy,             // copy
    KwDrop,             // drop
    KwUnsafe,           // unsafe
    KwDefer,            // defer

    // Keywords - Concurrency
    KwAsync,            // async
    KwAwait,            // await

    // Keywords - FFI
    KwExtern,           // extern

    // Keywords - Error Handling
    KwTry,              // try
    KwCatch,            // catch
    KwPanic,            // panic

    // Keywords - Logical
    KwAnd,              // and
    KwOr,               // or
    KwNot,              // not
    KwIs,               // is

    // Keywords - Values
    KwTrue,             // true
    KwFalse,            // false
    KwNil,              // nil
    KwNull,             // null

    // Arithmetic Operators
    Plus,               // +
    Minus,              // -
    Star,               // *
    Slash,              // /
    Percent,            // %
    DoubleStar,         // **

    // Bitwise Operators
    Ampersand,          // &
    Pipe,               // |
    Caret,              // ^
    Tilde,              // ~
    ShiftLeft,          // <<
    ShiftRight,         // >>

    // Comparison Operators
    Equal,              // ==
    NotEqual,           // !=
    Less,               // <
    Greater,            // >
    LessEqual,          // <=
    GreaterEqual,       // >=

    // Assignment Operators
    Assign,             // =
    PlusAssign,         // +=
    MinusAssign,        // -=
    StarAssign,         // *=
    SlashAssign,        // /=
    PercentAssign,      // %=
    DoubleStarAssign,   // **=
    AmpersandAssign,    // &=
    PipeAssign,         // |=
    CaretAssign,        // ^=
    ShiftLeftAssign,    // <<=
    ShiftRightAssign,   // >>=

    // Delimiters
    LeftParen,          // (
    RightParen,         // )
    LeftBracket,        // [
    RightBracket,       // ]
    LeftBrace,          // {
    RightBrace,         // }
    Colon,              // :
    Comma,              // ,
    Dot,                // .
    DoubleColon,        // ::
    Hash,               // #

    // Special Operators
    Range,              // ..
    RangeInclusive,     // ..=
    Arrow,              // ->
    PipeArrow,          // |>
    Question,           // ?

    // Indentation
    Indent,             // increase in indentation level
    Dedent,             // decrease in indentation level
    Newline,            // end of logical line

    // End of File
    Eof,
};

// ============================================================================
// Token
// ============================================================================

struct SourceLocation {
    uint32_t line   = 1;
    uint32_t column = 1;
    uint32_t offset = 0;
};

struct Token {
    TokenType      type     = TokenType::Eof;
    std::string    value    = "";
    SourceLocation loc      = {};

    bool is(TokenType t) const { return type == t; }
    bool is_keyword() const { return type >= TokenType::KwFn && type <= TokenType::KwNull; }
    bool is_operator() const { return type >= TokenType::Plus && type <= TokenType::Question; }
    bool is_literal() const { return type >= TokenType::IntLiteral && type <= TokenType::CharLiteral; }
};

// ============================================================================
// Token type to string (for debugging)
// ============================================================================

inline const char* token_type_str(TokenType t) {
    switch (t) {
        case TokenType::IntLiteral:         return "IntLiteral";
        case TokenType::FloatLiteral:       return "FloatLiteral";
        case TokenType::StringLiteral:      return "StringLiteral";
        case TokenType::RawStringLiteral:   return "RawStringLiteral";
        case TokenType::CharLiteral:        return "CharLiteral";
        case TokenType::Identifier:         return "Identifier";
        case TokenType::KwFn:               return "fn";
        case TokenType::KwReturn:           return "return";
        case TokenType::KwIf:               return "if";
        case TokenType::KwElse:             return "else";
        case TokenType::KwElif:             return "elif";
        case TokenType::KwFor:              return "for";
        case TokenType::KwWhile:            return "while";
        case TokenType::KwLoop:             return "loop";
        case TokenType::KwBreak:            return "break";
        case TokenType::KwContinue:         return "continue";
        case TokenType::KwMatch:            return "match";
        case TokenType::KwIn:               return "in";
        case TokenType::KwStruct:           return "struct";
        case TokenType::KwEnum:             return "enum";
        case TokenType::KwImpl:             return "impl";
        case TokenType::KwTrait:            return "trait";
        case TokenType::KwType:             return "type";
        case TokenType::KwSelf:             return "self";
        case TokenType::KwSelfType:         return "Self";
        case TokenType::KwImport:           return "import";
        case TokenType::KwFrom:             return "from";
        case TokenType::KwAs:               return "as";
        case TokenType::KwPub:              return "pub";
        case TokenType::KwMod:              return "mod";
        case TokenType::KwConst:            return "const";
        case TokenType::KwRef:              return "ref";
        case TokenType::KwMove:             return "move";
        case TokenType::KwCopy:             return "copy";
        case TokenType::KwDrop:             return "drop";
        case TokenType::KwUnsafe:           return "unsafe";
        case TokenType::KwDefer:            return "defer";
        case TokenType::KwAsync:            return "async";
        case TokenType::KwAwait:            return "await";
        case TokenType::KwExtern:           return "extern";
        case TokenType::KwTry:              return "try";
        case TokenType::KwCatch:            return "catch";
        case TokenType::KwPanic:            return "panic";
        case TokenType::KwAnd:              return "and";
        case TokenType::KwOr:               return "or";
        case TokenType::KwNot:              return "not";
        case TokenType::KwIs:               return "is";
        case TokenType::KwTrue:             return "true";
        case TokenType::KwFalse:            return "false";
        case TokenType::KwNil:              return "nil";
        case TokenType::KwNull:             return "null";
        case TokenType::Plus:               return "+";
        case TokenType::Minus:              return "-";
        case TokenType::Star:               return "*";
        case TokenType::Slash:              return "/";
        case TokenType::Percent:            return "%";
        case TokenType::DoubleStar:         return "**";
        case TokenType::Ampersand:          return "&";
        case TokenType::Pipe:               return "|";
        case TokenType::Caret:              return "^";
        case TokenType::Tilde:              return "~";
        case TokenType::ShiftLeft:          return "<<";
        case TokenType::ShiftRight:         return ">>";
        case TokenType::Equal:              return "==";
        case TokenType::NotEqual:           return "!=";
        case TokenType::Less:               return "<";
        case TokenType::Greater:            return ">";
        case TokenType::LessEqual:          return "<=";
        case TokenType::GreaterEqual:       return ">=";
        case TokenType::Assign:             return "=";
        case TokenType::PlusAssign:         return "+=";
        case TokenType::MinusAssign:        return "-=";
        case TokenType::StarAssign:         return "*=";
        case TokenType::SlashAssign:        return "/=";
        case TokenType::PercentAssign:      return "%=";
        case TokenType::DoubleStarAssign:   return "**=";
        case TokenType::AmpersandAssign:    return "&=";
        case TokenType::PipeAssign:         return "|=";
        case TokenType::CaretAssign:        return "^=";
        case TokenType::ShiftLeftAssign:    return "<<=";
        case TokenType::ShiftRightAssign:   return ">>=";
        case TokenType::LeftParen:          return "(";
        case TokenType::RightParen:         return ")";
        case TokenType::LeftBracket:        return "[";
        case TokenType::RightBracket:       return "]";
        case TokenType::LeftBrace:          return "{";
        case TokenType::RightBrace:         return "}";
        case TokenType::Colon:              return ":";
        case TokenType::Comma:              return ",";
        case TokenType::Dot:                return ".";
        case TokenType::DoubleColon:        return "::";
        case TokenType::Hash:               return "#";
        case TokenType::Range:              return "..";
        case TokenType::RangeInclusive:     return "..=";
        case TokenType::Arrow:              return "->";
        case TokenType::PipeArrow:          return "|>";
        case TokenType::Question:           return "?";
        case TokenType::Indent:             return "INDENT";
        case TokenType::Dedent:             return "DEDENT";
        case TokenType::Newline:            return "NEWLINE";
        case TokenType::Eof:               return "EOF";
    }
    return "UNKNOWN";
}

// ============================================================================
// Lexer Error
// ============================================================================

struct LexerError : std::runtime_error {
    SourceLocation loc;

    LexerError(const std::string& msg, SourceLocation loc)
        : std::runtime_error(std::format("{}:{}: {}", loc.line, loc.column, msg))
        , loc(loc) {}
};

// ============================================================================
// Lexer
// ============================================================================

class Lexer {
public:
    explicit Lexer(std::string source, std::string filename = "<stdin>")
        : m_source(std::move(source))
        , m_filename(std::move(filename)) {}

    std::vector<Token> tokenize() {
        m_tokens.clear();
        m_pos = 0;
        m_loc = {1, 1, 0};
        m_indent_stack.clear();
        m_indent_stack.push_back(0);
        m_paren_depth = 0;

        while (!at_end()) {
            scan_line();
        }

        // Emit remaining dedents at EOF
        while (m_indent_stack.size() > 1) {
            m_indent_stack.pop_back();
            emit(TokenType::Dedent, "");
        }

        emit(TokenType::Eof, "");
        return std::move(m_tokens);
    }

    const std::string& filename() const { return m_filename; }

private:
    std::string         m_source;
    std::string         m_filename;
    std::vector<Token>  m_tokens;
    uint32_t            m_pos = 0;
    SourceLocation      m_loc = {1, 1, 0};
    std::vector<int>    m_indent_stack = {0};
    int                 m_paren_depth = 0;

    // --- Keywords map ---
    static const std::unordered_map<std::string_view, TokenType>& keywords() {
        static const std::unordered_map<std::string_view, TokenType> kw = {
            {"fn",       TokenType::KwFn},
            {"return",   TokenType::KwReturn},
            {"if",       TokenType::KwIf},
            {"else",     TokenType::KwElse},
            {"elif",     TokenType::KwElif},
            {"for",      TokenType::KwFor},
            {"while",    TokenType::KwWhile},
            {"loop",     TokenType::KwLoop},
            {"break",    TokenType::KwBreak},
            {"continue", TokenType::KwContinue},
            {"match",    TokenType::KwMatch},
            {"in",       TokenType::KwIn},
            {"struct",   TokenType::KwStruct},
            {"enum",     TokenType::KwEnum},
            {"impl",     TokenType::KwImpl},
            {"trait",    TokenType::KwTrait},
            {"type",     TokenType::KwType},
            {"self",     TokenType::KwSelf},
            {"Self",     TokenType::KwSelfType},
            {"import",   TokenType::KwImport},
            {"from",     TokenType::KwFrom},
            {"as",       TokenType::KwAs},
            {"pub",      TokenType::KwPub},
            {"mod",      TokenType::KwMod},
            {"const",    TokenType::KwConst},
            {"ref",      TokenType::KwRef},
            {"move",     TokenType::KwMove},
            {"copy",     TokenType::KwCopy},
            {"drop",     TokenType::KwDrop},
            {"unsafe",   TokenType::KwUnsafe},
            {"defer",    TokenType::KwDefer},
            {"async",    TokenType::KwAsync},
            {"await",    TokenType::KwAwait},
            {"extern",   TokenType::KwExtern},
            {"try",      TokenType::KwTry},
            {"catch",    TokenType::KwCatch},
            {"panic",    TokenType::KwPanic},
            {"and",      TokenType::KwAnd},
            {"or",       TokenType::KwOr},
            {"not",      TokenType::KwNot},
            {"is",       TokenType::KwIs},
            {"true",     TokenType::KwTrue},
            {"false",    TokenType::KwFalse},
            {"nil",      TokenType::KwNil},
            {"null",     TokenType::KwNull},
        };
        return kw;
    }

    // --- Character helpers ---
    bool at_end() const { return m_pos >= m_source.size(); }

    char peek() const {
        if (at_end()) return '\0';
        return m_source[m_pos];
    }

    char peek_next() const {
        if (m_pos + 1 >= m_source.size()) return '\0';
        return m_source[m_pos + 1];
    }

    char peek_at(uint32_t offset) const {
        uint32_t idx = m_pos + offset;
        if (idx >= m_source.size()) return '\0';
        return m_source[idx];
    }

    char advance() {
        char c = m_source[m_pos++];
        m_loc.offset = m_pos;
        if (c == '\n') {
            m_loc.line++;
            m_loc.column = 1;
        } else {
            m_loc.column++;
        }
        return c;
    }

    bool match_char(char expected) {
        if (at_end() || m_source[m_pos] != expected) return false;
        advance();
        return true;
    }

    void emit(TokenType type, std::string value) {
        m_tokens.push_back(Token{type, std::move(value), m_loc});
    }

    void emit_at(TokenType type, std::string value, SourceLocation loc) {
        m_tokens.push_back(Token{type, std::move(value), loc});
    }

    [[noreturn]] void error(const std::string& msg) {
        throw LexerError(msg, m_loc);
    }

    // --- Indentation handling ---
    int measure_indent() {
        int spaces = 0;
        uint32_t p = m_pos;
        while (p < m_source.size() && m_source[p] == ' ') {
            spaces++;
            p++;
        }
        // Check for tabs
        if (p < m_source.size() && m_source[p] == '\t') {
            auto saved = m_loc;
            m_loc.column += spaces;
            error("tabs are not allowed, use 4 spaces for indentation");
        }
        return spaces;
    }

    void handle_indentation(int indent) {
        int current = m_indent_stack.back();

        if (indent > current) {
            m_indent_stack.push_back(indent);
            emit(TokenType::Indent, "");
        } else {
            while (indent < m_indent_stack.back()) {
                m_indent_stack.pop_back();
                emit(TokenType::Dedent, "");
            }
            if (indent != m_indent_stack.back()) {
                error("inconsistent indentation");
            }
        }
    }

    // --- Line scanning ---
    void scan_line() {
        // Measure leading indentation
        int indent = measure_indent();

        // Skip blank lines
        if (peek_at(indent) == '\n' || peek_at(indent) == '\r' || (m_pos + indent >= m_source.size())) {
            // Consume the whitespace and newline
            for (int i = 0; i < indent; i++) advance();
            if (!at_end()) {
                if (peek() == '\r') advance();
                if (peek() == '\n') advance();
            }
            return;
        }

        // Skip comment-only lines
        if (peek_at(indent) == '#') {
            for (int i = 0; i < indent; i++) advance();
            skip_comment();
            if (!at_end()) {
                if (peek() == '\r') advance();
                if (peek() == '\n') advance();
            }
            return;
        }

        // Emit indentation changes (only outside parens/brackets/braces)
        if (m_paren_depth == 0) {
            handle_indentation(indent);
        }

        // Consume leading spaces
        for (int i = 0; i < indent; i++) advance();

        // Scan tokens until end of line
        scan_tokens_until_eol();
    }

    void scan_tokens_until_eol() {
        while (!at_end()) {
            char c = peek();

            // End of line
            if (c == '\n' || c == '\r') {
                if (c == '\r') advance();
                if (!at_end() && peek() == '\n') advance();
                if (m_paren_depth == 0) {
                    emit(TokenType::Newline, "\\n");
                }
                return;
            }

            // Skip whitespace within line
            if (c == ' ') {
                advance();
                continue;
            }

            // Comments
            if (c == '#') {
                skip_comment();
                continue;
            }

            scan_token();
        }

        // EOF reached — emit newline if we had tokens
        if (m_paren_depth == 0 && !m_tokens.empty() && m_tokens.back().type != TokenType::Newline) {
            emit(TokenType::Newline, "\\n");
        }
    }

    void skip_comment() {
        // Check for multi-line comment ##
        if (peek() == '#' && peek_next() == '#') {
            advance(); // #
            advance(); // #
            // Skip until closing ##
            while (!at_end()) {
                if (peek() == '#' && peek_next() == '#') {
                    advance(); // #
                    advance(); // #
                    return;
                }
                advance();
            }
            error("unterminated multi-line comment");
        }

        // Single-line comment: skip until end of line
        while (!at_end() && peek() != '\n' && peek() != '\r') {
            advance();
        }
    }

    // --- Token scanning ---
    void scan_token() {
        auto start_loc = m_loc;
        char c = peek();

        // Numbers
        if (is_digit(c) || (c == '.' && is_digit(peek_next()))) {
            scan_number();
            return;
        }

        // Identifiers and keywords
        if (is_ident_start(c)) {
            scan_identifier();
            return;
        }

        // Strings
        if (c == '"') {
            scan_string();
            return;
        }

        // Raw strings
        if (c == 'r' && peek_next() == '"') {
            scan_raw_string();
            return;
        }

        // Char literals
        if (c == '\'') {
            scan_char();
            return;
        }

        // Operators and delimiters
        scan_operator();
    }

    // --- Number scanning ---
    void scan_number() {
        auto start_loc = m_loc;
        uint32_t start = m_pos;
        bool is_float = false;

        // Hex, binary, octal prefixes
        if (peek() == '0' && m_pos + 1 < m_source.size()) {
            char next = m_source[m_pos + 1];
            if (next == 'x' || next == 'X') {
                advance(); advance(); // 0x
                if (!is_hex_digit(peek())) error("expected hex digit after '0x'");
                while (!at_end() && (is_hex_digit(peek()) || peek() == '_')) advance();
                emit_at(TokenType::IntLiteral, m_source.substr(start, m_pos - start), start_loc);
                return;
            }
            if (next == 'b' || next == 'B') {
                advance(); advance(); // 0b
                if (peek() != '0' && peek() != '1') error("expected binary digit after '0b'");
                while (!at_end() && (peek() == '0' || peek() == '1' || peek() == '_')) advance();
                emit_at(TokenType::IntLiteral, m_source.substr(start, m_pos - start), start_loc);
                return;
            }
            if (next == 'o' || next == 'O') {
                advance(); advance(); // 0o
                if (!is_octal_digit(peek())) error("expected octal digit after '0o'");
                while (!at_end() && (is_octal_digit(peek()) || peek() == '_')) advance();
                emit_at(TokenType::IntLiteral, m_source.substr(start, m_pos - start), start_loc);
                return;
            }
        }

        // Decimal digits
        while (!at_end() && (is_digit(peek()) || peek() == '_')) advance();

        // Fractional part
        if (peek() == '.' && peek_next() != '.') {  // avoid matching ..
            is_float = true;
            advance(); // .
            while (!at_end() && (is_digit(peek()) || peek() == '_')) advance();
        }

        // Exponent
        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            advance(); // e/E
            if (peek() == '+' || peek() == '-') advance();
            if (!is_digit(peek())) error("expected digit in exponent");
            while (!at_end() && (is_digit(peek()) || peek() == '_')) advance();
        }

        auto type = is_float ? TokenType::FloatLiteral : TokenType::IntLiteral;
        emit_at(type, m_source.substr(start, m_pos - start), start_loc);
    }

    // --- Identifier / keyword scanning ---
    void scan_identifier() {
        auto start_loc = m_loc;
        uint32_t start = m_pos;

        while (!at_end() && is_ident_char(peek())) advance();

        std::string word = m_source.substr(start, m_pos - start);

        // Check for raw string: r"..."
        if (word == "r" && !at_end() && peek() == '"') {
            scan_raw_string();
            return;
        }

        // Check keywords
        auto it = keywords().find(word);
        if (it != keywords().end()) {
            emit_at(it->second, std::move(word), start_loc);
        } else {
            emit_at(TokenType::Identifier, std::move(word), start_loc);
        }
    }

    // --- String scanning ---
    void scan_string() {
        auto start_loc = m_loc;
        advance(); // opening "

        // Check for triple-quoted string
        if (peek() == '"' && peek_next() == '"') {
            advance(); // second "
            advance(); // third "
            scan_triple_string(start_loc);
            return;
        }

        std::string value;
        while (!at_end() && peek() != '"') {
            if (peek() == '\n' || peek() == '\r') {
                error("unterminated string literal");
            }
            if (peek() == '\\') {
                value += scan_escape_sequence();
            } else {
                value += advance();
            }
        }

        if (at_end()) error("unterminated string literal");
        advance(); // closing "

        emit_at(TokenType::StringLiteral, std::move(value), start_loc);
    }

    void scan_triple_string(SourceLocation start_loc) {
        std::string value;
        while (!at_end()) {
            if (peek() == '"' && peek_next() == '"' && peek_at(2) == '"') {
                advance(); advance(); advance(); // closing """
                // Triple-quoted strings are emitted as Raw to avoid interpolation
                emit_at(TokenType::RawStringLiteral, std::move(value), start_loc);
                return;
            }
            if (peek() == '\\') {
                value += scan_escape_sequence();
            } else {
                value += advance();
            }
        }
        error("unterminated triple-quoted string");
    }

    void scan_raw_string() {
        auto start_loc = m_loc;
        if (peek() == 'r') advance(); // r
        advance(); // opening "

        std::string value;
        while (!at_end() && peek() != '"') {
            if (peek() == '\n' || peek() == '\r') {
                error("unterminated raw string literal");
            }
            value += advance();
        }

        if (at_end()) error("unterminated raw string literal");
        advance(); // closing "

        emit_at(TokenType::RawStringLiteral, std::move(value), start_loc);
    }

    char scan_escape_sequence() {
        advance(); // backslash
        if (at_end()) error("unexpected end of file in escape sequence");

        char c = advance();
        switch (c) {
            case 'n':  return '\n';
            case 't':  return '\t';
            case 'r':  return '\r';
            case '\\': return '\\';
            case '"':  return '"';
            case '\'': return '\'';
            case '0':  return '\0';
            case 'x': {
                // \xHH
                if (!is_hex_digit(peek()) || !is_hex_digit(peek_next()))
                    error("expected two hex digits after \\x");
                char h = advance();
                char l = advance();
                return (char)((hex_val(h) << 4) | hex_val(l));
            }
            default:
                error(std::format("unknown escape sequence: \\{}", c));
        }
    }

    // --- Char literal scanning ---
    void scan_char() {
        auto start_loc = m_loc;
        advance(); // opening '

        if (at_end() || peek() == '\'') error("empty char literal");

        std::string value;
        if (peek() == '\\') {
            value += scan_escape_sequence();
        } else {
            value += advance();
        }

        if (at_end() || peek() != '\'') error("unterminated char literal");
        advance(); // closing '

        emit_at(TokenType::CharLiteral, std::move(value), start_loc);
    }

    // --- Operator / delimiter scanning ---
    void scan_operator() {
        auto start_loc = m_loc;
        char c = advance();

        switch (c) {
            // Single-char delimiters
            case '(':
                m_paren_depth++;
                emit_at(TokenType::LeftParen, "(", start_loc);
                return;
            case ')':
                m_paren_depth = std::max(0, m_paren_depth - 1);
                emit_at(TokenType::RightParen, ")", start_loc);
                return;
            case '[':
                m_paren_depth++;
                emit_at(TokenType::LeftBracket, "[", start_loc);
                return;
            case ']':
                m_paren_depth = std::max(0, m_paren_depth - 1);
                emit_at(TokenType::RightBracket, "]", start_loc);
                return;
            case '{':
                m_paren_depth++;
                emit_at(TokenType::LeftBrace, "{", start_loc);
                return;
            case '}':
                m_paren_depth = std::max(0, m_paren_depth - 1);
                emit_at(TokenType::RightBrace, "}", start_loc);
                return;
            case ',':
                emit_at(TokenType::Comma, ",", start_loc);
                return;
            case '~':
                emit_at(TokenType::Tilde, "~", start_loc);
                return;
            case '?':
                emit_at(TokenType::Question, "?", start_loc);
                return;
            case '#':
                emit_at(TokenType::Hash, "#", start_loc);
                return;

            // Colon: : or ::
            case ':':
                if (match_char(':')) {
                    emit_at(TokenType::DoubleColon, "::", start_loc);
                } else {
                    emit_at(TokenType::Colon, ":", start_loc);
                }
                return;

            // Dot: . or .. or ..=
            case '.':
                if (match_char('.')) {
                    if (match_char('=')) {
                        emit_at(TokenType::RangeInclusive, "..=", start_loc);
                    } else {
                        emit_at(TokenType::Range, "..", start_loc);
                    }
                } else {
                    emit_at(TokenType::Dot, ".", start_loc);
                }
                return;

            // Plus: + or +=
            case '+':
                if (match_char('=')) {
                    emit_at(TokenType::PlusAssign, "+=", start_loc);
                } else {
                    emit_at(TokenType::Plus, "+", start_loc);
                }
                return;

            // Minus: - or -= or ->
            case '-':
                if (match_char('>')) {
                    emit_at(TokenType::Arrow, "->", start_loc);
                } else if (match_char('=')) {
                    emit_at(TokenType::MinusAssign, "-=", start_loc);
                } else {
                    emit_at(TokenType::Minus, "-", start_loc);
                }
                return;

            // Star: * or ** or *= or **=
            case '*':
                if (match_char('*')) {
                    if (match_char('=')) {
                        emit_at(TokenType::DoubleStarAssign, "**=", start_loc);
                    } else {
                        emit_at(TokenType::DoubleStar, "**", start_loc);
                    }
                } else if (match_char('=')) {
                    emit_at(TokenType::StarAssign, "*=", start_loc);
                } else {
                    emit_at(TokenType::Star, "*", start_loc);
                }
                return;

            // Slash: / or /=
            case '/':
                if (match_char('=')) {
                    emit_at(TokenType::SlashAssign, "/=", start_loc);
                } else {
                    emit_at(TokenType::Slash, "/", start_loc);
                }
                return;

            // Percent: % or %=
            case '%':
                if (match_char('=')) {
                    emit_at(TokenType::PercentAssign, "%=", start_loc);
                } else {
                    emit_at(TokenType::Percent, "%", start_loc);
                }
                return;

            // Ampersand: & or &=
            case '&':
                if (match_char('=')) {
                    emit_at(TokenType::AmpersandAssign, "&=", start_loc);
                } else {
                    emit_at(TokenType::Ampersand, "&", start_loc);
                }
                return;

            // Pipe: | or |= or |>
            case '|':
                if (match_char('>')) {
                    emit_at(TokenType::PipeArrow, "|>", start_loc);
                } else if (match_char('=')) {
                    emit_at(TokenType::PipeAssign, "|=", start_loc);
                } else {
                    emit_at(TokenType::Pipe, "|", start_loc);
                }
                return;

            // Caret: ^ or ^=
            case '^':
                if (match_char('=')) {
                    emit_at(TokenType::CaretAssign, "^=", start_loc);
                } else {
                    emit_at(TokenType::Caret, "^", start_loc);
                }
                return;

            // Less: < or <= or << or <<=
            case '<':
                if (match_char('<')) {
                    if (match_char('=')) {
                        emit_at(TokenType::ShiftLeftAssign, "<<=", start_loc);
                    } else {
                        emit_at(TokenType::ShiftLeft, "<<", start_loc);
                    }
                } else if (match_char('=')) {
                    emit_at(TokenType::LessEqual, "<=", start_loc);
                } else {
                    emit_at(TokenType::Less, "<", start_loc);
                }
                return;

            // Greater: > or >= or >> or >>=
            case '>':
                if (match_char('>')) {
                    if (match_char('=')) {
                        emit_at(TokenType::ShiftRightAssign, ">>=", start_loc);
                    } else {
                        emit_at(TokenType::ShiftRight, ">>", start_loc);
                    }
                } else if (match_char('=')) {
                    emit_at(TokenType::GreaterEqual, ">=", start_loc);
                } else {
                    emit_at(TokenType::Greater, ">", start_loc);
                }
                return;

            // Equal: = or ==
            case '=':
                if (match_char('=')) {
                    emit_at(TokenType::Equal, "==", start_loc);
                } else {
                    emit_at(TokenType::Assign, "=", start_loc);
                }
                return;

            // Not equal: !=
            case '!':
                if (match_char('=')) {
                    emit_at(TokenType::NotEqual, "!=", start_loc);
                } else {
                    error("unexpected character '!', did you mean 'not'?");
                }
                return;

            default:
                error(std::format("unexpected character: '{}'", c));
        }
    }

    // --- Character classification ---
    static bool is_digit(char c) { return c >= '0' && c <= '9'; }
    static bool is_hex_digit(char c) {
        return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }
    static bool is_octal_digit(char c) { return c >= '0' && c <= '7'; }
    static bool is_ident_start(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    static bool is_ident_char(char c) {
        return is_ident_start(c) || is_digit(c);
    }
    static int hex_val(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + c - 'a';
        if (c >= 'A' && c <= 'F') return 10 + c - 'A';
        return 0;
    }
};

} // namespace atomic
#include "expr.hpp"
#include <cctype>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

enum class TokType { IDENT, KW_AND, KW_OR, KW_NOT, LPAREN, RPAREN, END, ERR };

struct Token {
    TokType     type;
    std::string text;
};

struct Lexer {
    const char *p;
    const char *end;

    explicit Lexer(const char *src) : p(src), end(src + strlen(src)) {}

    void skipWs() {
        while (p < end && isspace((unsigned char)*p)) p++;
    }

    Token next() {
        skipWs();
        if (p >= end) return {TokType::END, ""};

        if (*p == '(') { p++; return {TokType::LPAREN, "("}; }
        if (*p == ')') { p++; return {TokType::RPAREN, ")"}; }

        // Identifier or keyword
        if (isalpha((unsigned char)*p) || *p == '_') {
            const char *start = p;
            while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
            std::string word(start, p);

            // Case-insensitive keyword check
            char upper[16] = {};
            for (size_t i = 0; i < word.size() && i < 15; i++)
                upper[i] = (char)toupper((unsigned char)word[i]);

            if (strcmp(upper, "AND") == 0) return {TokType::KW_AND, word};
            if (strcmp(upper, "OR")  == 0) return {TokType::KW_OR,  word};
            if (strcmp(upper, "NOT") == 0) return {TokType::KW_NOT, word};

            return {TokType::IDENT, word};
        }

        return {TokType::ERR, std::string(1, *p++)};
    }

    // Peek without consuming
    Token peek() {
        const char *save = p;
        Token t = next();
        p = save;
        return t;
    }
};

// ---------------------------------------------------------------------------
// Recursive descent parser
// ---------------------------------------------------------------------------

struct Parser {
    Lexer       lex;
    std::string err;
    bool        failed = false;

    explicit Parser(const char *src) : lex(src) {}

    std::unique_ptr<ExprNode> parseOr();
    std::unique_ptr<ExprNode> parseAnd();
    std::unique_ptr<ExprNode> parseNot();
    std::unique_ptr<ExprNode> parseAtom();
};

std::unique_ptr<ExprNode> Parser::parseAtom()
{
    Token t = lex.next();
    if (t.type == TokType::IDENT) {
        auto node = std::make_unique<ExprNode>();
        node->op   = ExprNode::Op::LEAF;
        node->name = t.text;
        return node;
    }
    if (t.type == TokType::LPAREN) {
        auto node = parseOr();
        Token close = lex.next();
        if (close.type != TokType::RPAREN) {
            err = "expected ')'";
            failed = true;
            return nullptr;
        }
        return node;
    }
    err = "unexpected token '" + t.text + "'";
    failed = true;
    return nullptr;
}

std::unique_ptr<ExprNode> Parser::parseNot()
{
    Token t = lex.peek();
    if (t.type == TokType::KW_NOT) {
        lex.next(); // consume NOT
        auto operand = parseNot();
        if (failed || !operand) return nullptr;
        auto node = std::make_unique<ExprNode>();
        node->op   = ExprNode::Op::NOT;
        node->left = std::move(operand);
        return node;
    }
    return parseAtom();
}

std::unique_ptr<ExprNode> Parser::parseAnd()
{
    auto left = parseNot();
    if (failed || !left) return nullptr;

    while (true) {
        Token t = lex.peek();
        if (t.type != TokType::KW_AND) break;
        lex.next(); // consume AND
        auto right = parseNot();
        if (failed || !right) return nullptr;
        auto node = std::make_unique<ExprNode>();
        node->op    = ExprNode::Op::AND;
        node->left  = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

std::unique_ptr<ExprNode> Parser::parseOr()
{
    auto left = parseAnd();
    if (failed || !left) return nullptr;

    while (true) {
        Token t = lex.peek();
        if (t.type != TokType::KW_OR) break;
        lex.next(); // consume OR
        auto right = parseAnd();
        if (failed || !right) return nullptr;
        auto node = std::make_unique<ExprNode>();
        node->op    = ExprNode::Op::OR;
        node->left  = std::move(left);
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

// ---------------------------------------------------------------------------
// Evaluator
// ---------------------------------------------------------------------------

bool ExprNode::eval(const std::map<std::string, bool> &states) const
{
    switch (op) {
        case Op::LEAF: {
            auto it = states.find(name);
            return it != states.end() ? it->second : false;
        }
        case Op::NOT:
            return left ? !left->eval(states) : false;
        case Op::AND:
            return (left  ? left->eval(states)  : false) &&
                   (right ? right->eval(states) : false);
        case Op::OR:
            return (left  ? left->eval(states)  : false) ||
                   (right ? right->eval(states) : false);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::unique_ptr<ExprNode> parseExpr(const char *text, std::string &err)
{
    if (!text || !*text) {
        err = "empty expression";
        return nullptr;
    }

    Parser p(text);
    auto root = p.parseOr();
    if (p.failed || !root) {
        err = p.err.empty() ? "parse error" : p.err;
        return nullptr;
    }

    // Ensure we consumed everything
    Token leftover = p.lex.peek();
    if (leftover.type != TokType::END) {
        err = "unexpected token '" + leftover.text + "' after expression";
        return nullptr;
    }

    err.clear();
    return root;
}

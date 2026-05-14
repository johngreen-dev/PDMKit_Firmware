#pragma once
#include <string>
#include <map>
#include <memory>

struct ExprNode {
    enum class Op { LEAF, NOT, AND, OR } op = Op::LEAF;
    std::string               name;   // LEAF: pin/var name
    std::unique_ptr<ExprNode> left;   // AND/OR left; NOT operand
    std::unique_ptr<ExprNode> right;  // AND/OR right

    bool eval(const std::map<std::string, bool> &states) const;
};

// Returns nullptr and sets err on failure.
std::unique_ptr<ExprNode> parseExpr(const char *text, std::string &err);

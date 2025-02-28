#include "lang/lexer.hpp"

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

namespace hex::lang {

#define TOKEN(type, value) Token::Type::type, Token::type::value, lineNumber
#define VALUE_TOKEN(type, value) Token::Type::type, value, lineNumber

    Lexer::Lexer() { }

    std::string matchTillInvalid(const char* characters, std::function<bool(char)> predicate) {
        std::string ret;

        while (*characters != 0x00) {
            ret += *characters;
            characters++;

            if (!predicate(*characters))
                break;
        }

        return ret;
    }

    size_t getIntegerLiteralLength(std::string_view string) {
        return string.find_first_not_of("0123456789ABCDEFabcdef.xUL");
    }

    std::optional<Token::IntegerLiteral> parseIntegerLiteral(std::string_view string) {
        Token::ValueType type = Token::ValueType::Any;
        Token::IntegerLiteral result;

        u8 base;

        auto endPos = getIntegerLiteralLength(string);
        std::string_view numberData = string.substr(0, endPos);

        if (numberData.ends_with('U')) {
            type = Token::ValueType::Unsigned32Bit;
            numberData.remove_suffix(1);
        } else if (numberData.ends_with("UL")) {
            type = Token::ValueType::Unsigned64Bit;
            numberData.remove_suffix(2);
        } else if (numberData.ends_with("ULL")) {
            type = Token::ValueType::Unsigned128Bit;
            numberData.remove_suffix(3);
        } else if (numberData.ends_with("L")) {
            type = Token::ValueType::Signed64Bit;
            numberData.remove_suffix(1);
        } else if (numberData.ends_with("LL")) {
            type = Token::ValueType::Signed128Bit;
            numberData.remove_suffix(2);
        } else if (!numberData.starts_with("0x") && !numberData.starts_with("0b")) {
            if (numberData.ends_with('F')) {
                type = Token::ValueType::Float;
                numberData.remove_suffix(1);
            } else if (numberData.ends_with('D')) {
                type = Token::ValueType::Double;
                numberData.remove_suffix(1);
            }
        }

        if (numberData.starts_with("0x")) {
            numberData = numberData.substr(2);
            base = 16;

            if (Token::isFloatingPoint(type))
                return { };

            if (numberData.find_first_not_of("0123456789ABCDEFabcdef") != std::string_view::npos)
                return { };
        } else if (numberData.starts_with("0b")) {
            numberData = numberData.substr(2);
            base = 2;

            if (Token::isFloatingPoint(type))
                return { };

            if (numberData.find_first_not_of("01") != std::string_view::npos)
                return { };
        } else if (numberData.find('.') != std::string_view::npos || Token::isFloatingPoint(type)) {
            base = 10;
            if (type == Token::ValueType::Any)
                type = Token::ValueType::Double;

            if (std::count(numberData.begin(), numberData.end(), '.') > 1 || numberData.find_first_not_of("0123456789.") != std::string_view::npos)
                return { };

            if (numberData.ends_with('.'))
                return { };
        } else if (isdigit(numberData[0])) {
            base = 10;

            if (numberData.find_first_not_of("0123456789") != std::string_view::npos)
                return { };
        } else return { };

        if (type == Token::ValueType::Any)
            type = Token::ValueType::Signed32Bit;


        if (numberData.length() == 0)
            return { };

        if (Token::isUnsigned(type) || Token::isSigned(type)) {
            u128 integer = 0;
            for (const char& c : numberData) {
                integer *= base;

                if (isdigit(c))
                    integer += (c - '0');
                else if (c >= 'A' && c <= 'F')
                    integer += 10 + (c - 'A');
                else if (c >= 'a' && c <= 'f')
                    integer += 10 + (c - 'a');
                else return { };
            }

            switch (type) {
                case Token::ValueType::Unsigned32Bit:  return {{ type, u32(integer) }};
                case Token::ValueType::Signed32Bit:    return {{ type, s32(integer) }};
                case Token::ValueType::Unsigned64Bit:  return {{ type, u64(integer) }};
                case Token::ValueType::Signed64Bit:    return {{ type, s64(integer) }};
                case Token::ValueType::Unsigned128Bit: return {{ type, u128(integer) }};
                case Token::ValueType::Signed128Bit:   return {{ type, s128(integer) }};
                default: return { };
            }
        } else if (Token::isFloatingPoint(type)) {
            double floatingPoint = strtod(numberData.data(), nullptr);

            switch (type) {
                case Token::ValueType::Float:  return {{ type, float(floatingPoint) }};
                case Token::ValueType::Double: return {{ type, double(floatingPoint) }};
                default: return { };
            }
        }


        return { };
    }

    std::optional<std::vector<Token>> Lexer::lex(const std::string& code) {
        std::vector<Token> tokens;
        u32 offset = 0;

        u32 lineNumber = 1;

        try {

            while (offset < code.length()) {
                const char& c = code[offset];

                if (c == 0x00)
                    break;

                if (std::isblank(c) || std::isspace(c)) {
                    if (code[offset] == '\n') lineNumber++;
                    offset += 1;
                } else if (c == ';') {
                    tokens.emplace_back(TOKEN(Separator, EndOfExpression));
                    offset += 1;
                } else if (c == '(') {
                    tokens.emplace_back(TOKEN(Separator, RoundBracketOpen));
                    offset += 1;
                } else if (c == ')') {
                    tokens.emplace_back(TOKEN(Separator, RoundBracketClose));
                    offset += 1;
                } else if (c == '{') {
                    tokens.emplace_back(TOKEN(Separator, CurlyBracketOpen));
                    offset += 1;
                } else if (c == '}') {
                    tokens.emplace_back(TOKEN(Separator, CurlyBracketClose));
                    offset += 1;
                } else if (c == '[') {
                    tokens.emplace_back(TOKEN(Separator, SquareBracketOpen));
                    offset += 1;
                } else if (c == ']') {
                    tokens.emplace_back(TOKEN(Separator, SquareBracketClose));
                    offset += 1;
                } else if (c == ',') {
                    tokens.emplace_back(TOKEN(Separator, Comma));
                    offset += 1;
                } else if (c == '.') {
                    tokens.emplace_back(TOKEN(Separator, Dot));
                    offset += 1;
                } else if (c == '@') {
                    tokens.emplace_back(TOKEN(Operator, AtDeclaration));
                    offset += 1;
                } else if (code.substr(offset, 2) == "==") {
                    tokens.emplace_back(TOKEN(Operator, BoolEquals));
                    offset += 2;
                } else if (code.substr(offset, 2) == "!=") {
                    tokens.emplace_back(TOKEN(Operator, BoolNotEquals));
                    offset += 2;
                } else if (code.substr(offset, 2) == ">=") {
                    tokens.emplace_back(TOKEN(Operator, BoolGreaterThanOrEquals));
                    offset += 2;
                } else if (code.substr(offset, 2) == "<=") {
                    tokens.emplace_back(TOKEN(Operator, BoolLessThanOrEquals));
                    offset += 2;
                } else if (code.substr(offset, 2) == "&&") {
                    tokens.emplace_back(TOKEN(Operator, BoolAnd));
                    offset += 2;
                } else if (code.substr(offset, 2) == "||") {
                    tokens.emplace_back(TOKEN(Operator, BoolOr));
                    offset += 2;
                } else if (code.substr(offset, 2) == "^^") {
                    tokens.emplace_back(TOKEN(Operator, BoolXor));
                    offset += 2;
                } else if (c == '=') {
                    tokens.emplace_back(TOKEN(Operator, Assignment));
                    offset += 1;
                } else if (code.substr(offset, 2) == "::") {
                    tokens.emplace_back(TOKEN(Separator, ScopeResolution));
                    offset += 2;
                } else if (c == ':') {
                    tokens.emplace_back(TOKEN(Operator, Inherit));
                    offset += 1;
                } else if (c == '+') {
                    tokens.emplace_back(TOKEN(Operator, Plus));
                    offset += 1;
                } else if (c == '-') {
                    tokens.emplace_back(TOKEN(Operator, Minus));
                    offset += 1;
                } else if (c == '*') {
                    tokens.emplace_back(TOKEN(Operator, Star));
                    offset += 1;
                } else if (c == '/') {
                    tokens.emplace_back(TOKEN(Operator, Slash));
                    offset += 1;
                } else if (code.substr(offset, 2) == "<<") {
                    tokens.emplace_back(TOKEN(Operator, ShiftLeft));
                    offset += 2;
                } else if (code.substr(offset, 2) == ">>") {
                    tokens.emplace_back(TOKEN(Operator, ShiftRight));
                    offset += 2;
                } else if (c == '>') {
                    tokens.emplace_back(TOKEN(Operator, BoolGreaterThan));
                    offset += 1;
                } else if (c == '<') {
                    tokens.emplace_back(TOKEN(Operator, BoolLessThan));
                    offset += 1;
                } else if (c == '!') {
                    tokens.emplace_back(TOKEN(Operator, BoolNot));
                    offset += 1;
                } else if (c == '|') {
                    tokens.emplace_back(TOKEN(Operator, BitOr));
                    offset += 1;
                } else if (c == '&') {
                    tokens.emplace_back(TOKEN(Operator, BitAnd));
                    offset += 1;
                } else if (c == '^') {
                    tokens.emplace_back(TOKEN(Operator, BitXor));
                    offset += 1;
                } else if (c == '~') {
                    tokens.emplace_back(TOKEN(Operator, BitNot));
                    offset += 1;
                } else if (c == '?') {
                    tokens.emplace_back(TOKEN(Operator, TernaryConditional));
                    offset += 1;
                } else if (c == '\'') {
                    offset += 1;

                    if (offset >= code.length())
                        throwLexerError("invalid character literal", lineNumber);

                    char character = code[offset];

                    if (character == '\\') {
                        offset += 1;

                        if (offset >= code.length())
                            throwLexerError("invalid character literal", lineNumber);

                        if (code[offset] != '\\' && code[offset] != '\'')
                            throwLexerError("invalid escape sequence", lineNumber);


                        character = code[offset];
                    } else {
                        if (code[offset] == '\\' || code[offset] == '\'' || character == '\n' || character == '\r')
                            throwLexerError("invalid character literal", lineNumber);

                    }

                    offset += 1;

                    if (offset >= code.length() || code[offset] != '\'')
                        throwLexerError("missing terminating ' after character literal", lineNumber);

                    tokens.emplace_back(VALUE_TOKEN(Integer, Token::IntegerLiteral({ Token::ValueType::Character, character }) ));
                    offset += 1;

                } else if (std::isalpha(c)) {
                    std::string identifier = matchTillInvalid(&code[offset], [](char c) -> bool { return std::isalnum(c) || c == '_'; });

                    // Check for reserved keywords

                    if (identifier == "struct")
                        tokens.emplace_back(TOKEN(Keyword, Struct));
                    else if (identifier == "union")
                        tokens.emplace_back(TOKEN(Keyword, Union));
                    else if (identifier == "using")
                        tokens.emplace_back(TOKEN(Keyword, Using));
                    else if (identifier == "enum")
                        tokens.emplace_back(TOKEN(Keyword, Enum));
                    else if (identifier == "bitfield")
                        tokens.emplace_back(TOKEN(Keyword, Bitfield));
                    else if (identifier == "be")
                        tokens.emplace_back(TOKEN(Keyword, BigEndian));
                    else if (identifier == "le")
                        tokens.emplace_back(TOKEN(Keyword, LittleEndian));
                    else if (identifier == "if")
                        tokens.emplace_back(TOKEN(Keyword, If));
                    else if (identifier == "else")
                        tokens.emplace_back(TOKEN(Keyword, Else));

                        // Check for built-in types
                    else if (identifier == "u8")
                        tokens.emplace_back(TOKEN(ValueType, Unsigned8Bit));
                    else if (identifier == "s8")
                        tokens.emplace_back(TOKEN(ValueType, Signed8Bit));
                    else if (identifier == "u16")
                        tokens.emplace_back(TOKEN(ValueType, Unsigned16Bit));
                    else if (identifier == "s16")
                        tokens.emplace_back(TOKEN(ValueType, Signed16Bit));
                    else if (identifier == "u32")
                        tokens.emplace_back(TOKEN(ValueType, Unsigned32Bit));
                    else if (identifier == "s32")
                        tokens.emplace_back(TOKEN(ValueType, Signed32Bit));
                    else if (identifier == "u64")
                        tokens.emplace_back(TOKEN(ValueType, Unsigned64Bit));
                    else if (identifier == "s64")
                        tokens.emplace_back(TOKEN(ValueType, Signed64Bit));
                    else if (identifier == "u128")
                        tokens.emplace_back(TOKEN(ValueType, Unsigned128Bit));
                    else if (identifier == "s128")
                        tokens.emplace_back(TOKEN(ValueType, Signed128Bit));
                    else if (identifier == "float")
                        tokens.emplace_back(TOKEN(ValueType, Float));
                    else if (identifier == "double")
                        tokens.emplace_back(TOKEN(ValueType, Double));
                    else if (identifier == "char")
                        tokens.emplace_back(TOKEN(ValueType, Character));
                    else if (identifier == "padding")
                        tokens.emplace_back(TOKEN(ValueType, Padding));

                    // If it's not a keyword and a builtin type, it has to be an identifier

                    else
                        tokens.emplace_back(VALUE_TOKEN(Identifier, identifier));

                    offset += identifier.length();
                } else if (std::isdigit(c)) {
                    auto integer = parseIntegerLiteral(&code[offset]);

                    if (!integer.has_value())
                        throwLexerError("invalid integer literal", lineNumber);


                    tokens.emplace_back(VALUE_TOKEN(Integer, integer.value()));
                    offset += getIntegerLiteralLength(&code[offset]);
                } else
                    throwLexerError("unknown token", lineNumber);

            }

            tokens.emplace_back(TOKEN(Separator, EndOfProgram));
        } catch (LexerError &e) {
            this->m_error = e;
            return { };
        }


        return tokens;
    }
}
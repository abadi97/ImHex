#include "lang/evaluator.hpp"

#include "lang/token.hpp"
#include "helpers/utils.hpp"

#include <bit>
#include <algorithm>

#include <unistd.h>

namespace hex::lang {

    Evaluator::Evaluator(prv::Provider* &provider, std::endian defaultDataEndian)
        : m_provider(provider), m_defaultDataEndian(defaultDataEndian) {

        this->addFunction("findSequence", Function::MoreParametersThan | 1, [this](auto params) {
            return this->findSequence(params);
        });

        this->addFunction("readUnsigned", 2, [this](auto params) {
            return this->readUnsigned(params);
        });

        this->addFunction("readSigned", 2, [this](auto params) {
            return this->readSigned(params);
        });
    }

    ASTNodeIntegerLiteral* Evaluator::evaluateScopeResolution(ASTNodeScopeResolution *node) {
        ASTNode *currScope = nullptr;
        for (const auto &identifier : node->getPath()) {
            if (currScope == nullptr) {
                if (!this->m_types.contains(identifier))
                    break;

                currScope = this->m_types[identifier.data()];
            } else if (auto enumNode = dynamic_cast<ASTNodeEnum*>(currScope); enumNode != nullptr) {
                if (!enumNode->getEntries().contains(identifier))
                    break;
                else
                    return evaluateMathematicalExpression(static_cast<ASTNodeNumericExpression*>(enumNode->getEntries().at(identifier)));
            }
        }

        throwEvaluateError("failed to find identifier", node->getLineNumber());
    }

    ASTNodeIntegerLiteral* Evaluator::evaluateRValue(ASTNodeRValue *node) {

        const std::vector<PatternData*>* currMembers = this->m_currMembers.back();

        PatternData *currPattern = nullptr;
        for (const auto &identifier : node->getPath()) {
            if (auto structPattern = dynamic_cast<PatternDataStruct*>(currPattern); structPattern != nullptr)
                currMembers = &structPattern->getMembers();
            else if (auto unionPattern = dynamic_cast<PatternDataUnion*>(currPattern); unionPattern != nullptr)
                currMembers = &unionPattern->getMembers();
            else if (currPattern != nullptr)
                throwEvaluateError("tried to access member of a non-struct/union type", node->getLineNumber());

            auto candidate = std::find_if(currMembers->begin(), currMembers->end(), [&](auto member) {
                return member->getVariableName() == identifier;
            });

            if (candidate != currMembers->end())
                currPattern = *candidate;
            else
                throwEvaluateError(hex::format("could not find identifier '%s'", identifier.c_str()), node->getLineNumber());
        }

        if (auto unsignedPattern = dynamic_cast<PatternDataUnsigned*>(currPattern); unsignedPattern != nullptr) {
            u8 value[unsignedPattern->getSize()];
            this->m_provider->read(unsignedPattern->getOffset(), value, unsignedPattern->getSize());

            switch (unsignedPattern->getSize()) {
                case 1:  return new ASTNodeIntegerLiteral({ Token::ValueType::Unsigned8Bit,   hex::changeEndianess(*reinterpret_cast<u8*>(value), 1, this->getCurrentEndian()) });
                case 2:  return new ASTNodeIntegerLiteral({ Token::ValueType::Unsigned16Bit,  hex::changeEndianess(*reinterpret_cast<u16*>(value), 2, this->getCurrentEndian()) });
                case 4:  return new ASTNodeIntegerLiteral({ Token::ValueType::Unsigned32Bit,  hex::changeEndianess(*reinterpret_cast<u32*>(value), 4, this->getCurrentEndian()) });
                case 8:  return new ASTNodeIntegerLiteral({ Token::ValueType::Unsigned64Bit,  hex::changeEndianess(*reinterpret_cast<u64*>(value), 8, this->getCurrentEndian()) });
                case 16: return new ASTNodeIntegerLiteral({ Token::ValueType::Unsigned128Bit, hex::changeEndianess(*reinterpret_cast<u128*>(value), 16, this->getCurrentEndian()) });
                default: throwEvaluateError("invalid rvalue size", node->getLineNumber());
            }
        } else if (auto signedPattern = dynamic_cast<PatternDataSigned*>(currPattern); signedPattern != nullptr) {
            u8 value[unsignedPattern->getSize()];
            this->m_provider->read(signedPattern->getOffset(), value, signedPattern->getSize());

            switch (unsignedPattern->getSize()) {
                case 1:  return new ASTNodeIntegerLiteral({ Token::ValueType::Signed8Bit,   hex::changeEndianess(*reinterpret_cast<s8*>(value), 1, this->getCurrentEndian()) });
                case 2:  return new ASTNodeIntegerLiteral({ Token::ValueType::Signed16Bit,  hex::changeEndianess(*reinterpret_cast<s16*>(value), 2, this->getCurrentEndian()) });
                case 4:  return new ASTNodeIntegerLiteral({ Token::ValueType::Signed32Bit,  hex::changeEndianess(*reinterpret_cast<s32*>(value), 4, this->getCurrentEndian()) });
                case 8:  return new ASTNodeIntegerLiteral({ Token::ValueType::Signed64Bit,  hex::changeEndianess(*reinterpret_cast<s64*>(value), 8, this->getCurrentEndian()) });
                case 16: return new ASTNodeIntegerLiteral({ Token::ValueType::Signed128Bit, hex::changeEndianess(*reinterpret_cast<s128*>(value), 16, this->getCurrentEndian()) });
                default: throwEvaluateError("invalid rvalue size", node->getLineNumber());
            }
        } else if (auto enumPattern = dynamic_cast<PatternDataEnum*>(currPattern); enumPattern != nullptr) {
            u8 value[enumPattern->getSize()];
            this->m_provider->read(enumPattern->getOffset(), value, enumPattern->getSize());

            switch (enumPattern->getSize()) {
                case 1:  return new ASTNodeIntegerLiteral({ Token::ValueType::Unsigned8Bit,   hex::changeEndianess(*reinterpret_cast<u8*>(value), 1, this->getCurrentEndian()) });
                case 2:  return new ASTNodeIntegerLiteral({ Token::ValueType::Unsigned16Bit,  hex::changeEndianess(*reinterpret_cast<u16*>(value), 2, this->getCurrentEndian()) });
                case 4:  return new ASTNodeIntegerLiteral({ Token::ValueType::Unsigned32Bit,  hex::changeEndianess(*reinterpret_cast<u32*>(value), 4, this->getCurrentEndian()) });
                case 8:  return new ASTNodeIntegerLiteral({ Token::ValueType::Unsigned64Bit,  hex::changeEndianess(*reinterpret_cast<u64*>(value), 8, this->getCurrentEndian()) });
                case 16: return new ASTNodeIntegerLiteral({ Token::ValueType::Unsigned128Bit, hex::changeEndianess(*reinterpret_cast<u128*>(value), 16, this->getCurrentEndian()) });
                default: throwEvaluateError("invalid rvalue size", node->getLineNumber());
            }
        } else
            throwEvaluateError("tried to use non-integer value in numeric expression", node->getLineNumber());
    }

    ASTNodeIntegerLiteral* Evaluator::evaluateFunctionCall(ASTNodeFunctionCall *node) {
        std::vector<ASTNodeIntegerLiteral*> evaluatedParams;
        ScopeExit paramCleanup([&] {
           for (auto &param : evaluatedParams)
               delete param;
        });

        for (auto &param : node->getParams())
            evaluatedParams.push_back(this->evaluateMathematicalExpression(static_cast<ASTNodeNumericExpression*>(param)));

        if (!this->m_functions.contains(node->getFunctionName().data()))
            throwEvaluateError(hex::format("no function named '%s' found", node->getFunctionName().data()), node->getLineNumber());

        auto &function = this->m_functions[node->getFunctionName().data()];

        if (function.parameterCount == Function::UnlimitedParameters) {
            ; // Don't check parameter count
        }
        else if (function.parameterCount & Function::LessParametersThan) {
            if (evaluatedParams.size() >= (function.parameterCount & ~Function::LessParametersThan))
                throwEvaluateError(hex::format("too many parameters for function '%s'. Expected %d", node->getFunctionName().data(), function.parameterCount & ~Function::LessParametersThan), node->getLineNumber());
        } else if (function.parameterCount & Function::MoreParametersThan) {
            if (evaluatedParams.size() <= (function.parameterCount & ~Function::MoreParametersThan))
                throwEvaluateError(hex::format("too few parameters for function '%s'. Expected %d", node->getFunctionName().data(), function.parameterCount & ~Function::MoreParametersThan), node->getLineNumber());
        } else if (function.parameterCount != evaluatedParams.size()) {
            throwEvaluateError(hex::format("invalid number of parameters for function '%s'. Expected %d", node->getFunctionName().data(), function.parameterCount), node->getLineNumber());
        }

        return function.func(evaluatedParams);
    }

#define FLOAT_BIT_OPERATION(name) \
    auto name(std::floating_point auto left, auto right) { throw std::runtime_error(""); return 0; } \
    auto name(auto left, std::floating_point auto right) { throw std::runtime_error(""); return 0; } \
    auto name(std::floating_point auto left, std::floating_point auto right) { throw std::runtime_error(""); return 0; } \
    auto name(std::integral auto left, std::integral auto right)

    namespace {

        FLOAT_BIT_OPERATION(shiftLeft) {
            return left << right;
        }

        FLOAT_BIT_OPERATION(shiftRight) {
            return left >> right;
        }

        FLOAT_BIT_OPERATION(bitAnd) {
            return left & right;
        }

        FLOAT_BIT_OPERATION(bitOr) {
            return left | right;
        }

        FLOAT_BIT_OPERATION(bitXor) {
            return left ^ right;
        }

        FLOAT_BIT_OPERATION(bitNot) {
            return ~right;
        }

    }

    ASTNodeIntegerLiteral* Evaluator::evaluateOperator(ASTNodeIntegerLiteral *left, ASTNodeIntegerLiteral *right, Token::Operator op) {
        auto newType = [&] {
            #define CHECK_TYPE(type) if (left->getType() == (type) || right->getType() == (type)) return (type)
            #define DEFAULT_TYPE(type) return (type)

            CHECK_TYPE(Token::ValueType::Double);
            CHECK_TYPE(Token::ValueType::Float);
            CHECK_TYPE(Token::ValueType::Unsigned128Bit);
            CHECK_TYPE(Token::ValueType::Signed128Bit);
            CHECK_TYPE(Token::ValueType::Unsigned64Bit);
            CHECK_TYPE(Token::ValueType::Signed64Bit);
            CHECK_TYPE(Token::ValueType::Unsigned32Bit);
            CHECK_TYPE(Token::ValueType::Signed32Bit);
            CHECK_TYPE(Token::ValueType::Unsigned16Bit);
            CHECK_TYPE(Token::ValueType::Signed16Bit);
            CHECK_TYPE(Token::ValueType::Unsigned8Bit);
            CHECK_TYPE(Token::ValueType::Signed8Bit);
            CHECK_TYPE(Token::ValueType::Character);
            DEFAULT_TYPE(Token::ValueType::Signed32Bit);

            #undef CHECK_TYPE
            #undef DEFAULT_TYPE
        }();

        try {
            return std::visit([&](auto &&leftValue, auto &&rightValue) -> ASTNodeIntegerLiteral * {
                switch (op) {
                    case Token::Operator::Plus:
                        return new ASTNodeIntegerLiteral({ newType, leftValue + rightValue });
                    case Token::Operator::Minus:
                        return new ASTNodeIntegerLiteral({ newType, leftValue - rightValue });
                    case Token::Operator::Star:
                        return new ASTNodeIntegerLiteral({ newType, leftValue * rightValue });
                    case Token::Operator::Slash:
                        return new ASTNodeIntegerLiteral({ newType, leftValue / rightValue });
                    case Token::Operator::ShiftLeft:
                        return new ASTNodeIntegerLiteral({ newType, shiftLeft(leftValue, rightValue) });
                    case Token::Operator::ShiftRight:
                        return new ASTNodeIntegerLiteral({ newType, shiftRight(leftValue, rightValue) });
                    case Token::Operator::BitAnd:
                        return new ASTNodeIntegerLiteral({ newType, bitAnd(leftValue, rightValue) });
                    case Token::Operator::BitXor:
                        return new ASTNodeIntegerLiteral({ newType, bitXor(leftValue, rightValue) });
                    case Token::Operator::BitOr:
                        return new ASTNodeIntegerLiteral({ newType, bitOr(leftValue, rightValue) });
                    case Token::Operator::BitNot:
                        return new ASTNodeIntegerLiteral({ newType, bitNot(leftValue, rightValue) });
                    case Token::Operator::BoolEquals:
                        return new ASTNodeIntegerLiteral({ newType, leftValue == rightValue });
                    case Token::Operator::BoolNotEquals:
                        return new ASTNodeIntegerLiteral({ newType, leftValue != rightValue });
                    case Token::Operator::BoolGreaterThan:
                        return new ASTNodeIntegerLiteral({ newType, leftValue > rightValue });
                    case Token::Operator::BoolLessThan:
                        return new ASTNodeIntegerLiteral({ newType, leftValue < rightValue });
                    case Token::Operator::BoolGreaterThanOrEquals:
                        return new ASTNodeIntegerLiteral({ newType, leftValue >= rightValue });
                    case Token::Operator::BoolLessThanOrEquals:
                        return new ASTNodeIntegerLiteral({ newType, leftValue <= rightValue });
                    case Token::Operator::BoolAnd:
                        return new ASTNodeIntegerLiteral({ newType, leftValue && rightValue });
                    case Token::Operator::BoolXor:
                        return new ASTNodeIntegerLiteral({ newType, leftValue && !rightValue || !leftValue && rightValue });
                    case Token::Operator::BoolOr:
                        return new ASTNodeIntegerLiteral({ newType, leftValue || rightValue });
                    case Token::Operator::BoolNot:
                        return new ASTNodeIntegerLiteral({ newType, !rightValue });
                    default:
                        throwEvaluateError("invalid operator used in mathematical expression", left->getLineNumber());
                }

            }, left->getValue(), right->getValue());
        } catch (std::runtime_error &e) {
            throwEvaluateError("bitwise operations on floating point numbers are forbidden", left->getLineNumber());
        }
    }

    ASTNodeIntegerLiteral* Evaluator::evaluateOperand(ASTNode *node) {
        if (auto exprLiteral = dynamic_cast<ASTNodeIntegerLiteral*>(node); exprLiteral != nullptr)
            return exprLiteral;
        else if (auto exprExpression = dynamic_cast<ASTNodeNumericExpression*>(node); exprExpression != nullptr)
            return evaluateMathematicalExpression(exprExpression);
        else if (auto exprRvalue = dynamic_cast<ASTNodeRValue*>(node); exprRvalue != nullptr)
            return evaluateRValue(exprRvalue);
        else if (auto exprScopeResolution = dynamic_cast<ASTNodeScopeResolution*>(node); exprScopeResolution != nullptr)
            return evaluateScopeResolution(exprScopeResolution);
        else if (auto exprTernary = dynamic_cast<ASTNodeTernaryExpression*>(node); exprTernary != nullptr)
            return evaluateTernaryExpression(exprTernary);
        else if (auto exprFunctionCall = dynamic_cast<ASTNodeFunctionCall*>(node); exprFunctionCall != nullptr)
            return evaluateFunctionCall(exprFunctionCall);
        else
            throwEvaluateError("invalid operand", node->getLineNumber());
    }

    ASTNodeIntegerLiteral* Evaluator::evaluateTernaryExpression(ASTNodeTernaryExpression *node) {
        switch (node->getOperator()) {
            case Token::Operator::TernaryConditional: {
                auto condition = this->evaluateOperand(node->getFirstOperand());
                SCOPE_EXIT( delete condition; );

                if (std::visit([](auto &&value){ return value != 0; }, condition->getValue()))
                    return this->evaluateOperand(node->getSecondOperand());
                else
                    return this->evaluateOperand(node->getThirdOperand());
            }
            default:
                throwEvaluateError("invalid operator used in ternary expression", node->getLineNumber());
        }
    }

    ASTNodeIntegerLiteral* Evaluator::evaluateMathematicalExpression(ASTNodeNumericExpression *node) {
        auto leftInteger  = this->evaluateOperand(node->getLeftOperand());
        auto rightInteger = this->evaluateOperand(node->getRightOperand());

        return evaluateOperator(leftInteger, rightInteger, node->getOperator());
    }

    PatternData* Evaluator::evaluateBuiltinType(ASTNodeBuiltinType *node) {
        auto &type = node->getType();
        auto typeSize = Token::getTypeSize(type);

        PatternData *pattern;

        if (type == Token::ValueType::Character)
            pattern = new PatternDataCharacter(this->m_currOffset);
        else if (Token::isUnsigned(type))
            pattern = new PatternDataUnsigned(this->m_currOffset, typeSize);
        else if (Token::isSigned(type))
            pattern = new PatternDataSigned(this->m_currOffset, typeSize);
        else if (Token::isFloatingPoint(type))
            pattern = new PatternDataFloat(this->m_currOffset, typeSize);
        else
            throwEvaluateError("invalid builtin type", node->getLineNumber());

        this->m_currOffset += typeSize;

        pattern->setTypeName(Token::getTypeName(type));

        return pattern;
    }

    std::vector<PatternData*> Evaluator::evaluateMember(ASTNode *node) {
        this->m_currEndian.reset();

        if (auto memberVariableNode = dynamic_cast<ASTNodeVariableDecl*>(node); memberVariableNode != nullptr)
            return { this->evaluateVariable(memberVariableNode) };
        else if (auto memberArrayNode = dynamic_cast<ASTNodeArrayVariableDecl*>(node); memberArrayNode != nullptr)
            return { this->evaluateArray(memberArrayNode) };
        else if (auto memberPointerNode = dynamic_cast<ASTNodePointerVariableDecl*>(node); memberPointerNode != nullptr)
            return { this->evaluatePointer(memberPointerNode) };
        else if (auto conditionalNode = dynamic_cast<ASTNodeConditionalStatement*>(node); conditionalNode != nullptr) {
            auto condition = this->evaluateMathematicalExpression(static_cast<ASTNodeNumericExpression*>(conditionalNode->getCondition()));

            std::vector<PatternData*> patterns;
            if (std::visit([](auto &&value) { return value != 0; }, condition->getValue())) {
                for (auto &statement : conditionalNode->getTrueBody()) {
                    auto statementPatterns = this->evaluateMember(statement);
                    std::copy(statementPatterns.begin(), statementPatterns.end(), std::back_inserter(patterns));
                }
            } else {
                for (auto &statement : conditionalNode->getFalseBody()) {
                    auto statementPatterns = this->evaluateMember(statement);
                    std::copy(statementPatterns.begin(), statementPatterns.end(), std::back_inserter(patterns));
                }
            }

            delete condition;

            return patterns;
        }
        else
            throwEvaluateError("invalid struct member", node->getLineNumber());
    }

    PatternData* Evaluator::evaluateStruct(ASTNodeStruct *node) {
        std::vector<PatternData*> memberPatterns;

        this->m_currMembers.push_back(&memberPatterns);
        SCOPE_EXIT( this->m_currMembers.pop_back(); );

        auto startOffset = this->m_currOffset;
        for (auto &member : node->getMembers()) {
            auto newMembers = this->evaluateMember(member);
            std::copy(newMembers.begin(), newMembers.end(), std::back_inserter(memberPatterns));
        }

        return new PatternDataStruct(startOffset, this->m_currOffset - startOffset, memberPatterns);
    }

    PatternData* Evaluator::evaluateUnion(ASTNodeUnion *node) {
        std::vector<PatternData*> memberPatterns;

        this->m_currMembers.push_back(&memberPatterns);
        SCOPE_EXIT( this->m_currMembers.pop_back(); );

        auto startOffset = this->m_currOffset;
        for (auto &member : node->getMembers()) {
            auto newMembers = this->evaluateMember(member);
            std::copy(newMembers.begin(), newMembers.end(), std::back_inserter(memberPatterns));

            this->m_currOffset = startOffset;
        }

        return new PatternDataUnion(startOffset, this->m_currOffset - startOffset, memberPatterns);
    }

    PatternData* Evaluator::evaluateEnum(ASTNodeEnum *node) {
        std::vector<std::pair<Token::IntegerLiteral, std::string>> entryPatterns;

        auto startOffset = this->m_currOffset;
        for (auto &[name, value] : node->getEntries()) {
            auto expression = dynamic_cast<ASTNodeNumericExpression*>(value);
            if (expression == nullptr)
                throwEvaluateError("invalid expression in enum value", value->getLineNumber());

            auto valueNode = evaluateMathematicalExpression(expression);
            SCOPE_EXIT( delete valueNode; );

            entryPatterns.push_back({{ valueNode->getType(), valueNode->getValue() }, name });
        }

        size_t size;
        if (auto underlyingType = dynamic_cast<const ASTNodeBuiltinType*>(node->getUnderlyingType()); underlyingType != nullptr)
            size = Token::getTypeSize(underlyingType->getType());
        else
            throwEvaluateError("invalid enum underlying type", node->getLineNumber());

        return new PatternDataEnum(startOffset, size, entryPatterns);
    }

    PatternData* Evaluator::evaluateBitfield(ASTNodeBitfield *node) {
        std::vector<std::pair<std::string, size_t>> entryPatterns;

        auto startOffset = this->m_currOffset;
        size_t bits = 0;
        for (auto &[name, value] : node->getEntries()) {
            auto expression = dynamic_cast<ASTNodeNumericExpression*>(value);
            if (expression == nullptr)
                throwEvaluateError("invalid expression in bitfield field size", value->getLineNumber());

            auto valueNode = evaluateMathematicalExpression(expression);
            SCOPE_EXIT( delete valueNode; );

            auto fieldBits = std::visit([node, type = valueNode->getType()] (auto &&value) {
                if (Token::isFloatingPoint(type))
                    throwEvaluateError("bitfield entry size must be an integer value", node->getLineNumber());
                return static_cast<s128>(value);
            }, valueNode->getValue());

            if (fieldBits > 64 || fieldBits <= 0)
                throwEvaluateError("bitfield entry must occupy between 1 and 64 bits", value->getLineNumber());

            bits += fieldBits;

            entryPatterns.emplace_back(name, fieldBits);
        }

        return new PatternDataBitfield(startOffset, (bits / 8) + 1, entryPatterns);
    }

    PatternData* Evaluator::evaluateType(ASTNodeTypeDecl *node) {
        auto type = node->getType();

        if (!this->m_currEndian.has_value())
            this->m_currEndian = node->getEndian();

        PatternData *pattern;

        if (auto builtinTypeNode = dynamic_cast<ASTNodeBuiltinType*>(type); builtinTypeNode != nullptr)
            return this->evaluateBuiltinType(builtinTypeNode);
        else if (auto typeDeclNode = dynamic_cast<ASTNodeTypeDecl*>(type); typeDeclNode != nullptr)
            pattern = this->evaluateType(typeDeclNode);
        else if (auto structNode = dynamic_cast<ASTNodeStruct*>(type); structNode != nullptr)
            pattern = this->evaluateStruct(structNode);
        else if (auto unionNode = dynamic_cast<ASTNodeUnion*>(type); unionNode != nullptr)
            pattern = this->evaluateUnion(unionNode);
        else if (auto enumNode = dynamic_cast<ASTNodeEnum*>(type); enumNode != nullptr)
            pattern = this->evaluateEnum(enumNode);
        else if (auto bitfieldNode = dynamic_cast<ASTNodeBitfield*>(type); bitfieldNode != nullptr)
            pattern = this->evaluateBitfield(bitfieldNode);
        else
            throwEvaluateError("type could not be evaluated", node->getLineNumber());

        if (!node->getName().empty())
            pattern->setTypeName(node->getName().data());

        return pattern;
    }

    PatternData* Evaluator::evaluateVariable(ASTNodeVariableDecl *node) {

        if (auto offset = dynamic_cast<ASTNodeNumericExpression*>(node->getPlacementOffset()); offset != nullptr) {
            auto valueNode = evaluateMathematicalExpression(offset);
            SCOPE_EXIT( delete valueNode; );

            this->m_currOffset = std::visit([node, type = valueNode->getType()] (auto &&value) {
                if (Token::isFloatingPoint(type))
                    throwEvaluateError("placement offset must be an integer value", node->getLineNumber());
                return static_cast<u64>(value);
            }, valueNode->getValue());
        }
        if (this->m_currOffset >= this->m_provider->getActualSize())
            throwEvaluateError("array exceeds size of file", node->getLineNumber());

        PatternData *pattern;
        if (auto typeDecl = dynamic_cast<ASTNodeTypeDecl*>(node->getType()); typeDecl != nullptr)
            pattern = this->evaluateType(typeDecl);
        else if (auto builtinTypeDecl = dynamic_cast<ASTNodeBuiltinType*>(node->getType()); builtinTypeDecl != nullptr)
            pattern = this->evaluateBuiltinType(builtinTypeDecl);
        else
            throwEvaluateError("ASTNodeVariableDecl had an invalid type. This is a bug!", 1);

        pattern->setVariableName(node->getName().data());
        pattern->setEndian(this->getCurrentEndian());
        this->m_currEndian.reset();

        return pattern;
    }

    PatternData* Evaluator::evaluateArray(ASTNodeArrayVariableDecl *node) {

        if (auto offset = dynamic_cast<ASTNodeNumericExpression*>(node->getPlacementOffset()); offset != nullptr) {
            auto valueNode = evaluateMathematicalExpression(offset);
            SCOPE_EXIT( delete valueNode; );

            this->m_currOffset = std::visit([node, type = valueNode->getType()] (auto &&value) {
                if (Token::isFloatingPoint(type))
                    throwEvaluateError("placement offset must be an integer value", node->getLineNumber());
                return static_cast<u64>(value);
            }, valueNode->getValue());
        }

        auto startOffset = this->m_currOffset;

        ASTNodeIntegerLiteral *valueNode;

        if (auto sizeNumericExpression = dynamic_cast<ASTNodeNumericExpression*>(node->getSize()); sizeNumericExpression != nullptr)
            valueNode = evaluateMathematicalExpression(sizeNumericExpression);
        else
            throwEvaluateError("array size not a numeric expression", node->getLineNumber());

        SCOPE_EXIT( delete valueNode; );

        auto arraySize = std::visit([node, type = valueNode->getType()] (auto &&value) {
            if (Token::isFloatingPoint(type))
                throwEvaluateError("array size must be an integer value", node->getLineNumber());
            return static_cast<u64>(value);
        }, valueNode->getValue());

        if (auto typeDecl = dynamic_cast<ASTNodeTypeDecl*>(node->getType()); typeDecl != nullptr) {
            if (auto builtinType = dynamic_cast<ASTNodeBuiltinType*>(typeDecl->getType()); builtinType != nullptr) {
                if (builtinType->getType() == Token::ValueType::Padding) {
                    this->m_currOffset += arraySize;
                    return new PatternDataPadding(startOffset, arraySize);
                }
            }
        }

        std::vector<PatternData*> entries;
        std::optional<u32> color;
        for (s128 i = 0; i < arraySize; i++) {
            PatternData *entry;
            if (auto typeDecl = dynamic_cast<ASTNodeTypeDecl*>(node->getType()); typeDecl != nullptr)
                entry = this->evaluateType(typeDecl);
            else if (auto builtinTypeDecl = dynamic_cast<ASTNodeBuiltinType*>(node->getType()); builtinTypeDecl != nullptr) {
                entry = this->evaluateBuiltinType(builtinTypeDecl);
            }
            else
                throwEvaluateError("ASTNodeVariableDecl had an invalid type. This is a bug!", 1);

            entry->setVariableName(hex::format("[%llu]", (u64)i));
            entry->setEndian(this->getCurrentEndian());

            if (!color.has_value())
                color = entry->getColor();
            entry->setColor(color.value_or(0));

            entries.push_back(entry);

            if (this->m_currOffset >= this->m_provider->getActualSize())
                throwEvaluateError("array exceeds size of file", node->getLineNumber());
        }

        this->m_currEndian.reset();

        PatternData *pattern;
        if (entries.empty())
            pattern = new PatternDataPadding(startOffset, 0);
        else if (dynamic_cast<PatternDataCharacter*>(entries[0]))
            pattern = new PatternDataString(startOffset, (this->m_currOffset - startOffset), color.value_or(0));
        else
            pattern = new PatternDataArray(startOffset, (this->m_currOffset - startOffset), entries, color.value_or(0));

        pattern->setVariableName(node->getName().data());

        return pattern;
    }

    PatternData* Evaluator::evaluatePointer(ASTNodePointerVariableDecl *node) {
        s128 pointerOffset;
        if (auto offset = dynamic_cast<ASTNodeNumericExpression*>(node->getPlacementOffset()); offset != nullptr) {
            auto valueNode = evaluateMathematicalExpression(offset);
            SCOPE_EXIT( delete valueNode; );

            pointerOffset = std::visit([node, type = valueNode->getType()] (auto &&value) {
                if (Token::isFloatingPoint(type))
                    throwEvaluateError("pointer offset must be an integer value", node->getLineNumber());
                return static_cast<s128>(value);
            }, valueNode->getValue());
            this->m_currOffset = pointerOffset;
        } else {
            pointerOffset = this->m_currOffset;
        }

        PatternData *sizeType;
        if (auto builtinTypeNode = dynamic_cast<ASTNodeBuiltinType*>(node->getSizeType()); builtinTypeNode != nullptr) {
            sizeType = evaluateBuiltinType(builtinTypeNode);
        } else
            throwEvaluateError("Pointer size is not a builtin type", node->getLineNumber());

        size_t pointerSize = sizeType->getSize();
        delete sizeType;

        u128 pointedAtOffset = 0;
        this->m_provider->read(pointerOffset, &pointedAtOffset, pointerSize);

        this->m_currOffset = pointedAtOffset;
        auto pointedAt = evaluateType(dynamic_cast<ASTNodeTypeDecl*>(node->getType()));
        this->m_currOffset = pointerOffset + pointerSize;

        return new PatternDataPointer(pointerOffset, pointerSize, pointedAt);
    }

    std::optional<std::vector<PatternData*>> Evaluator::evaluate(const std::vector<ASTNode *> &ast) {

        std::vector<PatternData*> patterns;

        try {
            for (const auto& node : ast) {
                this->m_currEndian.reset();

                if (auto variableDeclNode = dynamic_cast<ASTNodeVariableDecl*>(node); variableDeclNode != nullptr) {
                    patterns.push_back(this->evaluateVariable(variableDeclNode));
                } else if (auto arrayDeclNode = dynamic_cast<ASTNodeArrayVariableDecl*>(node); arrayDeclNode != nullptr) {
                    patterns.push_back(this->evaluateArray(arrayDeclNode));
                } else if (auto pointerDeclNode = dynamic_cast<ASTNodePointerVariableDecl*>(node); pointerDeclNode != nullptr) {
                    patterns.push_back(this->evaluatePointer(pointerDeclNode));
                } else if (auto typeDeclNode = dynamic_cast<ASTNodeTypeDecl*>(node); typeDeclNode != nullptr) {
                    this->m_types[typeDeclNode->getName().data()] = typeDeclNode->getType();
                }

            }
        } catch (EvaluateError &e) {
            this->m_error = e;
            return { };
        }


        return patterns;
    }

}
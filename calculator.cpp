#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace llvm;
using namespace std; // Added to use standard library components without std:: prefix

// Lexer
// The lexer identifies tokens [0-255] for unknown characters, otherwise returns tokens for recognized items.
enum Token {
    tok_eof = -1,     // Token for end of input
    tok_error = -2,   // Token for errors
    tok_number = -4   // Token for numeric values
};

static double NumVal; // Stores the numeric value if tok_number is returned

/// gettok - Fetch the next token from standard input.
static int gettok() {
    static int LastChar = ' ';
    // Ignore whitespace characters.
    while (isspace(LastChar))
        LastChar = getchar();

    if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
        fprintf(stderr, "Only numeric literals and operators are permitted.\n");
        while (isalnum((LastChar = getchar())))
            return tok_error;
    }

    if (isdigit(LastChar) || LastChar == '.') { // Number: [0-9.]+
        string NumStr;
        do {
            NumStr += LastChar;
            LastChar = getchar();
        } while (isdigit(LastChar) || LastChar == '.');
        NumVal = strtod(NumStr.c_str(), nullptr);
        return tok_number;
    }

    // Check if the end of the file has been reached. Do not consume the EOF character.
    if (LastChar == EOF)
        return tok_eof;

    // Otherwise, return the character's ASCII value.
    int ThisChar = LastChar;
    LastChar = getchar();
    return ThisChar;
}

// Syntax Tree
namespace {

/// ExprAST - Base class for all expression nodes.
class ExprAST {
public:
    virtual ~ExprAST() = default;
    virtual Value *codegen() = 0;
};

/// NumberExprAST - Represents numeric literals like "1.0".
class NumberExprAST : public ExprAST {
public:
    double Val;
    NumberExprAST(double Val) : Val(Val) {}
    Value *codegen() override;
};

/// BinaryExprAST - Represents binary operators.
class BinaryExprAST : public ExprAST {
    char Op;
    unique_ptr<ExprAST> LHS, RHS;

public:
    BinaryExprAST(char Op, unique_ptr<ExprAST> LHS, unique_ptr<ExprAST> RHS)
        : Op(Op), LHS(move(LHS)), RHS(move(RHS)) {}
    Value *codegen() override;
};

/// PrototypeAST - Describes a function's "prototype", capturing its name and argument names, thereby defining the number of arguments. Useful for parsing input as an anonymous function.
class PrototypeAST {
    string Name;
    vector<string> Args;

public:
    PrototypeAST(const string &Name, vector<string> Args)
        : Name(Name), Args(move(Args)) {}

    Function *codegen();
    const string &getName() const { return Name; }
};

/// FunctionAST - Represents a function definition. Useful for parsing input as an anonymous function.
class FunctionAST {
    unique_ptr<PrototypeAST> Proto;
    unique_ptr<ExprAST> Body;

public:
    FunctionAST(unique_ptr<PrototypeAST> Proto, unique_ptr<ExprAST> Body)
        : Proto(move(Proto)), Body(move(Body)) {}

    Function *codegen();
};

// Parser

/// CurTok/getNextToken - Provides a simple token buffer. CurTok is the current token being examined by the parser. getNextToken reads another token from the lexer and updates CurTok with the result.
static int CurTok;
static int getNextToken() {
    return CurTok = gettok();
}

/// BinopPrecedence - Stores the precedence level for each defined binary operator.
static map<int, int> BinopPrecedence;

/// GetTokPrecedence - Retrieves the precedence of the current binary operator token.
static int GetTokPrecedence() {
    if (!isascii(CurTok))
        return -1;

    // Ensure it's a declared binary operator.
    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec <= 0)
        return -1;
    return TokPrec;
}

/// LogError* - Helper functions for handling errors.
unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}
unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
    LogError(Str);
    return nullptr;
}

static unique_ptr<ExprAST> ParseExpression();

/// numberexpr ::= number
static unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = make_unique<NumberExprAST>(NumVal);
    getNextToken(); // move past the number
    return move(Result);
}

/// parenexpr ::= '(' expression ')'
static unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken(); // consume '('
    auto V = ParseExpression();
    if (!V)
        return nullptr;

    if (CurTok != ')')
        return LogError("expected ')'");
    getNextToken(); // consume ')'
    return V;
}

/// primary
/// ::= identifierexpr
/// ::= numberexpr
/// ::= parenexpr
static unique_ptr<ExprAST> ParsePrimary() {
    switch (CurTok) {
    default:
        return LogError("unexpected token when expecting an expression");
    case tok_number:
        return ParseNumberExpr();
    case '(':
        return ParseParenExpr();
    }
}

/// binoprhs
/// ::= ('+' primary)*
static unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, unique_ptr<ExprAST> LHS) {
    // If this is a binary operator, find its precedence.
    while (true) {
        int TokPrec = GetTokPrecedence();

        // If this operator binds less tightly than the current one, we're done.
        if (TokPrec < ExprPrec)
            return LHS;

        int BinOp = CurTok;
        getNextToken(); // consume the operator

        // Parse the primary expression following the binary operator.
        auto RHS = ParsePrimary();
        if (!RHS)
            return nullptr;

        // If the current operator binds less tightly with RHS than the operator after RHS, let the pending operator take RHS as its LHS.
        int NextPrec = GetTokPrecedence();
        if (TokPrec < NextPrec) {
            RHS = ParseBinOpRHS(TokPrec + 1, move(RHS));
            if (!RHS)
                return nullptr;
        }

        // Combine LHS and RHS.
        LHS = make_unique<BinaryExprAST>(BinOp, move(LHS), move(RHS));
    }
}

/// expression
/// ::= primary binoprhs
static unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary();
    if (!LHS)
        return nullptr;

    return ParseBinOpRHS(0, move(LHS));
}

/// toplevelexpr ::= expression
static unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        // Create an anonymous prototype to hold our binary expressions.
        auto Proto = make_unique<PrototypeAST>("__anon_expr", vector<string>());
        return make_unique<FunctionAST>(move(Proto), move(E));
    }
    return nullptr;
}

// Code Generation
// These are the main static variables used for LLVM operations
static unique_ptr<LLVMContext> TheContext;
static unique_ptr<Module> TheModule;
static unique_ptr<IRBuilder<>> Builder;
static map<string, Value *> NamedValues;

Value *LogErrorV(const char *Str) {
    LogError(Str);
    return nullptr;
}

Value *NumberExprAST::codegen() {
    // All types will be of type double.
    return ConstantFP::get(*TheContext, APFloat(Val));
}

Value *BinaryExprAST::codegen() {
    Value *L = LHS->codegen();
    Value *R = RHS->codegen();
    if (!L || !R)
        return nullptr;

    switch (Op) {
    case '+':
        return Builder->CreateFAdd(L, R, "addtmp");
    case '-':
        return Builder->CreateFSub(L, R, "subtmp");
    case '*':
        return Builder->CreateFMul(L, R, "multmp");
    case '/':
        return Builder->CreateFDiv(L, R, "divtmp");
    case '<':
        L = Builder->CreateFCmpULT(L, R, "cmptmp");
        // Convert boolean 0/1 to double 0.0 or 1.0
        return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
    case '>':
        L = Builder->CreateFCmpUGT(L, R, "cmptmp");
        return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
    case '=':
        // Handle equality comparison (==)
        L = Builder->CreateFCmpUEQ(L, R, "cmptmp");
        return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
    default:
        return LogErrorV("invalid binary operator");
    }
}

Function *PrototypeAST::codegen() {
    // Create the function type: double(double,double) etc.
    vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
    FunctionType *FT = FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

    Function *F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

    // Assign names to all arguments.
    unsigned Idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]);

    return F;
}

Function *FunctionAST::codegen() {
    // First, check if there is an existing function from a previous 'extern' declaration.
    Function *TheFunction = TheModule->getFunction(Proto->getName());

    if (!TheFunction)
        TheFunction = Proto->codegen();
    if (!TheFunction)
        return nullptr;

    // Create a new basic block to start inserting into.
    BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    // Record the function arguments in the NamedValues map.
    NamedValues.clear();
    for (auto &Arg : TheFunction->args())
        NamedValues[string(Arg.getName())] = &Arg;

    if (Value *RetVal = Body->codegen()) {
        // Complete the function.
        Builder->CreateRet(RetVal);

        // Verify the generated code to ensure consistency.
        verifyFunction(*TheFunction);

        return TheFunction;
    }

    // Error reading body, remove function.
    TheFunction->eraseFromParent();
    return nullptr;
}

// Top-Level Parsing and JIT Driver

static void InitializeModule() {
    // Open a new context and module.
    TheContext = make_unique<LLVMContext>();
    TheModule = make_unique<Module>("jit", *TheContext);

    // Create a new builder for the module.
    Builder = make_unique<IRBuilder<>>(*TheContext);
}

static void HandleTopLevelExpression() {
    // Evaluate a top-level expression into an anonymous function.
    if (auto FnAST = ParseTopLevelExpr()) {
        if (auto *FnIR = FnAST->codegen()) {
            fprintf(stderr, "Generated IR and result:\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");

            // Remove the anonymous expression which will be every expression.
            FnIR->eraseFromParent();
        } else {
            // Skip token for error recovery.
            getNextToken();
        }
    }
}

/// top ::= expression | ';'
static void MainLoop() {
    while (true) {
        fprintf(stderr, "ready> ");
        switch (CurTok) {
        case tok_eof:
            return;
        case tok_error:
        case ';': // Ignore top-level semicolons.
            getNextToken();
            break;
        default:
            HandleTopLevelExpression();
            break;
        }
    }
}

//===----------------------------------------------------------------------===//
// Main driver code.
//===----------------------------------------------------------------------===//

int main() {
    // Set up standard binary operators.
    BinopPrecedence['<'] = 10;
    BinopPrecedence['>'] = 10;
    BinopPrecedence['='] = 10; // For checking if the two sides are equal
    BinopPrecedence['+'] = 20;
    BinopPrecedence['-'] = 20;
    BinopPrecedence['*'] = 40;
    BinopPrecedence['/'] = 40;

    // Initialize the first token.
    fprintf(stderr, "ready> ");
    getNextToken();

    // Create the module, which holds all the code.
    InitializeModule();

    // Run the main "interpreter loop" now.
    MainLoop();

    // Print out all of the generated code.
    TheModule->print(errs(), nullptr);

    return 0;
}
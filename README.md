# LLVM-Based Expression Evaluator

This project is a simple expression evaluator implemented using LLVM. It processes mathematical expressions and generates corresponding LLVM Intermediate Representation (IR) code.

## Overview

The code is designed to parse and evaluate mathematical expressions involving basic arithmetic operations. It uses LLVM's capabilities to generate and verify IR code for these expressions.

## Features

- **Lexer**: Tokenizes the input into numbers and operators.
- **Parser**: Converts tokens into an Abstract Syntax Tree (AST).
- **Code Generation**: Translates the AST into LLVM IR.
- **Execution**: Evaluates expressions and outputs the result.

## Components

- **Lexer**: Scans the input and identifies tokens such as numbers and operators.
- **Parser**: Builds an AST from the tokens, handling precedence and associativity of operators.
- **Code Generation**: Uses LLVM to generate IR code from the AST.
- **Main Loop**: Reads expressions, processes them, and outputs results.

## Usage

1. **Compile the Code**: Ensure you have LLVM installed. Compile the code using a C++ compiler with LLVM libraries linked.

2. **Run the Program**: Execute the compiled binary. The program will prompt with `ready>`.

3. **Input Expressions**: Enter mathematical expressions using numbers and operators like `+`, `-`, `*`, `/`, `<`, `>`, and `=`.

4. **View Results**: The program will output the generated IR and the result of the expression.

## Example


Commands should end with a semicolon (;) to indicate the end of an expression. For example:

2 + 25 * 2 - 8;

## Dependencies

- LLVM library
- Standard C++ libraries

## Installation

1. Install LLVM on your system.
2. Clone this repository.
3. Compile the code using a C++ compiler with LLVM support.

## Build and Run

To build the calculator, use the following command:

```bash
clang++ -g calculator.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -O3 -o calculator

```

To run the calculator, execute:
```bash
./calculator
```


## Contributing

Contributions are welcome! Please fork the repository and submit a pull request with your changes.

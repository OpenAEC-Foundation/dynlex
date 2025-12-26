# 3BX

A human and AI readable programming language that compiles to machine code using LLVM.

## Features

- **Multi-paradigm**: Supports imperative, functional, and object-oriented programming
- **Skript-like syntax**: Natural language-style syntax designed for readability
- **User-definable patterns**: Create custom syntax patterns for domain-specific needs
- **LLVM backend**: Compiles to optimized native machine code

## Dependencies

### Ubuntu/Debian

```bash
sudo apt update && sudo apt install -y llvm llvm-dev clang cmake build-essential libz-dev libzstd-dev
```

### Fedora

```bash
sudo dnf install llvm llvm-devel clang cmake gcc-c++ zlib-devel libzstd-devel
```

### Arch Linux

```bash
sudo pacman -S llvm clang cmake base-devel
```

### macOS

```bash
brew install llvm cmake
```

## Building

```bash
mkdir build && cd build
cmake ..
make
```

## Usage

```bash
./3bx <source_file.3bx>
```

## Project Structure

```
3BX/
├── src/           # Source files
│   ├── lexer/     # Tokenization
│   ├── parser/    # Syntax analysis
│   ├── ast/       # Abstract syntax tree
│   ├── semantic/  # Type checking & validation
│   └── codegen/   # LLVM IR generation
├── include/       # Header files
├── tests/         # Test suite
├── examples/      # Example 3BX programs
└── docs/          # Documentation
```

## License

TBD

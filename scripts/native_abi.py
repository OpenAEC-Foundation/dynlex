"""Symbol discovery and C assembler labels for native ABI test callers."""

import re


C_SYMBOL_MACROS = """\
#define DYNLEX_STRINGIFY_IMPL(value) #value
#define DYNLEX_STRINGIFY(value) DYNLEX_STRINGIFY_IMPL(value)
#define DYNLEX_SYMBOL(name) DYNLEX_STRINGIFY(__USER_LABEL_PREFIX__) name
"""


def exposed_symbol(llvm_ir: str, function_name: str) -> str:
    # getPatternFunctionName encodes the first parameter's opening brace as 3.
    # Include it so "exchange boolean" cannot select "exchange boolean pointer".
    matches = re.findall(
        rf"^define .* @({re.escape(function_name.replace(' ', '_'))}_3[^ (]*_callable[^ (]*)\(",
        llvm_ir,
        re.MULTILINE,
    )
    if len(matches) != 1:
        raise RuntimeError(f"expected one exposed function {function_name!r}, found {matches}")
    return matches[0]


def abi_symbols(llvm_ir: str) -> list[str]:
    return re.findall(r"^define .* @(abi_[A-Za-z0-9_]+)\(", llvm_ir, re.MULTILINE)


def one_symbol(symbols: list[str], prefix: str) -> str:
    matches = [name for name in symbols if name.startswith(prefix)]
    if len(matches) != 1:
        raise RuntimeError(f"expected one ABI symbol with prefix {prefix!r}, found {matches}")
    return matches[0]

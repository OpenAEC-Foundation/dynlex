#!/usr/bin/env python3
"""
Code conventions checker for DynLex C++ codebase.
Checks:
1. Only one struct/class/enum per header file, none in source files
2. File naming: camelCase
3. Class/struct/enum naming: PascalCase
4. Function naming: camelCase
"""

import json
import os
import re
import sys
from pathlib import Path

CACHE_FILE = "build/.conventions_cache.json"

# Regex patterns
STRUCT_PATTERN = re.compile(r'\bstruct\s+([A-Za-z_][A-Za-z0-9_]*)\s*[{;:]')
CLASS_PATTERN = re.compile(r'\bclass\s+([A-Za-z_][A-Za-z0-9_]*)\s*[{;:]')
ENUM_PATTERN = re.compile(r'\benum\s+(?:class\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*[{;:]')

# Variable patterns - only match definite variable declarations
# Pattern 1: auto varName (with optional const, &, *)
AUTO_VAR_PATTERN = re.compile(r'\b(?:const\s+)?auto\s*[&*]?\s*([A-Za-z_][A-Za-z0-9_]*)\s*[=;,)\]]')
# Pattern 2: const Type& varName or const Type* varName
CONST_REF_VAR_PATTERN = re.compile(r'\bconst\s+[A-Za-z_][A-Za-z0-9_:]*(?:<[^>]*>)?\s*[&*]\s*([A-Za-z_][A-Za-z0-9_]*)\s*[=;,)\]]')
# Pattern 3: Basic types: int, bool, float, double, char, size_t, etc.
BASIC_TYPES = r'(?:int|bool|float|double|char|size_t|ssize_t|uint\d+_t|int\d+_t|unsigned|signed|long|short|void)\b'
BASIC_TYPE_VAR_PATTERN = re.compile(
    r'\b(?:const\s+)?(?:unsigned\s+|signed\s+)?'  # Optional const, unsigned, signed
    + BASIC_TYPES +
    r'\s*[&*]?\s*([A-Za-z_][A-Za-z0-9_]*)\s*[=;,)\[\]]'  # Variable name followed by =, ;, ,, ), or [
)
# Match function definitions - requires return type OR ClassName:: prefix
# Matches: return_type [ClassName::]function_name(params) [const] [override] [noexcept] {
# Also matches: ClassName::ClassName(params) { (constructors)
FUNCTION_PATTERN = re.compile(
    r'^\s*'  # Start of line with optional whitespace
    r'(?:'
        r'(?:[A-Za-z_][A-Za-z0-9_<>:,\s*&]*\s+)'  # Return type (required)
        r'(?:[A-Za-z_][A-Za-z0-9_]*::)*'  # Optional ClassName:: prefix
    r'|'
        r'(?:[A-Za-z_][A-Za-z0-9_]*::)+'  # OR: ClassName:: prefix (for constructors)
    r')'
    r'([A-Za-z_][A-Za-z0-9_]*)\s*'  # Function name (captured)
    r'\([^)]*\)\s*'  # Parameters
    r'(?:const\s*)?(?:override\s*)?(?:noexcept(?:\([^)]*\))?\s*)?'  # Optional qualifiers
    r'\{',  # Opening brace
    re.MULTILINE
)

def is_camel_case(name):
    """Check if name is camelCase (starts with lowercase, no underscores except leading)."""
    if not name:
        return False
    # Remove file extension
    name = Path(name).stem
    # camelCase: starts with lowercase, no underscores
    if name[0].isupper():
        return False
    if '_' in name:
        return False
    return True

def is_pascal_case(name):
    """Check if name is PascalCase (starts with uppercase, no underscores)."""
    if not name:
        return False
    if name[0].islower():
        return False
    if '_' in name:
        return False
    return True

def get_type_nesting_depth(content, pos):
    """Calculate type nesting depth at position, ignoring namespace braces."""
    # Find all namespace positions
    namespace_pattern = re.compile(r'\bnamespace\s+[A-Za-z_][A-Za-z0-9_]*\s*\{')
    namespace_brace_positions = set()
    for match in namespace_pattern.finditer(content):
        # Find the opening brace position
        brace_pos = match.end() - 1
        namespace_brace_positions.add(brace_pos)

    # Track depth, not counting namespace braces
    depth = 0
    brace_stack = []  # Track whether each brace is a namespace brace
    for i in range(pos):
        if content[i] == '{':
            is_namespace = i in namespace_brace_positions
            brace_stack.append(is_namespace)
            if not is_namespace:
                depth += 1
        elif content[i] == '}':
            if brace_stack:
                was_namespace = brace_stack.pop()
                if not was_namespace:
                    depth -= 1
    return depth

def find_definitions(content, pattern):
    """Find all top-level type definitions matching pattern, excluding forward declarations and nested types."""
    matches = []
    for match in pattern.finditer(content):
        name = match.group(1)
        # Check if it's a forward declaration (ends with ;) or actual definition (ends with { or :)
        end_char = match.group(0)[-1]
        if end_char in '{:':
            # Only count top-level definitions (type nesting depth 0)
            if get_type_nesting_depth(content, match.start()) == 0:
                matches.append(name)
    return matches

def check_file(filepath):
    """Check a single file for convention violations."""
    violations = []

    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
    except Exception as e:
        return [f"Could not read file: {e}"]

    is_header = filepath.endswith('.hpp') or filepath.endswith('.h')
    is_source = filepath.endswith('.cpp') or filepath.endswith('.c')
    filename = os.path.basename(filepath)

    # Check file length (max 1000 lines)
    line_count = content.count('\n') + 1
    if line_count > 1000:
        violations.append(f"File has {line_count} lines (max 1000 allowed)")

    # Check file naming (camelCase)
    if not is_camel_case(filename):
        violations.append(f"File name '{filename}' should be camelCase")

    # Find structs, classes, enums
    structs = find_definitions(content, STRUCT_PATTERN)
    classes = find_definitions(content, CLASS_PATTERN)
    enums = find_definitions(content, ENUM_PATTERN)

    # Deduplicate while preserving order
    seen = set()
    all_types = []
    for t in structs + classes + enums:
        if t not in seen:
            seen.add(t)
            all_types.append(t)

    # Check: only one type per header, none in source
    if is_header:
        if len(all_types) > 1:
            violations.append(f"Header has {len(all_types)} types (only 1 allowed): {', '.join(all_types)}")
    elif is_source:
        if len(all_types) > 0:
            violations.append(f"Source file has {len(all_types)} type definitions (none allowed): {', '.join(all_types)}")

    # Check PascalCase for types
    for name in structs:
        if not is_pascal_case(name):
            violations.append(f"Struct '{name}' should be PascalCase")
    for name in classes:
        if not is_pascal_case(name):
            violations.append(f"Class '{name}' should be PascalCase")
    for name in enums:
        if not is_pascal_case(name):
            violations.append(f"Enum '{name}' should be PascalCase")

    # Check variable naming (camelCase, no single-letter names except in specific contexts)
    seen_vars = set()
    for pattern in [AUTO_VAR_PATTERN, CONST_REF_VAR_PATTERN, BASIC_TYPE_VAR_PATTERN]:
        for match in pattern.finditer(content):
            var_name = match.group(1)
            # Skip if already seen
            if var_name in seen_vars:
                continue
            seen_vars.add(var_name)
            # Check for single-letter names (not allowed per conventions)
            if len(var_name) == 1:
                violations.append(f"Variable '{var_name}' is too short (use descriptive names)")
                continue
            if not is_camel_case(var_name):
                violations.append(f"Variable '{var_name}' should be camelCase")

    # Check function naming (camelCase)
    for match in FUNCTION_PATTERN.finditer(content):
        func_name = match.group(1)
        # Skip constructors/destructors (same name as class, or starts with ~)
        if func_name in all_types or func_name.startswith('~'):
            continue
        # Skip constructors defined in .cpp files (ClassName::ClassName pattern)
        match_text = match.group(0)
        if f'{func_name}::{func_name}(' in match_text or f'{func_name}::{func_name} (' in match_text:
            continue
        # Skip main
        if func_name == 'main':
            continue
        # Skip operator overloads
        if func_name.startswith('operator'):
            continue
        if not is_camel_case(func_name):
            violations.append(f"Function '{func_name}' should be camelCase")

    return violations

def load_cache():
    if os.path.isfile(CACHE_FILE):
        with open(CACHE_FILE, 'r') as f:
            return json.load(f)
    return {}

def save_cache(cache):
    os.makedirs(os.path.dirname(CACHE_FILE), exist_ok=True)
    with open(CACHE_FILE, 'w') as f:
        json.dump(cache, f)

def main():
    print("Checking code conventions...")

    if len(sys.argv) < 2:
        print("Usage: check_conventions.py file1 [file2 ...]")
        return 1

    cache = load_cache()
    all_violations = {}

    for filepath in sys.argv[1:]:
        if os.path.isfile(filepath) and filepath.endswith(('.cpp', '.hpp', '.c', '.h')):
            mtime = os.path.getmtime(filepath)
            cached = cache.get(filepath)

            if cached and cached.get('mtime') == mtime:
                violations = cached.get('violations', [])
            else:
                violations = check_file(filepath)
                cache[filepath] = {'mtime': mtime, 'violations': violations}

            if violations:
                all_violations[filepath] = violations

    save_cache(cache)

    # Report
    if all_violations:
        print("Convention violations found:\n")
        for filepath, violations in sorted(all_violations.items()):
            print(f"{filepath}:")
            for v in violations:
                print(f"  - {v}")
            print()
        print(f"Total: {sum(len(v) for v in all_violations.values())} violations in {len(all_violations)} files")
        print("read CONVENTIONS.md if you haven't yet!")
        return 1
    else:
        print("No convention violations found (doesn't mean there are none. keep complexity low!)")
        return 0

if __name__ == '__main__':
    sys.exit(main())

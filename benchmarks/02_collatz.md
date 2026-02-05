# Benchmark: Collatz Conjecture

This benchmark computes the total number of Collatz steps for all numbers from 1 to 1,000,000. The Collatz sequence is data-dependent and cannot be optimized away by the compiler.

## Results

| Language | Optimization | Execution Time | vs Python |
|----------|-------------|----------------|-----------|
| Python | - | 11.363s | 1x |
| **3BX** | O0 | 0.969s | **12x faster** |
| C++ | O0 | 0.338s | 34x faster |
| **3BX** | O3 | 0.221s | **51x faster** |
| C++ | O3 | 0.172s | 66x faster |

All outputs: `131434272` (correct)

## Analysis

- **3BX vs Python**: 12-51x faster depending on optimization
- **3BX O3 vs C++ O3**: 1.3x slower (due to function call overhead for `set var to val` pattern)
- **3BX O0 vs C++ O0**: 2.9x slower (function calls not yet inlined)

The gap between 3BX and C++ will narrow as more patterns become macros (inlined at compile time).

## Source Code

### 3BX (collatz.3bx)

```
macro effect return value:
    replacement:
        @intrinsic("return", value)

effect set var to val:
    execute:
        @intrinsic("store", var, val)

effect print msg as line:
    execute:
        @intrinsic("call", "libc", "printf", "%ld\n", msg)

expression left < right:
    get:
        return @intrinsic("less than", left, right)

expression left > right:
    get:
        return @intrinsic("greater than", left, right)

expression left equals right:
    get:
        return @intrinsic("equal", left, right)

macro expression left + right:
    replacement:
        @intrinsic("add", left, right)

macro expression left * right:
    replacement:
        @intrinsic("multiply", left, right)

macro expression left / right:
    replacement:
        @intrinsic("divide", left, right)

macro expression left mod right:
    replacement:
        @intrinsic("modulo", left, right)

macro section loop while condition:
    replacement:
        @intrinsic("loop while", condition)

macro section if condition:
    replacement:
        @intrinsic("if", condition)

set total_steps to 0
set n to 1

loop while n < 1000000:
    set num to n
    loop while num > 1:
        set total_steps to total_steps + 1
        set remainder to num mod 2
        if remainder equals 0:
            set num to num / 2
        if remainder > 0:
            set temp to num * 3
            set num to temp + 1
    set n to n + 1

print total_steps as line
```

### C++ (collatz.cpp)

```cpp
#include <cstdio>

int main() {
    long total_steps = 0;
    for (long n = 1; n < 1000000; n++) {
        long num = n;
        while (num > 1) {
            total_steps++;
            if (num % 2 == 0) {
                num = num / 2;
            } else {
                num = num * 3 + 1;
            }
        }
    }
    printf("%ld\n", total_steps);
    return 0;
}
```

### Python (collatz.py)

```python
total_steps = 0
n = 1
while n < 1000000:
    num = n
    while num > 1:
        total_steps += 1
        if num % 2 == 0:
            num = num // 2
        else:
            num = num * 3 + 1
    n += 1
print(total_steps)
```

## How to Run

```bash
# 3BX
./build/3bx collatz.3bx -O3 -o collatz_3bx
time ./collatz_3bx

# C++
g++ -O3 collatz.cpp -o collatz_cpp
time ./collatz_cpp

# Python
time python3 collatz.py
```

## Notes

- The 3BX code uses `set temp to num * 3` then `set num to temp + 1` due to a current expression precedence parsing issue with `num * 3 + 1`
- The `set var to val` pattern is not yet a macro, causing function call overhead; making it a macro would improve performance

# Benchmark: Collatz Conjecture

This benchmark computes the total number of Collatz steps for all numbers from 1 to 1,000,000. The Collatz sequence is data-dependent and cannot be optimized away by the compiler.

## Results

Measured on July 31, 2026:

| Implementation | Compiler settings | Median | Mean | Standard deviation |
|----------------|-------------------|-------:|-----:|-------------------:|
| C++ | Clang `-O3`, native CPU tuning, LTO | 108.595 ms | 108.942 ms | 0.991 ms |
| DynLex | `-O3`, native CPU tuning | 113.870 ms | 114.054 ms | 0.744 ms |

DynLex took `1.048x` the C++ execution time. Every tested binary printed
`131434272`.

### Method

- CPU: Intel Core i5-9400, six physical cores, maximum 4.1 GHz
- OS: Linux 7.0.0-28-generic
- DynLex: 0.0.1 using the repository's LLVM 23 toolchain
- C++: Clang 20.1.2 with GNU gold for LTO
- CPU frequency governor: `powersave`, with turbo enabled
- 10 warm-up runs and 50 measured runs per executable
- Runs alternated between the executables and were pinned to CPU 0
- Wall time included process startup; benchmark output was discarded while timing

The flag search also covered `-Ofast`, fast-math, explicit loop unrolling,
explicit loop and SLP vectorization, disabling unrolling, GCC 13.3, and GCC LTO.
Clang profile-guided optimization trained on this workload was also slower than
Clang LTO alone. Those settings did not improve this integer, data-dependent
workload. Clang LTO was the only additional setting with a repeatable gain, so
it is included in the C++ result.

## Source Code

The executable benchmark sources are [`collatz.dl`](./collatz.dl),
[`collatz.cpp`](./collatz.cpp), and [`collatz.py`](./collatz.py). The DynLex and
C++ programs both use signed 64-bit state and express each Collatz transition as
the same remainder calculation and conditional selection.

## How to Run

```bash
# DynLex
./build/dynlex tests/benchmarks/collatz.dl \
    -O3 -march=native -mtune=native -o collatz_dynlex
taskset -c 0 ./collatz_dynlex

# C++
clang++-20 -std=c++23 tests/benchmarks/collatz.cpp \
    -O3 -march=native -mtune=native -flto -fuse-ld=gold \
    -fno-exceptions -fno-rtti -o collatz_cpp
taskset -c 0 ./collatz_cpp

# Python
time python3 tests/benchmarks/collatz.py
```

# Task 3.6 — Performance benchmarks
# These are standalone executables, not CTest tests.

add_executable(benchmark_persist_vs_std
    benchmark_persist_vs_std.cpp
)
target_link_libraries(benchmark_persist_vs_std PRIVATE bindiff)

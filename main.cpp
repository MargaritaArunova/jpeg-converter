#include "jpeg/benchmark.h"
#include <thread>
#include <iostream>
#include <cstdlib>

int main(int argc, char* argv[]) {
    // Allow overriding max thread count from command line: ./jpeg_converter 8
    int max_threads = static_cast<int>(std::thread::hardware_concurrency());
    if (max_threads < 2) max_threads = 2;

    if (argc > 1) {
        int n = std::atoi(argv[1]);
        if (n > 0) max_threads = n;
    }

    // Round down to nearest power of 2 so the benchmark doubles cleanly.
    int pow2 = 1;
    while (pow2 * 2 <= max_threads) pow2 *= 2;
    max_threads = pow2;

    jpeg::run_all_benchmarks(max_threads);
    return 0;
}

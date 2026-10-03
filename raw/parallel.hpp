#pragma once
// Row-parallel loop. Each row is written by exactly one thread and every pixel
// depends only on read-only inputs, so the output is byte-identical for any
// thread count. Builds without thread support (plain WebAssembly) run serially.
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#define RAW_NATIVE_NO_THREADS 1
#endif
#ifndef RAW_NATIVE_NO_THREADS
#include <thread>
#include <vector>
#endif
namespace raw {
template<class F> void parallelRows(int rows, int threads, F&& rowFn){
#ifndef RAW_NATIVE_NO_THREADS
    if (threads > 1 && rows > 1){
        if (threads > rows) threads = rows;
        std::vector<std::thread> pool;
        pool.reserve((size_t)threads);
        for (int t = 0; t < threads; ++t){
            pool.emplace_back([&, t]{
                for (int y = t; y < rows; y += threads) rowFn(y);   // interleaved rows balance load
            });
        }
        for (auto& th : pool) th.join();
        return;
    }
#else
    (void)threads;
#endif
    for (int y = 0; y < rows; ++y) rowFn(y);
}
// True when this build can run more than one render thread.
constexpr bool threadsAvailable(){
#ifdef RAW_NATIVE_NO_THREADS
    return false;
#else
    return true;
#endif
}
}

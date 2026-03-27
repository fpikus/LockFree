// Code examples for the presentation by Fedor G. Pikus on
// Modern Lock-Free Programming.
//
// MIT License
//
// Copyright (c) 2026 Fedor G. Pikus, fpikus@gmail.com
//  https://github.com/fpikus/LockFree
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <atomic>

#include "benchmark/benchmark.h"

#define REPEAT2(x) x x
#define REPEAT4(x) REPEAT2(x) REPEAT2(x)
#define REPEAT8(x) REPEAT4(x) REPEAT4(x)
#define REPEAT16(x) REPEAT8(x) REPEAT8(x)
#define REPEAT32(x) REPEAT16(x) REPEAT16(x)
#define REPEAT(x) REPEAT32(x)

std::atomic<unsigned long> x(0);
void BM_sharing(benchmark::State& state) {
    for (auto _ : state) {
        REPEAT(benchmark::DoNotOptimize(++x););
    }
    state.SetItemsProcessed(32*state.iterations());
}

std::atomic<unsigned long> sum(0);
unsigned long* a = new unsigned long[1024];
void BM_false_sharing(benchmark::State& state) {
    auto& x = a[state.thread_index()];
    x = 0;
    for (auto _ : state) {
        REPEAT(benchmark::DoNotOptimize(++x););
    }
    sum += x;
    state.SetItemsProcessed(32*state.iterations());
}

struct aligned_uint64_t {
    alignas(std::hardware_destructive_interference_size) unsigned long i {0};
};
aligned_uint64_t* aa = new aligned_uint64_t[1024];
void BM_no_sharing(benchmark::State& state) {
    auto& x = aa[state.thread_index()].i;
    x = 0;
    for (auto _ : state) {
        REPEAT(benchmark::DoNotOptimize(++x););
    }
    sum += x;
    state.SetItemsProcessed(32*state.iterations());
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);
#define ARG \
    ->ThreadRange(1, numcpu) \
    ->UseRealTime()

BENCHMARK(BM_sharing) ARG;
BENCHMARK(BM_false_sharing) ARG;
BENCHMARK(BM_no_sharing) ARG;

BENCHMARK_MAIN();

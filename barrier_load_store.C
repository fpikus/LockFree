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

void BM_relaxed(benchmark::State& state) {
    const bool read = state.thread_index() & 1;
    for (auto _ : state) {
        if (read) {
        REPEAT(benchmark::DoNotOptimize(x.load(std::memory_order_relaxed)););
        } else {
        REPEAT(x.store(1, std::memory_order_relaxed););
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(32*state.iterations());
}

void BM_acq_rel(benchmark::State& state) {
    const bool read = state.thread_index() & 1;
    for (auto _ : state) {
        if (read) {
        REPEAT(benchmark::DoNotOptimize(x.load(std::memory_order_acquire)););
        } else {
        REPEAT(x.store(1, std::memory_order_release););
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(32*state.iterations());
}

void BM_seq_cst(benchmark::State& state) {
    const bool read = state.thread_index() & 1;
    for (auto _ : state) {
        if (read) {
        REPEAT(benchmark::DoNotOptimize(x.load(std::memory_order_seq_cst)););
        } else {
        REPEAT(x.store(1, std::memory_order_seq_cst););
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(32*state.iterations());
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);
#define ARG(N) \
    ->Threads(N) \
    ->UseRealTime()

BENCHMARK(BM_relaxed) ARG(2);
BENCHMARK(BM_acq_rel) ARG(2);
BENCHMARK(BM_seq_cst) ARG(2);

BENCHMARK(BM_relaxed) ARG(numcpu);
BENCHMARK(BM_acq_rel) ARG(numcpu);
BENCHMARK(BM_seq_cst) ARG(numcpu);

BENCHMARK(BM_relaxed) ARG(numcpu/2);
BENCHMARK(BM_acq_rel) ARG(numcpu/2);
BENCHMARK(BM_seq_cst) ARG(numcpu/2);

BENCHMARK_MAIN();

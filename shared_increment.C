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

#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <iostream>
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
inline void spinlock_pause() { _mm_pause(); }
#elif defined(__aarch64__) || defined(_M_ARM64)
inline void spinlock_pause() { __asm__ volatile("yield" ::: "memory"); }
#else
#error "Unsupported architecture"
#endif

#include "benchmark/benchmark.h"

std::atomic<unsigned long> xa(0);

void BM_atomic(benchmark::State& state) {
  std::atomic<unsigned long>& x = xa;
  for (auto _ : state) {
    benchmark::DoNotOptimize(x.fetch_add(1, std::memory_order_relaxed));
  }
  state.SetItemsProcessed(state.iterations());
}

unsigned long x = 0;
std::mutex M;

void BM_mutex(benchmark::State& state) {
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    std::lock_guard L(M);
    benchmark::DoNotOptimize(++x);
  }
  state.SetItemsProcessed(state.iterations());
}

class BasicSpinlock {
  public:
  BasicSpinlock() = default;
  BasicSpinlock(const BasicSpinlock&) = delete;
  BasicSpinlock operator=(const BasicSpinlock&) = delete;
  void lock() { while (flag_.exchange(1, std::memory_order_acquire)) {} }
  void unlock() { flag_.store(0, std::memory_order_release); }
  private:
  std::atomic<unsigned int> flag_ {};
};

BasicSpinlock BS;

void BM_basic_spinlock(benchmark::State& state) {
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    std::lock_guard L(BS);
    benchmark::DoNotOptimize(++x);
  }
  state.SetItemsProcessed(state.iterations());
}

class LoadSpinlock {
  public:
  LoadSpinlock() = default;
  LoadSpinlock(const LoadSpinlock&) = delete;
  LoadSpinlock operator=(const LoadSpinlock&) = delete;
  void lock() { while (flag_.load(std::memory_order_relaxed) || flag_.exchange(1, std::memory_order_acquire)) {} }
  void unlock() { flag_.store(0, std::memory_order_release); }
  private:
  std::atomic<unsigned int> flag_ {};
};

LoadSpinlock LS;

void BM_load_spinlock(benchmark::State& state) {
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    std::lock_guard L(LS);
    benchmark::DoNotOptimize(++x);
  }
  state.SetItemsProcessed(state.iterations());
}

class SpinlockPause {
  public:
  SpinlockPause() = default;
  SpinlockPause(const SpinlockPause&) = delete;
  SpinlockPause operator=(const SpinlockPause&) = delete;
  void lock() { 
      while (flag_.load(std::memory_order_relaxed) || flag_.exchange(1, std::memory_order_acquire)) {
      spinlock_pause();
    }
  }
  void unlock() { flag_.store(0, std::memory_order_release); }
  private:
  std::atomic<unsigned int> flag_ {};
};

SpinlockPause SP;

void BM_spinlock_pause(benchmark::State& state) {
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    std::lock_guard L(SP);
    benchmark::DoNotOptimize(++x);
  }
  state.SetItemsProcessed(state.iterations());
}

class SpinlockYield {
  public:
  SpinlockYield() = default;
  SpinlockYield(const SpinlockYield&) = delete;
  SpinlockYield operator=(const SpinlockYield&) = delete;
  void lock() { 
      while (flag_.load(std::memory_order_relaxed) || flag_.exchange(1, std::memory_order_acquire)) {
      sched_yield();
    }
  }
  void unlock() { flag_.store(0, std::memory_order_release); }
  private:
  std::atomic<unsigned int> flag_ {};
};

SpinlockYield SY;

void BM_spinlock_yield(benchmark::State& state) {
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    std::lock_guard L(SY);
    benchmark::DoNotOptimize(++x);
  }
  state.SetItemsProcessed(state.iterations());
}

class Spinlock {
  public:
  Spinlock() = default;
  Spinlock(const Spinlock&) = delete;
  Spinlock operator=(const Spinlock&) = delete;
  void lock() {
    static const timespec ns = { 0, 1 };
    for (int i = 0; flag_.load(std::memory_order_relaxed) || flag_.exchange(1, std::memory_order_acquire); ++i) {
      if (i == 8) {
        i = 0;
        nanosleep(&ns, nullptr);
      }
    }
  }
  void unlock() { flag_.store(0, std::memory_order_release); }
  private:
  std::atomic<unsigned int> flag_ {};
};

Spinlock S;

void BM_spinlock(benchmark::State& state) {
  if (state.thread_index() == 0) x = 0;
  for (auto _ : state) {
    std::lock_guard L(S);
    benchmark::DoNotOptimize(++x);
  }
  state.SetItemsProcessed(state.iterations());
}

static const long numcpu = sysconf(_SC_NPROCESSORS_CONF);

#define ARGS \
  ->ThreadRange(1, numcpu) \
  ->UseRealTime()

BENCHMARK(BM_atomic) ARGS;
BENCHMARK(BM_mutex) ARGS;
BENCHMARK(BM_basic_spinlock) ARGS;
BENCHMARK(BM_load_spinlock) ARGS;
BENCHMARK(BM_spinlock_pause) ARGS;
BENCHMARK(BM_spinlock_yield) ARGS;
BENCHMARK(BM_spinlock) ARGS;

BENCHMARK_MAIN();

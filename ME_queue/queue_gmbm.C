#include "include/atomic_queue/atomic_queue.h"

#include <cstdint>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#else
  #include <unistd.h>
#endif

#include <benchmark/benchmark.h>

#ifndef CHECK
#define CHECK(x) if (!(x)) std::abort();
#endif
#ifndef CHECK_EQ
#define CHECK_EQ(a, b) if ((a) != (b)) std::abort();
#endif

// ---------------------------------------------------------------------------
// AddressUtils::prev_power_of_2 from address-utils.h
// ---------------------------------------------------------------------------
namespace AddressUtils {

#define b2_(x)   (   (x) | (   (x) >> 1) )
#define b4_(x)   ( b2_(x) | ( b2_(x) >> 2) )
#define b8_(x)   ( b4_(x) | ( b4_(x) >> 4) )
#define b16_(x)  ( b8_(x) | ( b8_(x) >> 8) )
#define b32_(x)  (b16_(x) | (b16_(x) >>16) )
#define b64_(x)  (b32_(x) | (b32_(x) >>32) )
template <typename T, size_t S> struct PrevPower2Helper;
template <typename T> struct PrevPower2Helper<T, 4> {
    static T Compute(T x) { return ~(b32_(x) >> 1) & x; }
};
template <typename T> struct PrevPower2Helper<T, 8> {
    static T Compute(T x) { return ~(b64_(x) >> 1) & x; }
};
template <typename T> inline T prev_power_of_2(T x) { return PrevPower2Helper<T, sizeof(T)>::Compute(x); }
#undef b2_
#undef b4_
#undef b8_
#undef b16_
#undef b32_
#undef b64_

} // namespace AddressUtils

using meq_t = atomic_queue::AtomicQueueB<int*>;
meq_t* meq = {};

void BM_me(benchmark::State& state) {
    (void)state;
    const size_t N = AddressUtils::prev_power_of_2(state.range(0));
    const size_t mask = N - 1;
    if (state.thread_index() == 0) {
        meq = new meq_t(static_cast<unsigned>(N));
    }
    std::vector<int> v(N);
    for (size_t i = 0; i != N; ++i) v[i] = i;
    const bool producer = state.thread_index() & 1;

    size_t p = 0, c = 0, pmiss = 0, cmiss = 0;
    for (auto _ : state) {
        if (producer) {
            if (meq->try_push(&v[p & mask])) ++p;
            else ++pmiss;
        } else {
            int* ptr;
            if (meq->try_pop(ptr)) ++c;
            else ++cmiss;
            benchmark::DoNotOptimize(ptr);
        }
    }
    state.SetItemsProcessed(p + c);
    state.counters["push"] = benchmark::Counter(static_cast<double>(p), benchmark::Counter::kIsRate);
    state.counters["push_fail"] = benchmark::Counter(static_cast<double>(pmiss), benchmark::Counter::kIsRate);
    state.counters["pop"] = benchmark::Counter(static_cast<double>(c), benchmark::Counter::kIsRate);
    state.counters["pop_fail"] = benchmark::Counter(static_cast<double>(cmiss), benchmark::Counter::kIsRate);
    if (state.thread_index() == 0) {
        size_t rem = 0;
        int* ptr;
        while (meq->try_pop(ptr)) ++rem;
        state.counters["rem%"] = benchmark::Counter(100.0 * rem / N);
        delete meq; meq = nullptr;
    }
}

static size_t get_thread_count() {
#ifdef _WIN32
    SYSTEM_INFO si;
    ::GetSystemInfo(&si);
    return static_cast<size_t>(si.dwNumberOfProcessors);
#else
    return static_cast<size_t>(sysconf(_SC_NPROCESSORS_CONF));
#endif
}
static const size_t thread_count = get_thread_count();

#define ARGS \
  ->UseRealTime() \
  ->ThreadRange(2, thread_count) \
  ->RangeMultiplier(2)->Range(1UL << 16, 1UL << 16)  

BENCHMARK(BM_me) ARGS;

BENCHMARK_MAIN();

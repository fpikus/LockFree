// Custom throughput benchmark for atomic_queue::AtomicQueueB (ME).

#include "include/atomic_queue/atomic_queue.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <latch>
#include <new>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <timeapi.h>
  #pragma comment(lib, "winmm.lib")
  namespace {
    struct TimerResolutionGuard {
        TimerResolutionGuard()  { ::timeBeginPeriod(1); }
        ~TimerResolutionGuard() { ::timeEndPeriod(1);   }
    };
    static TimerResolutionGuard g_timer_resolution_guard;
  }
#else
  #include <unistd.h>
#endif

namespace {

constexpr size_t QSZ = 1UL << 16;
using Q = atomic_queue::AtomicQueueB<int*>;

size_t hw_thread_count() {
#ifdef _WIN32
    SYSTEM_INFO si;
    ::GetSystemInfo(&si);
    return static_cast<size_t>(si.dwNumberOfProcessors);
#else
    return static_cast<size_t>(sysconf(_SC_NPROCESSORS_CONF));
#endif
}

struct alignas(64) ThreadStats {
    size_t p = 0, c = 0, pmiss = 0, cmiss = 0;
    char   pad[64 - 4 * sizeof(size_t)];
};

struct RunResult {
    size_t n_threads = 0, capacity = 0;
    double elapsed_s = 0.0;
    size_t total_p = 0, total_c = 0, total_pmiss = 0, total_cmiss = 0;
};

RunResult run_once(size_t n_threads, std::chrono::nanoseconds duration) {
    Q q(static_cast<unsigned>(QSZ));
    const size_t N    = QSZ;
    const size_t mask = N - 1;

    std::latch start_latch(n_threads + 1);
    std::atomic<bool> stop{false};
    std::vector<ThreadStats> stats(n_threads);
    std::vector<std::thread> threads;
    threads.reserve(n_threads);

    for (size_t t = 0; t < n_threads; ++t) {
        threads.emplace_back([&, t] {
            const bool producer = (t & 1) != 0;
            std::vector<int> v(N);
            for (size_t i = 0; i < N; ++i) v[i] = static_cast<int>(i);

            size_t p = 0, c = 0, pmiss = 0, cmiss = 0;
            start_latch.arrive_and_wait();

            if (producer) {
                while (!stop.load(std::memory_order_relaxed)) {
                    if (q.try_push(&v[p & mask])) ++p;
                    else ++pmiss;
                }
            } else {
                while (!stop.load(std::memory_order_relaxed)) {
                    int* ptr = nullptr;
                    if (q.try_pop(ptr)) ++c;
                    else ++cmiss;
                }
            }

            stats[t].p = p; stats[t].c = c;
            stats[t].pmiss = pmiss; stats[t].cmiss = cmiss;
        });
    }

    start_latch.arrive_and_wait();
    const auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(duration);
    stop.store(true, std::memory_order_relaxed);
    const auto t1 = std::chrono::steady_clock::now();

    for (auto& th : threads) th.join();

    { int* p = nullptr; while (q.try_pop(p)) {} }

    RunResult r{};
    r.n_threads = n_threads;
    r.capacity  = N;
    r.elapsed_s = std::chrono::duration<double>(t1 - t0).count();
    for (auto& s : stats) {
        r.total_p     += s.p;
        r.total_c     += s.c;
        r.total_pmiss += s.pmiss;
        r.total_cmiss += s.cmiss;
    }
    return r;
}

std::string humanize(double x) {
    char buf[32];
    if      (x >= 1e9) std::snprintf(buf, sizeof(buf), "%7.3fG", x / 1e9);
    else if (x >= 1e6) std::snprintf(buf, sizeof(buf), "%7.3fM", x / 1e6);
    else if (x >= 1e3) std::snprintf(buf, sizeof(buf), "%7.3fk", x / 1e3);
    else               std::snprintf(buf, sizeof(buf), "%8.1f",  x);
    return std::string(buf);
}

void print_header() {
    std::printf("%-16s %4s %12s %8s %9s %9s %9s %9s %9s\n",
                "Benchmark", "thr", "items", "ns/op",
                "items/s", "push/s", "pop/s", "pushF/s", "popF/s");
}

void print_row(const char* name, const RunResult& r) {
    const size_t items = r.total_p + r.total_c;
    const double ns_per_op =
        r.elapsed_s * 1e9 * static_cast<double>(r.n_threads) /
        static_cast<double>(items ? items : 1);
    std::printf("%-16s %4zu %12zu %8.2f %9s %9s %9s %9s %9s\n",
                name, r.n_threads, items, ns_per_op,
                humanize(static_cast<double>(items)        / r.elapsed_s).c_str(),
                humanize(static_cast<double>(r.total_p)    / r.elapsed_s).c_str(),
                humanize(static_cast<double>(r.total_c)    / r.elapsed_s).c_str(),
                humanize(static_cast<double>(r.total_pmiss)/ r.elapsed_s).c_str(),
                humanize(static_cast<double>(r.total_cmiss)/ r.elapsed_s).c_str());
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
    double duration_s = 5.0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--duration=", 0) == 0) duration_s = std::atof(a.c_str() + 11);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: %s [--duration=SECONDS]\n", argv[0]);
            return 0;
        }
    }
    const size_t hw = hw_thread_count();
    std::vector<size_t> thread_counts;
    for (size_t t = 2; t <= hw; t *= 2) thread_counts.push_back(t);
    if (thread_counts.empty() || thread_counts.back() != hw)
        thread_counts.push_back(hw);
    const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(duration_s));

    std::printf("hw_threads=%zu  capacity=%zu  duration=%.3fs\n\n",
                hw, QSZ, duration_s);
    print_header();
    for (size_t nt : thread_counts) print_row("ME_queue", run_once(nt, duration));
    return 0;
}

// Custom LATENCY benchmark for atomic_queue::AtomicQueueB (ME).
// See DN_queue/queue_lmbm.C for the self-balancing harness design.

#include "include/atomic_queue/atomic_queue.h"
#include "latbench_common.h"

namespace {

using namespace latbench;

constexpr size_t QSZ = 1UL << 16;
using Q = atomic_queue::AtomicQueueB<uint64_t>;

RunResult run_once(size_t n_threads, std::chrono::nanoseconds duration,
                   size_t refill_batch, size_t warmup_skip) {
    Q q(static_cast<unsigned>(QSZ));

    std::latch start_latch(n_threads + 1);
    std::atomic<bool> stop{false};
    std::vector<ThreadStats> stats(n_threads);
    std::vector<std::thread> threads;
    threads.reserve(n_threads);

    for (size_t t = 0; t < n_threads; ++t) {
        threads.emplace_back([&, t] {
            bind_current_thread_to_cpu(static_cast<int>(t));
            ThreadStats& s = stats[t];
            size_t warmup = warmup_skip;
            start_latch.arrive_and_wait();

            while (!stop.load(std::memory_order_relaxed)) {
                Tick ts = 0;
                if (q.try_pop(ts)) {
                    Tick now = rdtsc_end();
                    int64_t d = static_cast<int64_t>(now - ts);
                    if (d >= 0) {
                        if (warmup > 0) --warmup;
                        else            record_latency(s, static_cast<Tick>(d));
                    } else ++s.lat_neg;
                    ++s.c;
                } else {
                    ++s.cmiss;
                    for (size_t i = 0;
                         i < refill_batch && !stop.load(std::memory_order_relaxed);
                         ++i) {
                        Tick ts2 = rdtsc_start();
                        if (q.try_push(ts2)) ++s.p;
                        else { ++s.pmiss; break; }
                    }
                }
            }
        });
    }

    start_latch.arrive_and_wait();
    const auto t0 = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(duration);
    stop.store(true, std::memory_order_relaxed);
    const auto t1 = std::chrono::steady_clock::now();

    for (auto& th : threads) th.join();

    { Tick dump = 0; while (q.try_pop(dump)) {} }

    RunResult r{};
    r.n_threads = n_threads;
    r.capacity  = QSZ;
    r.elapsed_s = std::chrono::duration<double>(t1 - t0).count();
    reduce(stats, r);
    return r;
}

} // namespace

int main(int argc, char** argv) {
    double duration_s   = 5.0;
    size_t refill_batch = 8;
    size_t warmup_skip  = 100;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a.rfind("--duration=", 0) == 0) duration_s   = std::atof(a.c_str() + 11);
        else if (a.rfind("--batch=",    0) == 0) refill_batch = std::strtoull(a.c_str() + 8, nullptr, 0);
        else if (a.rfind("--warmup=",   0) == 0) warmup_skip  = std::strtoull(a.c_str() + 9, nullptr, 0);
        else if (a == "--dump-hist") latbench::g_dump_hist = true;
        else if (a == "--help" || a == "-h") {
            std::printf("usage: %s [--duration=SECONDS] [--batch=N] [--warmup=N] [--dump-hist]\n", argv[0]);
            return 0;
        }
    }

    const size_t hw = latbench::hw_thread_count();
    const double cyc_per_ns = latbench::calibrate_cycles_per_ns();

    std::vector<size_t> thread_counts;
    for (size_t t = 2; t <= hw; t *= 2) thread_counts.push_back(t);
    if (thread_counts.empty() || thread_counts.back() != hw)
        thread_counts.push_back(hw);

    const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(duration_s));

    std::printf("hw_threads=%zu capacity=%zu duration=%.3fs batch=%zu warmup=%zu rdtsc=%.4f GHz\n\n",
                hw, QSZ, duration_s, refill_batch, warmup_skip, cyc_per_ns);
    latbench::print_header();
    for (size_t nt : thread_counts) {
        latbench::print_row("ME_queue",
                            run_once(nt, duration, refill_batch, warmup_skip),
                            cyc_per_ns);
    }
    return 0;
}

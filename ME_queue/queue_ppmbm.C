// Ping-pong LATENCY benchmark for atomic_queue::AtomicQueueB (ME).
// See RingAtomicQueue/queue_ppmbm.C for the harness design.

#include "include/atomic_queue/atomic_queue.h"
#include "latbench_common.h"

namespace {

using namespace latbench;

constexpr size_t QSZ = 64;
using Q = atomic_queue::AtomicQueueB<uint64_t>;

RunResult run_once(size_t n_messages, size_t warmup_skip,
                   int tx_cpu, int rx_cpu) {
    Q q1(static_cast<unsigned>(QSZ)), q2(static_cast<unsigned>(QSZ));

    std::latch start_latch(3);
    std::vector<ThreadStats> stats(2);

    std::thread rx([&] {
        bind_current_thread_to_cpu(rx_cpu);
        start_latch.arrive_and_wait();
        for (size_t i = 0; i < n_messages; ++i) {
            Tick v;
            while (!q1.try_pop(v)) cpu_pause();
            while (!q2.try_push(v)) cpu_pause();
        }
    });

    std::thread tx([&] {
        bind_current_thread_to_cpu(tx_cpu);
        ThreadStats& s = stats[0];
        size_t warmup = warmup_skip;
        start_latch.arrive_and_wait();
        for (size_t i = 0; i < n_messages; ++i) {
            Tick t0 = rdtsc_start();
            while (!q1.try_push(t0)) cpu_pause();
            Tick echoed;
            while (!q2.try_pop(echoed)) cpu_pause();
            Tick t1 = rdtsc_end();
            int64_t d = static_cast<int64_t>(t1 - t0);
            if (d >= 0) {
                if (warmup > 0) --warmup;
                else            record_latency(s, static_cast<Tick>(d));
            } else ++s.lat_neg;
            ++s.c;
        }
        s.p = n_messages;
    });

    start_latch.arrive_and_wait();
    const auto t0 = std::chrono::steady_clock::now();
    tx.join();
    rx.join();
    const auto t1 = std::chrono::steady_clock::now();

    RunResult r{};
    r.n_threads = 2;
    r.capacity  = QSZ;
    r.elapsed_s = std::chrono::duration<double>(t1 - t0).count();
    reduce(stats, r);
    return r;
}

} // namespace

int main(int argc, char** argv) {
    size_t n_messages  = 1UL << 25;  // ~5 s wall-clock across queues
    size_t warmup_skip = 1000;
    int    tx_cpu      = 0;
    int    rx_cpu      = 1;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a.rfind("--n=",      0) == 0) n_messages  = std::strtoull(a.c_str() + 4, nullptr, 0);
        else if (a.rfind("--warmup=", 0) == 0) warmup_skip = std::strtoull(a.c_str() + 9, nullptr, 0);
        else if (a.rfind("--tx-cpu=", 0) == 0) tx_cpu      = std::atoi(a.c_str() + 9);
        else if (a.rfind("--rx-cpu=", 0) == 0) rx_cpu      = std::atoi(a.c_str() + 9);
        else if (a == "--dump-hist") latbench::g_dump_hist = true;
        else if (a == "--help" || a == "-h") {
            std::printf("usage: %s [--n=MESSAGES] [--warmup=N] [--tx-cpu=N] [--rx-cpu=N] [--dump-hist]\n", argv[0]);
            return 0;
        }
    }
    const double cyc_per_ns = latbench::calibrate_cycles_per_ns();
    std::printf("ping-pong  n=%zu capacity=%zu warmup=%zu tx-cpu=%d rx-cpu=%d rdtsc=%.4f GHz\n\n",
                n_messages, QSZ, warmup_skip, tx_cpu, rx_cpu, cyc_per_ns);
    latbench::print_header();
    latbench::print_row("ME_queue",
                        run_once(n_messages, warmup_skip, tx_cpu, rx_cpu),
                        cyc_per_ns);
    return 0;
}

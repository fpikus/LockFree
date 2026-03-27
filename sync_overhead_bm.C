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
#include <math.h>
#include <latch>
#include <chrono>
#include <thread>
#include <emmintrin.h>

using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::system_clock;

class Spinlock {
  public:
  Spinlock() = default;
  Spinlock(const Spinlock&) = delete;
  Spinlock operator=(const Spinlock&) = delete;
  void lock() {
    static const timespec ns = { 0, 1 };(void)ns;
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

alignas(64) std::atomic<unsigned long> xa(0);
alignas(64) unsigned long x(0);
Spinlock S; // Shares cache line with x
std::mutex M;

void experiment(
    const long nthreads,
    const size_t nshared,
    const size_t nwork,
    const double test_time,
    auto&& sharing,
    double& throughput
) {
    xa = 0;
    x = 0;

    std::latch sync(nthreads);
    std::atomic<int> done {0};

    auto shared_work = [&]() {
        for (size_t i = 1; i != nshared; ++i) {
            sharing();
        }
        return sharing();
    };

    auto parallel_work = [&](double& y, unsigned long& n) {
        for (size_t i = 0; i != nwork; ++i) {
            y = sin(++n + cos(y));
        }
    };

    auto work = [&](double& Y, unsigned long& N, auto&& shared_work) {
        double y {};
        unsigned long n = 0;

        sync.arrive_and_wait();

        while (done.load(std::memory_order_acquire) == 0) {
            y += shared_work();
            parallel_work(y, n);
        }

        Y = y;
        N += n;
    };

    struct Job {
        double y {};
        unsigned long n {};
        std::thread t;
    };
    std::vector<Job> jobs(nthreads);

    for (auto& job : jobs) {
        job.t = std::thread(work, std::ref(job.y), std::ref(job.n), shared_work);
    }

    auto sec = [](auto t) {
        return duration_cast<microseconds>(t).count()*1e-6;
    };

    auto t0 = system_clock::now();
    auto t = t0;
    do {
        sleep(1);
        t = system_clock::now();
    } while (sec(t - t0) < test_time);


    done.store(1, std::memory_order_release);
    unsigned long N = 0;
    for (auto& job : jobs) {
        job.t.join();
        //std::cout << job.y << " " << job.n << std::endl;
        N += job.n;
    }
    //std::cout << "test time " << sec(t - t0) << " seconds, work " << N << " throughput " << N/sec(t - t0) << std::endl;
    throughput = N/sec(t - t0);
} // experiment()

int main(int argc, char** argv) {
    const long nthreads = 12;
    const double test_time = 1; // Seconds
    struct WorkSpec {
        size_t nshared;
        size_t nwork;
    };
    std::vector<WorkSpec> workspecs{ 
        //{1000, 1}, {100, 1}, {10, 1},
        //{1, 1}, {10, 10},
        {1, 2}, {1, 5}, {1, 10}, {1, 100},
        {1, 1000}, {1, 10000},
    };
    bool run_prime = true, run_atomic = true, run_spinlock = true, run_mutex = false, run_cas = true;
    if (argc >= 3) {
        const size_t nshared = ::atol(argv[1]);
        const size_t nwork = ::atol(argv[2]);
        if (nshared > 0 && nwork > 0) {
            workspecs.resize(1);
            workspecs[0].nshared = nshared;
            workspecs[0].nwork = nwork;
        }
    }
    if (argc >= 4) {
        const char worktype = argv[3][0];
        if (worktype == 'a') run_prime = run_spinlock = run_mutex = run_cas = false;
        if (worktype == 'c') run_prime = run_atomic = run_spinlock = run_mutex = false;
        if (worktype == 's') run_prime = run_atomic = run_mutex = run_cas = false;
        if (worktype == 'm') { run_prime = run_atomic = run_spinlock = run_cas = false; run_mutex = true; }
    }

    auto sharing_atomic = [&]() {
        return xa.fetch_add(1, std::memory_order_acq_rel);
    };
    auto sharing_cas = [&]() {
        unsigned long xl = xa.load(std::memory_order_acquire);
        while (!xa.compare_exchange_strong(xl, xl + 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {}
        return xl + 1;
    };
    auto sharing_spinlock = [&]() {
        std::lock_guard L(S);
        ++x;
        return x;
    };
    auto sharing_mutex = [&]() {
        std::lock_guard L(M);
        ++x;
        return x;
    };

    std::cout << "Starting tests" << std::endl;
    for (WorkSpec spec : workspecs) {
        double t_atomic {}, t_cas {}, t_spinlock {}, t_mutex {};

        if (run_prime) {
            std::cout << "Priming " << std::endl;
            experiment(nthreads, spec.nshared, spec.nwork, test_time, sharing_atomic, t_atomic);
            experiment(nthreads, spec.nshared, spec.nwork, test_time, sharing_cas, t_cas);
            experiment(nthreads, spec.nshared, spec.nwork, test_time, sharing_spinlock, t_spinlock);
        }

        std::cout << "Parallel/shared ratio: " << double(spec.nwork)/spec.nshared;
        if (run_atomic) {
            experiment(nthreads, spec.nshared, spec.nwork, test_time, sharing_atomic, t_atomic);
            std::cout << " atomic: " << t_atomic;
        }
        if (run_cas) {
            experiment(nthreads, spec.nshared, spec.nwork, test_time, sharing_cas, t_cas);
            std::cout << " cas: " << t_cas;
        }
        if (run_spinlock) {
            experiment(nthreads, spec.nshared, spec.nwork, test_time, sharing_spinlock, t_spinlock);
            std::cout << " spinlock: " << t_spinlock;
        }
        if (run_mutex) {
            experiment(nthreads, spec.nshared, spec.nwork, test_time, sharing_mutex, t_mutex);
            std::cout << " mutex: " << t_mutex;
        }
        if (run_atomic && run_spinlock) {
            std::cout << " spinlock/atomic " << t_spinlock/t_atomic;
        }
        if (run_cas && run_spinlock) {
            std::cout << " spinlock/cas " << t_spinlock/t_cas;
        }
        if (run_atomic && run_mutex) {
            std::cout << " mutex/atomic " << t_mutex/t_atomic;
        }
        if (run_cas && run_mutex) {
            std::cout << " mutex/cas " << t_mutex/t_cas;
        }
        std::cout << std::endl;
    }
}

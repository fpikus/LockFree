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

#ifndef RING_ATOMIC_QUEUE_H_
#define RING_ATOMIC_QUEUE_H_

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <new>
#include <type_traits>
#include <utility>

// ---------------------------------------------------------------------------
// Simple CHECK macros (abort on failure, no messages).
// ---------------------------------------------------------------------------
#define CHECK(c) if (!(c)) std::abort();
#define CHECK_GE(a, b) if ((a) < (b)) std::abort();
#define CHECK_EQ(a, b) if ((a) != (b)) std::abort();

// ---------------------------------------------------------------------------
// REQUIRES - SFINAE helper from concept-utils.h
// ---------------------------------------------------------------------------
#define REQUIRES(...) std::enable_if_t<(__VA_ARGS__), bool> = true

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

// ---------------------------------------------------------------------------
// SpinLock - simplified standalone version using std::atomic<int>
// ---------------------------------------------------------------------------
namespace {
    static const struct timespec spin_wait_short = { 0, 1 };
    static const struct timespec spin_wait_long  = { 0, 10000001 };
}

class SpinLock {
public:
    void lock() {
        for (int spin_count = 0;
             lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire);
             ++spin_count)
        {
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return;
            if (spin_count < 8) {
                nanosleep(&spin_wait_short, nullptr);
            } else {
                spin_count = 0;
                nanosleep(&spin_wait_long, nullptr);
            }
        }
    }

    void unlock() {
        lock_.store(0, std::memory_order_release);
    }

    bool try_lock() {
        for (int spin_count = 0;
             lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire);
             ++spin_count)
        {
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
            if (!(lock_.load(std::memory_order_relaxed) || lock_.exchange(1, std::memory_order_acquire))) return true;
            if (spin_count < 8) {
                nanosleep(&spin_wait_short, nullptr);
            } else {
                return false;
            }
        }
        return true;
    }

    bool locked() const {
        return lock_.load(std::memory_order_relaxed) == 1;
    }

    class ScopeLock {
    public:
        ScopeLock(SpinLock& spinlock) : spinlock_(&spinlock) { spinlock_->lock(); }
        ScopeLock(SpinLock* spinlock) : spinlock_(spinlock) { if (spinlock_) spinlock_->lock(); }
        ~ScopeLock() { if (spinlock_) spinlock_->unlock(); }
        void release() { if (spinlock_) spinlock_->unlock(); spinlock_ = nullptr; }
        operator bool() const { return false; }
    private:
        SpinLock* spinlock_;
    };

    class ScopeUnlock {
    public:
        ScopeUnlock(SpinLock& spinlock) : spinlock_(&spinlock) { spinlock_->unlock(); }
        ScopeUnlock(SpinLock* spinlock) : spinlock_(spinlock) { if (spinlock_) spinlock_->unlock(); }
        ~ScopeUnlock() { if (spinlock_) spinlock_->lock(); }
        void release() { if (spinlock_) spinlock_->lock(); spinlock_ = nullptr; }
        operator bool() const { return false; }
    private:
        SpinLock* spinlock_;
    };

private:
    std::atomic<int> lock_ {0};
};

// ---------------------------------------------------------------------------
// RingAtomicMapQueueMPMC
// ---------------------------------------------------------------------------

// Key and Value together form the element stored in the queue. The Key type
// has to support lock-free atomic operations; this implies that it is
// trivially copyable and destructible. The value type can be any type; the
// default (void) means that the queue contains only keys.
template <typename Key, typename Value = void,
         size_t NTRY = 8,                       // Number of attempts to acquire a slot before giving up
         size_t ALIGN = 0>                      // Additional alignment; 64 to align each queue slot on a cache line
class RingAtomicMapQueueMPMC {
    static_assert(std::atomic<Key>::is_always_lock_free);
    // Queue slot type (with and without value).
    template <typename K, typename V> struct alignas(std::max({ALIGN, alignof(std::atomic<K>), alignof(V)})) queue_slot_t {
        std::atomic<K> key;
        std::atomic<int> busy;      // In-flight commit: set under lock before unlock, cleared after key store/clear
        V value;
    };
    template <typename K> struct alignas(std::max({ALIGN, alignof(std::atomic<K>)})) queue_slot_t<K, void> {
        std::atomic<K> key;
        std::atomic<int> busy;      // In-flight commit: set under lock before unlock, cleared after key store/clear
    };
    using slot_t = queue_slot_t<Key, Value>;

    public:
    // Constructor initializes the queue with the span of memory to be used as
    // a ring buffer.  While any size is allowed, the queue will use the
    // largest array of elements whose size is a power of 2 that can fit into
    // the provided buffer. The buffer must have space for at least 8 elements.
    RingAtomicMapQueueMPMC(void* memory, size_t bytes) :
        queue_(static_cast<slot_t*>(memory)),
        capacity_(AddressUtils::prev_power_of_2(bytes/sizeof(slot_t))),
        capacity_mask_(capacity_ - 1)
    {
        CHECK_GE(capacity_, 8);
        ::memset(queue_, 0, capacity_ * sizeof(slot_t));
    }

    // Destructor drains the queue if there are any elements remaining.
    ~RingAtomicMapQueueMPMC() {
        // Drain remaining elements. For KV queues this runs value destructors
        // that would otherwise leak. No-op if the queue is already empty.
        for (size_t i = head_.i; i != tail_.i; ++i) {
            slot_t& slot = queue_[i & capacity_mask_];
            if constexpr (!std::is_same_v<Value, void>) {
                slot.value.~Value();
            }
            slot.key.store(Key{}, std::memory_order_relaxed);
        }
    }

    // Push an element (key-value pair) onto the queue.
    // This method copies or moves the value by the corresponding constructor.
    // The key is copied bit-wise, atomically.
    // The method returns true if the operation was successful, and false if
    // the queue is full. In the latter case, the value is not moved-from and
    // is otherwise unchanged.
    template <typename V, REQUIRES(std::is_constructible_v<Value, V>)>
    bool push(Key key, V&& value) {
        SpinLock::ScopeLock l(tail_.l);
        // Next slot to store the data into.
        slot_t& slot = queue_[tail_.i & capacity_mask_];
        // If the slot is empty, we can enqueue and increment producer index.
        // Otherwise, the queue is full, bail out.
        for (size_t i = 0; i != NTRY; ++i) {
            if (slot.key.load(std::memory_order_acquire) == Key{} &&
                !slot.busy.load(std::memory_order_relaxed)) {
                slot.busy.store(1, std::memory_order_relaxed);
                ++tail_.i;
                l.release();    // Atomic operations on queue slots are enough from now on
                ::new(&slot.value) Value(std::forward<V>(value));       // Move/copy-constructed in place
                slot.key.store(key, std::memory_order_release);         // Slot is released
                slot.busy.store(0, std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    }

    // Push for key-only queue.
    template <typename V = void, REQUIRES(std::is_same_v<Value, V>)>
    bool push(Key key) {
        SpinLock::ScopeLock l(tail_.l);
        // Next slot to store the data into.
        slot_t& slot = queue_[tail_.i & capacity_mask_];
        // If the slot is empty, we can enqueue and increment producer index.
        // Otherwise, the queue is full, bail out.
        for (size_t i = 0; i != NTRY; ++i) {
            if (slot.key.load(std::memory_order_acquire) == Key{} &&
                !slot.busy.load(std::memory_order_relaxed)) {
                slot.busy.store(1, std::memory_order_relaxed);
                ++tail_.i;
                l.release();    // Atomic operations on queue slots are enough from now on
                slot.key.store(key, std::memory_order_release);
                slot.busy.store(0, std::memory_order_relaxed);
                return true;
            }
        }
        return false;
    } // push()

    // Pop the first element from the queue. The key is returned; if the queue
    // is empty, the default-constructed value is returned.  Value is returned
    // by move assignment only if the queue is not empty, otherwise caller's
    // value is unchanged.
    template <typename V = Value, REQUIRES(!std::is_same_v<void, V>)>
    Key pop(V& value) {
        SpinLock::ScopeLock l(head_.l);
        // Next slot to read the data from.
        slot_t& slot = queue_[head_.i & capacity_mask_];
        // If the slot is not empty and not mid-commit, we can dequeue and empty it out.
        Key ret = slot.key.load(std::memory_order_acquire);
        if (ret != Key{} && !slot.busy.load(std::memory_order_relaxed)) {
            slot.busy.store(1, std::memory_order_relaxed);
            ++head_.i;
            l.release();    // Atomic operations on queue slots are enough from now on
            value = std::move(slot.value);  // Move-assigned to the caller
            slot.value.~Value();            // Destructed in place, push will construct again
            slot.key.store(Key{}, std::memory_order_release);
            slot.busy.store(0, std::memory_order_relaxed);
            return ret;
        }
        return Key{};
    }

    // Pop for key-only queue.
    template <typename V = Value, REQUIRES(std::is_same_v<void, V>)>
    Key pop() {
        SpinLock::ScopeLock l(head_.l);
        // Next slot to read the data from.
        slot_t& slot = queue_[head_.i & capacity_mask_];
        // If the slot is not empty and not mid-commit, we can dequeue and empty it out.
        Key ret = slot.key.load(std::memory_order_acquire);
        if (ret != Key{} && !slot.busy.load(std::memory_order_relaxed)) {
            slot.busy.store(1, std::memory_order_relaxed);
            ++head_.i;
            l.release();    // Atomic operations on queue slots are enough from now on
            slot.key.store(Key{}, std::memory_order_release);
            slot.busy.store(0, std::memory_order_relaxed);
            return ret;
        }
        return Key{};
    } // pop()

    // Element size, with the requested alignment.
    static constexpr size_t element_size() { return sizeof(slot_t); }

    // Element alignment.
    static constexpr size_t element_align() { return alignof(slot_t); }

    // Current queue capacity, in number of elements.
    size_t capacity() const { return capacity_; }

    // Check if the queue is empty. If the queue is not empty, pop() would have
    // succeeded were it done instead of empty(). No guarantee that the next
    // pop() call succeeds unless there are no consumers active at the same
    // time.
    bool empty() const {
        SpinLock::ScopeLock l(head_.l);
        // Next slot to read the data from.
        slot_t& slot = queue_[head_.i & capacity_mask_];
        // If the slot is not empty, the queue is not empty either.
        Key key = slot.key.load(std::memory_order_relaxed);
        return key == Key{};
    } // empty()

    private:
    slot_t* const queue_;                       // Queue memory (slot_t[capacity_] array)
    size_t const capacity_;                     // Current queue capacity (number of elements, always a power of 2)
    size_t const capacity_mask_;                // Cached index bit mask (capacity_ - 1)
    
    // Tail - producer data.
    alignas(256) struct tail_t {
        mutable SpinLock l;
        size_t i {};
    } tail_;
    
    // Head - consumer data.
    alignas(256) struct head_t {
        mutable SpinLock l;
        size_t i {};
    } head_;
}; // RingAtomicMapQueueMPMC

#endif // RING_ATOMIC_QUEUE_H_

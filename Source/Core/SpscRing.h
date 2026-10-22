// NX Player — MIT License
// Lock-free single-producer / single-consumer ring of trivially copyable elements.
// The producer (decoder / network thread) and the consumer (audio thread) never block,
// never allocate and never take a lock. Capacity is fixed at construction.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>

namespace nx
{

template <typename T>
class SpscRing
{
    static_assert (std::is_trivially_copyable_v<T>, "SpscRing only stores trivially copyable data");

public:
    explicit SpscRing (size_t capacityElements = 0) { reset (capacityElements); }

    /** Not thread-safe: call only while neither side is running. */
    void reset (size_t capacityElements)
    {
        cap = capacityElements + 1;   // one slot is kept empty to tell "full" from "empty"
        storage.reset (cap > 1 ? new T[cap] : nullptr);
        head.store (0, std::memory_order_relaxed);
        tail.store (0, std::memory_order_relaxed);
    }

    size_t capacity() const noexcept { return cap > 0 ? cap - 1 : 0; }

    size_t readable() const noexcept
    {
        const auto h = head.load (std::memory_order_acquire);
        const auto t = tail.load (std::memory_order_acquire);
        return h >= t ? h - t : cap - t + h;
    }

    size_t writable() const noexcept { return capacity() - readable(); }

    /** Producer side. Returns the number of elements written (may be less than n). */
    size_t write (const T* src, size_t n) noexcept
    {
        const auto h = head.load (std::memory_order_relaxed);
        const auto t = tail.load (std::memory_order_acquire);
        const size_t free = (t > h ? t - h : cap - h + t) - 1;
        n = n < free ? n : free;
        const size_t first = n < cap - h ? n : cap - h;
        std::memcpy (storage.get() + h, src, first * sizeof (T));
        std::memcpy (storage.get(), src + first, (n - first) * sizeof (T));
        head.store ((h + n) % cap, std::memory_order_release);
        return n;
    }

    /** Consumer side. Returns the number of elements read. */
    size_t read (T* dst, size_t n) noexcept
    {
        const auto t = tail.load (std::memory_order_relaxed);
        const auto h = head.load (std::memory_order_acquire);
        const size_t avail = h >= t ? h - t : cap - t + h;
        n = n < avail ? n : avail;
        const size_t first = n < cap - t ? n : cap - t;
        std::memcpy (dst, storage.get() + t, first * sizeof (T));
        std::memcpy (dst + first, storage.get(), (n - first) * sizeof (T));
        tail.store ((t + n) % cap, std::memory_order_release);
        return n;
    }

    /** Consumer side: drops up to n elements. */
    size_t discard (size_t n) noexcept
    {
        const auto t = tail.load (std::memory_order_relaxed);
        const auto h = head.load (std::memory_order_acquire);
        const size_t avail = h >= t ? h - t : cap - t + h;
        n = n < avail ? n : avail;
        tail.store ((t + n) % cap, std::memory_order_release);
        return n;
    }

    /** Consumer side: copies without consuming. */
    size_t peek (T* dst, size_t n) const noexcept
    {
        const auto t = tail.load (std::memory_order_relaxed);
        const auto h = head.load (std::memory_order_acquire);
        const size_t avail = h >= t ? h - t : cap - t + h;
        n = n < avail ? n : avail;
        const size_t first = n < cap - t ? n : cap - t;
        std::memcpy (dst, storage.get() + t, first * sizeof (T));
        std::memcpy (dst + first, storage.get(), (n - first) * sizeof (T));
        return n;
    }

private:
    std::unique_ptr<T[]> storage;
    size_t cap = 0;
    alignas (64) std::atomic<size_t> head { 0 };
    alignas (64) std::atomic<size_t> tail { 0 };
};

} // namespace nx

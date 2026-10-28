// SpeedBreaker PS5: what the payload SDK's libc++ 18 lacks, force-included
// into every C++ file of the PS5 build (port/ps5/cmake/ps5.cmake).
#pragma once
#ifdef __cplusplus
#include <atomic>
#include <cstddef>
#include <type_traits>

// std::atomic_ref came with libc++ 19. The runtime uses it on guest memory
// (critical sections, spin locks, movie_pacing's countdown): this one has
// the operations it needs, on the compiler's __atomic builtins, and waits
// through libc++ 18's own address-keyed contention table, as std::atomic
// does there (__atomic/atomic_sync.h).
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 190000
namespace std
{
    template <class T>
    struct atomic_ref
    {
        static_assert(std::is_trivially_copyable_v<T>);
        static constexpr bool is_always_lock_free = __atomic_always_lock_free(sizeof(T), 0);
        static constexpr size_t required_alignment = alignof(T) > sizeof(T) ? alignof(T) : sizeof(T);

        explicit atomic_ref(T& object) noexcept : m_ptr(std::addressof(object)) {}
        atomic_ref(const atomic_ref&) noexcept = default;
        atomic_ref& operator=(const atomic_ref&) = delete;

        T load(memory_order order = memory_order_seq_cst) const noexcept
        {
            T value;
            __atomic_load(m_ptr, &value, int(order));
            return value;
        }
        operator T() const noexcept { return load(); }
        void store(T value, memory_order order = memory_order_seq_cst) const noexcept
        {
            __atomic_store(m_ptr, &value, int(order));
        }
        T operator=(T value) const noexcept { store(value); return value; }
        T exchange(T value, memory_order order = memory_order_seq_cst) const noexcept
        {
            T old;
            __atomic_exchange(m_ptr, &value, &old, int(order));
            return old;
        }
        bool compare_exchange_weak(T& expected, T desired, memory_order order = memory_order_seq_cst) const noexcept
        {
            return __atomic_compare_exchange(m_ptr, &expected, &desired, true, int(order), int(FailureOrder(order)));
        }
        bool compare_exchange_strong(T& expected, T desired, memory_order order = memory_order_seq_cst) const noexcept
        {
            return __atomic_compare_exchange(m_ptr, &expected, &desired, false, int(order), int(FailureOrder(order)));
        }
        template <class U = T, class = enable_if_t<is_integral_v<U>>>
        T fetch_add(T value, memory_order order = memory_order_seq_cst) const noexcept
        {
            return __atomic_fetch_add(m_ptr, value, int(order));
        }
        template <class U = T, class = enable_if_t<is_integral_v<U>>>
        T fetch_sub(T value, memory_order order = memory_order_seq_cst) const noexcept
        {
            return __atomic_fetch_sub(m_ptr, value, int(order));
        }
        template <class U = T, class = enable_if_t<is_integral_v<U>>>
        T fetch_or(T value, memory_order order = memory_order_seq_cst) const noexcept
        {
            return __atomic_fetch_or(m_ptr, value, int(order));
        }
        template <class U = T, class = enable_if_t<is_integral_v<U>>>
        T fetch_and(T value, memory_order order = memory_order_seq_cst) const noexcept
        {
            return __atomic_fetch_and(m_ptr, value, int(order));
        }

        void wait(T old, memory_order order = memory_order_seq_cst) const noexcept
        {
            for (;;)
            {
                __cxx_contention_t monitor = __libcpp_atomic_monitor(m_ptr);
                if (!Equal(load(order), old))
                    return;
                __libcpp_atomic_wait(m_ptr, monitor);
            }
        }
        void notify_one() const noexcept { __cxx_atomic_notify_one(m_ptr); }
        void notify_all() const noexcept { __cxx_atomic_notify_all(m_ptr); }

    private:
        static constexpr memory_order FailureOrder(memory_order order) noexcept
        {
            return order == memory_order_acq_rel ? memory_order_acquire
                : order == memory_order_release ? memory_order_relaxed : order;
        }
        static bool Equal(const T& a, const T& b) noexcept
        {
            return __builtin_memcmp(&a, &b, sizeof(T)) == 0;
        }
        T* m_ptr;
    };
    template <class T>
    atomic_ref(T&) -> atomic_ref<T>;
}
#endif
#endif

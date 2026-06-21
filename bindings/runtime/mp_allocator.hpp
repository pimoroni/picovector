#include <cstdlib>
#include <limits>
#include <new>
#include <cstdint>
#include <type_traits>

#ifndef MP_ALLOCATOR_H
#define MP_ALLOCATOR_H

extern "C" {
#include "py/runtime.h"
}

// ─────────────────────────────────────────────────────────────────────────────
// no-scan eligibility
//
// A std::vector<T> backing buffer can skip MicroPython's GC scan only when T
// provably contains NO pointers into the GC heap. We deliberately do NOT use
// std::is_trivially_copyable for this: a raw pointer, and any POD that embeds
// one (e.g. `struct { Foo* p; int n; }`), are *both* trivially copyable, so that
// trait would happily mark pointer-bearing data as no-scan and let the GC free
// the pointee out from under it. C++ has no "transitively contains a pointer"
// trait (that needs reflection), so the rule here is fail-safe:
//
//   * arithmetic / enum scalars  → provably pointer-free            → no_scan
//   * everything else            → assume it may hold a pointer     → SCAN
//
// Pointer-free POD aggregates we have audited opt in explicitly via
// MP_POINTER_FREE(T). Note the trait keys off the *declared element type*, so
// it is only as honest as that type — a raw byte buffer later reinterpreted as
// a pointer-bearing struct must be decided by hand (use m_malloc directly).
// ─────────────────────────────────────────────────────────────────────────────

template <class T>
struct mp_no_scan : std::bool_constant<std::is_arithmetic_v<T> || std::is_enum_v<T>> {};

// Vouch that an aggregate is pointer-free (audit before adding!).
#define MP_POINTER_FREE(T) template <> struct mp_no_scan<T> : std::true_type {}

namespace picovector { struct vec2_t; }
MP_POINTER_FREE(picovector::vec2_t);   // { float x, y } — no pointers

template<class T>
struct MPAllocator
{
    typedef T value_type;

    MPAllocator() = default;

    template<class U>
    constexpr MPAllocator(const MPAllocator <U>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n)
    {
        void *p;
        if constexpr (mp_no_scan<T>::value) {
            p = m_malloc_no_scan(n * sizeof(T));   // pointer-free data
        } else {
            p = m_malloc(n * sizeof(T));           // may hold GC pointers → scan
        }
        if (p)
        {
            return static_cast<T*>(p);
        }
        mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Failed to allocate %lu bytes!"), n);
        return NULL;
    }

    void deallocate(T* p, std::size_t n) noexcept
    {
#if MICROPY_MALLOC_USES_ALLOCATED_SIZE
        m_free(p, n);
#else
        (void)n;
        m_free(p);
#endif
    }
};

template<class T, class U>
bool operator==(const MPAllocator <T>&, const MPAllocator <U>&) { return true; }

template<class T, class U>
bool operator!=(const MPAllocator <T>&, const MPAllocator <U>&) { return false; }

#endif // MP_ALLOCATOR_H

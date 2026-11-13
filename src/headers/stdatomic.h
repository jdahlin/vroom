/*
 * vcc's <stdatomic.h> (Darwin target): C11 atomics on the GNU __atomic_*
 * builtins, as GCC's header does.  _Atomic types have clang's layout, and
 * plain loads, stores and operators on them are atomic (seq_cst).
 */
#ifndef __VCC_STDATOMIC_H
#define __VCC_STDATOMIC_H

#include <stddef.h>
#include <stdint.h>

typedef enum memory_order {
	memory_order_relaxed = __ATOMIC_RELAXED,
	memory_order_consume = __ATOMIC_CONSUME,
	memory_order_acquire = __ATOMIC_ACQUIRE,
	memory_order_release = __ATOMIC_RELEASE,
	memory_order_acq_rel = __ATOMIC_ACQ_REL,
	memory_order_seq_cst = __ATOMIC_SEQ_CST
} memory_order;

typedef _Atomic(_Bool) atomic_bool;
typedef _Atomic(char) atomic_char;
typedef _Atomic(signed char) atomic_schar;
typedef _Atomic(unsigned char) atomic_uchar;
#if __STDC_VERSION__ >= 202311L
typedef _Atomic(unsigned char) atomic_char8_t;
#define ATOMIC_CHAR8_T_LOCK_FREE __CLANG_ATOMIC_CHAR8_T_LOCK_FREE
#endif
typedef _Atomic(__CHAR16_TYPE__) atomic_char16_t;
typedef _Atomic(__CHAR32_TYPE__) atomic_char32_t;
typedef _Atomic(__WCHAR_TYPE__) atomic_wchar_t;
typedef _Atomic(short) atomic_short;
typedef _Atomic(unsigned short) atomic_ushort;
typedef _Atomic(int) atomic_int;
typedef _Atomic(unsigned int) atomic_uint;
typedef _Atomic(long) atomic_long;
typedef _Atomic(unsigned long) atomic_ulong;
typedef _Atomic(long long) atomic_llong;
typedef _Atomic(unsigned long long) atomic_ullong;
typedef _Atomic(int_least8_t) atomic_int_least8_t;
typedef _Atomic(uint_least8_t) atomic_uint_least8_t;
typedef _Atomic(int_least16_t) atomic_int_least16_t;
typedef _Atomic(uint_least16_t) atomic_uint_least16_t;
typedef _Atomic(int_least32_t) atomic_int_least32_t;
typedef _Atomic(uint_least32_t) atomic_uint_least32_t;
typedef _Atomic(int_least64_t) atomic_int_least64_t;
typedef _Atomic(uint_least64_t) atomic_uint_least64_t;
typedef _Atomic(int_fast8_t) atomic_int_fast8_t;
typedef _Atomic(uint_fast8_t) atomic_uint_fast8_t;
typedef _Atomic(int_fast16_t) atomic_int_fast16_t;
typedef _Atomic(uint_fast16_t) atomic_uint_fast16_t;
typedef _Atomic(int_fast32_t) atomic_int_fast32_t;
typedef _Atomic(uint_fast32_t) atomic_uint_fast32_t;
typedef _Atomic(int_fast64_t) atomic_int_fast64_t;
typedef _Atomic(uint_fast64_t) atomic_uint_fast64_t;
typedef _Atomic(intptr_t) atomic_intptr_t;
typedef _Atomic(uintptr_t) atomic_uintptr_t;
typedef _Atomic(size_t) atomic_size_t;
typedef _Atomic(ptrdiff_t) atomic_ptrdiff_t;
typedef _Atomic(intmax_t) atomic_intmax_t;
typedef _Atomic(uintmax_t) atomic_uintmax_t;

#define ATOMIC_BOOL_LOCK_FREE 2
#define ATOMIC_CHAR_LOCK_FREE 2
#define ATOMIC_CHAR16_T_LOCK_FREE __CLANG_ATOMIC_CHAR16_T_LOCK_FREE
#define ATOMIC_CHAR32_T_LOCK_FREE __CLANG_ATOMIC_CHAR32_T_LOCK_FREE
#define ATOMIC_WCHAR_T_LOCK_FREE __CLANG_ATOMIC_WCHAR_T_LOCK_FREE
#define ATOMIC_SHORT_LOCK_FREE 2
#define ATOMIC_INT_LOCK_FREE 2
#define ATOMIC_LONG_LOCK_FREE 2
#define ATOMIC_LLONG_LOCK_FREE 2
#define ATOMIC_POINTER_LOCK_FREE 2

#define ATOMIC_VAR_INIT(v) (v)
#define atomic_init(p, v) ((void)(*(p) = (v)))
#define kill_dependency(y) (y)

/* functions as well, as clang's header: their addresses can be taken */
void atomic_thread_fence(memory_order);
void atomic_signal_fence(memory_order);
#define atomic_thread_fence(mo) __atomic_thread_fence(mo)
#define atomic_signal_fence(mo) __atomic_signal_fence(mo)
#define atomic_is_lock_free(p) __atomic_is_lock_free(sizeof(*(p)), (p))

#define atomic_store_explicit(p, v, mo) __atomic_store_n((p), (v), (mo))
#define atomic_store(p, v) __atomic_store_n((p), (v), __ATOMIC_SEQ_CST)
#define atomic_load_explicit(p, mo) __atomic_load_n((p), (mo))
#define atomic_load(p) __atomic_load_n((p), __ATOMIC_SEQ_CST)
#define atomic_exchange_explicit(p, v, mo) __atomic_exchange_n((p), (v), (mo))
#define atomic_exchange(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_strong_explicit(p, e, d, s, f) __atomic_compare_exchange_n((p), (e), (d), 0, (s), (f))
#define atomic_compare_exchange_strong(p, e, d) __atomic_compare_exchange_n((p), (e), (d), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
#define atomic_compare_exchange_weak_explicit(p, e, d, s, f) __atomic_compare_exchange_n((p), (e), (d), 1, (s), (f))
#define atomic_compare_exchange_weak(p, e, d) __atomic_compare_exchange_n((p), (e), (d), 1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)

#define atomic_fetch_add_explicit(p, v, mo) __atomic_fetch_add((p), (v), (mo))
#define atomic_fetch_add(p, v) __atomic_fetch_add((p), (v), __ATOMIC_SEQ_CST)
#define atomic_fetch_sub_explicit(p, v, mo) __atomic_fetch_sub((p), (v), (mo))
#define atomic_fetch_sub(p, v) __atomic_fetch_sub((p), (v), __ATOMIC_SEQ_CST)
#define atomic_fetch_or_explicit(p, v, mo) __atomic_fetch_or((p), (v), (mo))
#define atomic_fetch_or(p, v) __atomic_fetch_or((p), (v), __ATOMIC_SEQ_CST)
#define atomic_fetch_xor_explicit(p, v, mo) __atomic_fetch_xor((p), (v), (mo))
#define atomic_fetch_xor(p, v) __atomic_fetch_xor((p), (v), __ATOMIC_SEQ_CST)
#define atomic_fetch_and_explicit(p, v, mo) __atomic_fetch_and((p), (v), (mo))
#define atomic_fetch_and(p, v) __atomic_fetch_and((p), (v), __ATOMIC_SEQ_CST)

typedef struct atomic_flag { unsigned char __val; } atomic_flag;
#define ATOMIC_FLAG_INIT { 0 }
_Bool atomic_flag_test_and_set(volatile atomic_flag *);
_Bool atomic_flag_test_and_set_explicit(volatile atomic_flag *, memory_order);
void atomic_flag_clear(volatile atomic_flag *);
void atomic_flag_clear_explicit(volatile atomic_flag *, memory_order);
#define atomic_flag_test_and_set_explicit(p, mo) ((_Bool)__atomic_exchange_n(&(p)->__val, 1, (mo)))
#define atomic_flag_test_and_set(p) atomic_flag_test_and_set_explicit(p, __ATOMIC_SEQ_CST)
#define atomic_flag_clear_explicit(p, mo) __atomic_store_n(&(p)->__val, 0, (mo))
#define atomic_flag_clear(p) atomic_flag_clear_explicit(p, __ATOMIC_SEQ_CST)

#endif

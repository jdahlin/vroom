# 20.10.2 Header `<memory>` synopsis [memory.syn]

[Contents](../../README.md) · up: [20.10 Memory \[memory\]](memory.md) · prev: [20.10.1 In general \[memory.general\]](memory.general.md) · next: [20.10.3 Pointer traits \[pointer.traits\]](pointer.traits.md)

**1** The header `<memory>` defines several types and function templates that describe properties of pointers and pointer-like types, manage memory for containers and other template types, destroy objects, and construct objects in uninitialized memory buffers ([[pointer.traits]](pointer.traits.md)–[[specialized.addressof]](specialized.addressof.md) and [[specialized.algorithms]](../algorithms/specialized.algorithms.md)). The header also defines the templates `unique_ptr`, `shared_ptr`, `weak_ptr`, and various function templates that operate on objects of these types ([[smartptr]](smartptr.md)).

```cpp
#include <compare>              // see [compare.syn]

namespace std {
  // [pointer.traits], pointer traits
  template<class Ptr> struct pointer_traits;
  template<class T> struct pointer_traits<T*>;

  // [pointer.conversion], pointer conversion
  template<class T>
    constexpr T* to_address(T* p) noexcept;
  template<class Ptr>
    constexpr auto to_address(const Ptr& p) noexcept;

  // [util.dynamic.safety], pointer safety
  enum class pointer_safety { relaxed, preferred, strict };
  void declare_reachable(void* p);
  template<class T>
    T* undeclare_reachable(T* p);
  void declare_no_pointers(char* p, size_t n);
  void undeclare_no_pointers(char* p, size_t n);
  pointer_safety get_pointer_safety() noexcept;

  // [ptr.align], pointer alignment
  void* align(size_t alignment, size_t size, void*& ptr, size_t& space);
  template<size_t N, class T>
    [[nodiscard]] constexpr T* assume_aligned(T* ptr);

  // [allocator.tag], allocator argument tag
  struct allocator_arg_t { explicit allocator_arg_t() = default; };
  inline constexpr allocator_arg_t allocator_arg{};

  // [allocator.uses], uses_allocator
  template<class T, class Alloc> struct uses_allocator;

  // [allocator.uses.trait], uses_allocator
  template<class T, class Alloc>
    inline constexpr bool uses_allocator_v = uses_allocator<T, Alloc>::value;

  // [allocator.uses.construction], uses-allocator construction
  template<class T, class Alloc, class... Args>
    constexpr auto uses_allocator_construction_args(const Alloc& alloc,
                                                    Args&&... args) noexcept -> see below;
  template<class T, class Alloc, class Tuple1, class Tuple2>
    constexpr auto uses_allocator_construction_args(const Alloc& alloc, piecewise_construct_t,
                                                    Tuple1&& x, Tuple2&& y)
                                                    noexcept ->  see below;
  template<class T, class Alloc>
    constexpr auto uses_allocator_construction_args(const Alloc& alloc) noexcept -> see below;
  template<class T, class Alloc, class U, class V>
    constexpr auto uses_allocator_construction_args(const Alloc& alloc,
                                                    U&& u, V&& v) noexcept -> see below;
  template<class T, class Alloc, class U, class V>
    constexpr auto uses_allocator_construction_args(const Alloc& alloc,
                                                    const pair<U,V>& pr) noexcept -> see below;
  template<class T, class Alloc, class U, class V>
    constexpr auto uses_allocator_construction_args(const Alloc& alloc,
                                                    pair<U,V>&& pr) noexcept -> see below;
  template<class T, class Alloc, class... Args>
    constexpr T make_obj_using_allocator(const Alloc& alloc, Args&&... args);
  template<class T, class Alloc, class... Args>
    constexpr T* uninitialized_construct_using_allocator(T* p, const Alloc& alloc,
                                                         Args&&... args);

  // [allocator.traits], allocator traits
  template<class Alloc> struct allocator_traits;

  // [default.allocator], the default allocator
  template<class T> class allocator;
  template<class T, class U>
    constexpr bool operator==(const allocator<T>&, const allocator<U>&) noexcept;

  // [specialized.addressof], addressof
  template<class T>
    constexpr T* addressof(T& r) noexcept;
  template<class T>
    const T* addressof(const T&&) = delete;

  // [specialized.algorithms], specialized algorithms
  // [special.mem.concepts], special memory concepts
  template<class I>
    concept no-throw-input-iterator = see below;    // exposition only
  template<class I>
    concept no-throw-forward-iterator = see below;  // exposition only
  template<class S, class I>
    concept no-throw-sentinel = see below;          // exposition only
  template<class R>
    concept no-throw-input-range = see below;       // exposition only
  template<class R>
    concept no-throw-forward-range = see below;     // exposition only

  template<class NoThrowForwardIterator>
    void uninitialized_default_construct(NoThrowForwardIterator first,
                                         NoThrowForwardIterator last);
  template<class ExecutionPolicy, class NoThrowForwardIterator>
    void uninitialized_default_construct(ExecutionPolicy&& exec,        // see [algorithms.parallel.overloads]
                                         NoThrowForwardIterator first,
                                         NoThrowForwardIterator last);
  template<class NoThrowForwardIterator, class Size>
    NoThrowForwardIterator
      uninitialized_default_construct_n(NoThrowForwardIterator first, Size n);
  template<class ExecutionPolicy, class NoThrowForwardIterator, class Size>
    NoThrowForwardIterator
      uninitialized_default_construct_n(ExecutionPolicy&& exec,         // see [algorithms.parallel.overloads]
                                        NoThrowForwardIterator first, Size n);

  namespace ranges {
    template<no-throw-forward-iterator I, no-throw-sentinel<I> S>
      requires default_initializable<iter_value_t<I>>
        I uninitialized_default_construct(I first, S last);
    template<no-throw-forward-range R>
      requires default_initializable<range_value_t<R>>
        borrowed_iterator_t<R> uninitialized_default_construct(R&& r);

    template<no-throw-forward-iterator I>
      requires default_initializable<iter_value_t<I>>
        I uninitialized_default_construct_n(I first, iter_difference_t<I> n);
  }

  template<class NoThrowForwardIterator>
    void uninitialized_value_construct(NoThrowForwardIterator first,
                                       NoThrowForwardIterator last);
  template<class ExecutionPolicy, class NoThrowForwardIterator>
    void uninitialized_value_construct(ExecutionPolicy&& exec,  // see [algorithms.parallel.overloads]
                                       NoThrowForwardIterator first,
                                       NoThrowForwardIterator last);
  template<class NoThrowForwardIterator, class Size>
    NoThrowForwardIterator
      uninitialized_value_construct_n(NoThrowForwardIterator first, Size n);
  template<class ExecutionPolicy, class NoThrowForwardIterator, class Size>
    NoThrowForwardIterator
      uninitialized_value_construct_n(ExecutionPolicy&& exec,   // see [algorithms.parallel.overloads]
                                      NoThrowForwardIterator first, Size n);

  namespace ranges {
    template<no-throw-forward-iterator I, no-throw-sentinel<I> S>
      requires default_initializable<iter_value_t<I>>
        I uninitialized_value_construct(I first, S last);
    template<no-throw-forward-range R>
      requires default_initializable<range_value_t<R>>
        borrowed_iterator_t<R> uninitialized_value_construct(R&& r);

    template<no-throw-forward-iterator I>
      requires default_initializable<iter_value_t<I>>
        I uninitialized_value_construct_n(I first, iter_difference_t<I> n);
  }

  template<class InputIterator, class NoThrowForwardIterator>
    NoThrowForwardIterator uninitialized_copy(InputIterator first, InputIterator last,
                                              NoThrowForwardIterator result);
  template<class ExecutionPolicy, class InputIterator, class NoThrowForwardIterator>
    NoThrowForwardIterator uninitialized_copy(ExecutionPolicy&& exec,   // see [algorithms.parallel.overloads]
                                              InputIterator first, InputIterator last,
                                              NoThrowForwardIterator result);
  template<class InputIterator, class Size, class NoThrowForwardIterator>
    NoThrowForwardIterator uninitialized_copy_n(InputIterator first, Size n,
                                                NoThrowForwardIterator result);
  template<class ExecutionPolicy, class InputIterator, class Size, class NoThrowForwardIterator>
    NoThrowForwardIterator uninitialized_copy_n(ExecutionPolicy&& exec, // see [algorithms.parallel.overloads]
                                                InputIterator first, Size n,
                                                NoThrowForwardIterator result);

  namespace ranges {
    template<class I, class O>
      using uninitialized_copy_result = in_out_result<I, O>;
    template<input_iterator I, sentinel_for<I> S1,
             no-throw-forward-iterator O, no-throw-sentinel<O> S2>
      requires constructible_from<iter_value_t<O>, iter_reference_t<I>>
        uninitialized_copy_result<I, O>
          uninitialized_copy(I ifirst, S1 ilast, O ofirst, S2 olast);
    template<input_range IR, no-throw-forward-range OR>
      requires constructible_from<range_value_t<OR>, range_reference_t<IR>>
        uninitialized_copy_result<borrowed_iterator_t<IR>, borrowed_iterator_t<OR>>
          uninitialized_copy(IR&& in_range, OR&& out_range);

    template<class I, class O>
      using uninitialized_copy_n_result = in_out_result<I, O>;
    template<input_iterator I, no-throw-forward-iterator O, no-throw-sentinel<O> S>
      requires constructible_from<iter_value_t<O>, iter_reference_t<I>>
        uninitialized_copy_n_result<I, O>
          uninitialized_copy_n(I ifirst, iter_difference_t<I> n, O ofirst, S olast);
  }

  template<class InputIterator, class NoThrowForwardIterator>
    NoThrowForwardIterator uninitialized_move(InputIterator first, InputIterator last,
                                              NoThrowForwardIterator result);
  template<class ExecutionPolicy, class InputIterator, class NoThrowForwardIterator>
    NoThrowForwardIterator uninitialized_move(ExecutionPolicy&& exec,   // see [algorithms.parallel.overloads]
                                              InputIterator first, InputIterator last,
                                              NoThrowForwardIterator result);
  template<class InputIterator, class Size, class NoThrowForwardIterator>
    pair<InputIterator, NoThrowForwardIterator>
      uninitialized_move_n(InputIterator first, Size n, NoThrowForwardIterator result);
  template<class ExecutionPolicy, class InputIterator, class Size, class NoThrowForwardIterator>
    pair<InputIterator, NoThrowForwardIterator>
      uninitialized_move_n(ExecutionPolicy&& exec,              // see [algorithms.parallel.overloads]
                           InputIterator first, Size n, NoThrowForwardIterator result);

  namespace ranges {
    template<class I, class O>
      using uninitialized_move_result = in_out_result<I, O>;
    template<input_iterator I, sentinel_for<I> S1,
             no-throw-forward-iterator O, no-throw-sentinel<O> S2>
      requires constructible_from<iter_value_t<O>, iter_rvalue_reference_t<I>>
        uninitialized_move_result<I, O>
          uninitialized_move(I ifirst, S1 ilast, O ofirst, S2 olast);
    template<input_range IR, no-throw-forward-range OR>
      requires constructible_from<range_value_t<OR>, range_rvalue_reference_t<IR>>
        uninitialized_move_result<borrowed_iterator_t<IR>, borrowed_iterator_t<OR>>
          uninitialized_move(IR&& in_range, OR&& out_range);

    template<class I, class O>
      using uninitialized_move_n_result = in_out_result<I, O>;
    template<input_iterator I,
             no-throw-forward-iterator O, no-throw-sentinel<O> S>
      requires constructible_from<iter_value_t<O>, iter_rvalue_reference_t<I>>
        uninitialized_move_n_result<I, O>
          uninitialized_move_n(I ifirst, iter_difference_t<I> n, O ofirst, S olast);
  }

  template<class NoThrowForwardIterator, class T>
    void uninitialized_fill(NoThrowForwardIterator first, NoThrowForwardIterator last,
                            const T& x);
  template<class ExecutionPolicy, class NoThrowForwardIterator, class T>
    void uninitialized_fill(ExecutionPolicy&& exec,             // see [algorithms.parallel.overloads]
                            NoThrowForwardIterator first, NoThrowForwardIterator last,
                            const T& x);
  template<class NoThrowForwardIterator, class Size, class T>
    NoThrowForwardIterator
      uninitialized_fill_n(NoThrowForwardIterator first, Size n, const T& x);
  template<class ExecutionPolicy, class NoThrowForwardIterator, class Size, class T>
    NoThrowForwardIterator
      uninitialized_fill_n(ExecutionPolicy&& exec,              // see [algorithms.parallel.overloads]
                           NoThrowForwardIterator first, Size n, const T& x);

  namespace ranges {
    template<no-throw-forward-iterator I, no-throw-sentinel<I> S, class T>
      requires constructible_from<iter_value_t<I>, const T&>
        I uninitialized_fill(I first, S last, const T& x);
    template<no-throw-forward-range R, class T>
      requires constructible_from<range_value_t<R>, const T&>
        borrowed_iterator_t<R> uninitialized_fill(R&& r, const T& x);

    template<no-throw-forward-iterator I, class T>
      requires constructible_from<iter_value_t<I>, const T&>
        I uninitialized_fill_n(I first, iter_difference_t<I> n, const T& x);
  }

  // [specialized.construct], construct_at
  template<class T, class... Args>
    constexpr T* construct_at(T* location, Args&&... args);

  namespace ranges {
    template<class T, class... Args>
      constexpr T* construct_at(T* location, Args&&... args);
  }

  // [specialized.destroy], destroy
  template<class T>
    constexpr void destroy_at(T* location);
  template<class NoThrowForwardIterator>
    constexpr void destroy(NoThrowForwardIterator first, NoThrowForwardIterator last);
  template<class ExecutionPolicy, class NoThrowForwardIterator>
    void destroy(ExecutionPolicy&& exec,                        // see [algorithms.parallel.overloads]
                 NoThrowForwardIterator first, NoThrowForwardIterator last);
  template<class NoThrowForwardIterator, class Size>
    constexpr NoThrowForwardIterator destroy_n(NoThrowForwardIterator first, Size n);
  template<class ExecutionPolicy, class NoThrowForwardIterator, class Size>
    NoThrowForwardIterator destroy_n(ExecutionPolicy&& exec,    // see [algorithms.parallel.overloads]
                                     NoThrowForwardIterator first, Size n);

  namespace ranges {
    template<destructible T>
      constexpr void destroy_at(T* location) noexcept;

    template<no-throw-input-iterator I, no-throw-sentinel<I> S>
      requires destructible<iter_value_t<I>>
        constexpr I destroy(I first, S last) noexcept;
    template<no-throw-input-range R>
      requires destructible<range_value_t<R>>
        constexpr borrowed_iterator_t<R> destroy(R&& r) noexcept;

    template<no-throw-input-iterator I>
      requires destructible<iter_value_t<I>>
        constexpr I destroy_n(I first, iter_difference_t<I> n) noexcept;
  }

  // [unique.ptr], class template unique_ptr
  template<class T> struct default_delete;
  template<class T> struct default_delete<T[]>;
  template<class T, class D = default_delete<T>> class unique_ptr;
  template<class T, class D> class unique_ptr<T[], D>;

  template<class T, class... Args>
    unique_ptr<T> make_unique(Args&&... args);                                  // T is not array
  template<class T>
    unique_ptr<T> make_unique(size_t n);                                        // T is U[]
  template<class T, class... Args>
    unspecified make_unique(Args&&...) = delete;                                // T is U[N]

  template<class T>
    unique_ptr<T> make_unique_for_overwrite();                                  // T is not array
  template<class T>
    unique_ptr<T> make_unique_for_overwrite(size_t n);                          // T is U[]
  template<class T, class... Args>
    unspecified make_unique_for_overwrite(Args&&...) = delete;                  // T is U[N]

  template<class T, class D>
    void swap(unique_ptr<T, D>& x, unique_ptr<T, D>& y) noexcept;

  template<class T1, class D1, class T2, class D2>
    bool operator==(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);
  template<class T1, class D1, class T2, class D2>
    bool operator<(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);
  template<class T1, class D1, class T2, class D2>
    bool operator>(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);
  template<class T1, class D1, class T2, class D2>
    bool operator<=(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);
  template<class T1, class D1, class T2, class D2>
    bool operator>=(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);
  template<class T1, class D1, class T2, class D2>
    requires three_way_comparable_with<typename unique_ptr<T1, D1>::pointer,
                                       typename unique_ptr<T2, D2>::pointer>
    compare_three_way_result_t<typename unique_ptr<T1, D1>::pointer,
                               typename unique_ptr<T2, D2>::pointer>
      operator<=>(const unique_ptr<T1, D1>& x, const unique_ptr<T2, D2>& y);

  template<class T, class D>
    bool operator==(const unique_ptr<T, D>& x, nullptr_t) noexcept;
  template<class T, class D>
    bool operator<(const unique_ptr<T, D>& x, nullptr_t);
  template<class T, class D>
    bool operator<(nullptr_t, const unique_ptr<T, D>& y);
  template<class T, class D>
    bool operator>(const unique_ptr<T, D>& x, nullptr_t);
  template<class T, class D>
    bool operator>(nullptr_t, const unique_ptr<T, D>& y);
  template<class T, class D>
    bool operator<=(const unique_ptr<T, D>& x, nullptr_t);
  template<class T, class D>
    bool operator<=(nullptr_t, const unique_ptr<T, D>& y);
  template<class T, class D>
    bool operator>=(const unique_ptr<T, D>& x, nullptr_t);
  template<class T, class D>
    bool operator>=(nullptr_t, const unique_ptr<T, D>& y);
  template<class T, class D>
    requires three_way_comparable_with<typename unique_ptr<T, D>::pointer, nullptr_t>
    compare_three_way_result_t<typename unique_ptr<T, D>::pointer, nullptr_t>
      operator<=>(const unique_ptr<T, D>& x, nullptr_t);

  template<class E, class T, class Y, class D>
    basic_ostream<E, T>& operator<<(basic_ostream<E, T>& os, const unique_ptr<Y, D>& p);

  // [util.smartptr.weak.bad], class bad_weak_ptr
  class bad_weak_ptr;

  // [util.smartptr.shared], class template shared_ptr
  template<class T> class shared_ptr;

  // [util.smartptr.shared.create], shared_ptr creation
  template<class T, class... Args>
    shared_ptr<T> make_shared(Args&&... args);                                  // T is not array
  template<class T, class A, class... Args>
    shared_ptr<T> allocate_shared(const A& a, Args&&... args);                  // T is not array

  template<class T>
    shared_ptr<T> make_shared(size_t N);                                        // T is U[]
  template<class T, class A>
    shared_ptr<T> allocate_shared(const A& a, size_t N);                        // T is U[]

  template<class T>
    shared_ptr<T> make_shared();                                                // T is U[N]
  template<class T, class A>
    shared_ptr<T> allocate_shared(const A& a);                                  // T is U[N]

  template<class T>
    shared_ptr<T> make_shared(size_t N, const remove_extent_t<T>& u);           // T is U[]
  template<class T, class A>
    shared_ptr<T> allocate_shared(const A& a, size_t N,
                                  const remove_extent_t<T>& u);                 // T is U[]

  template<class T>
    shared_ptr<T> make_shared(const remove_extent_t<T>& u);                     // T is U[N]
  template<class T, class A>
    shared_ptr<T> allocate_shared(const A& a, const remove_extent_t<T>& u);     // T is U[N]

  template<class T>
    shared_ptr<T> make_shared_for_overwrite();                                  // T is not U[]
  template<class T, class A>
    shared_ptr<T> allocate_shared_for_overwrite(const A& a);                    // T is not U[]

  template<class T>
    shared_ptr<T> make_shared_for_overwrite(size_t N);                          // T is U[]
  template<class T, class A>
    shared_ptr<T> allocate_shared_for_overwrite(const A& a, size_t N);          // T is U[]

  // [util.smartptr.shared.cmp], shared_ptr comparisons
  template<class T, class U>
    bool operator==(const shared_ptr<T>& a, const shared_ptr<U>& b) noexcept;
  template<class T, class U>
    strong_ordering operator<=>(const shared_ptr<T>& a, const shared_ptr<U>& b) noexcept;

  template<class T>
    bool operator==(const shared_ptr<T>& x, nullptr_t) noexcept;
  template<class T>
    strong_ordering operator<=>(const shared_ptr<T>& x, nullptr_t) noexcept;

  // [util.smartptr.shared.spec], shared_ptr specialized algorithms
  template<class T>
    void swap(shared_ptr<T>& a, shared_ptr<T>& b) noexcept;

  // [util.smartptr.shared.cast], shared_ptr casts
  template<class T, class U>
    shared_ptr<T> static_pointer_cast(const shared_ptr<U>& r) noexcept;
  template<class T, class U>
    shared_ptr<T> static_pointer_cast(shared_ptr<U>&& r) noexcept;
  template<class T, class U>
    shared_ptr<T> dynamic_pointer_cast(const shared_ptr<U>& r) noexcept;
  template<class T, class U>
    shared_ptr<T> dynamic_pointer_cast(shared_ptr<U>&& r) noexcept;
  template<class T, class U>
    shared_ptr<T> const_pointer_cast(const shared_ptr<U>& r) noexcept;
  template<class T, class U>
    shared_ptr<T> const_pointer_cast(shared_ptr<U>&& r) noexcept;
  template<class T, class U>
    shared_ptr<T> reinterpret_pointer_cast(const shared_ptr<U>& r) noexcept;
  template<class T, class U>
    shared_ptr<T> reinterpret_pointer_cast(shared_ptr<U>&& r) noexcept;

  // [util.smartptr.getdeleter], shared_ptr get_deleter
  template<class D, class T>
    D* get_deleter(const shared_ptr<T>& p) noexcept;

  // [util.smartptr.shared.io], shared_ptr I/O
  template<class E, class T, class Y>
    basic_ostream<E, T>& operator<<(basic_ostream<E, T>& os, const shared_ptr<Y>& p);

  // [util.smartptr.weak], class template weak_ptr
  template<class T> class weak_ptr;

  // [util.smartptr.weak.spec], weak_ptr specialized algorithms
  template<class T> void swap(weak_ptr<T>& a, weak_ptr<T>& b) noexcept;

  // [util.smartptr.ownerless], class template owner_less
  template<class T = void> struct owner_less;

  // [util.smartptr.enab], class template enable_shared_from_this
  template<class T> class enable_shared_from_this;

  // [util.smartptr.hash], hash support
  template<class T> struct hash;
  template<class T, class D> struct hash<unique_ptr<T, D>>;
  template<class T> struct hash<shared_ptr<T>>;

  // [util.smartptr.atomic], atomic smart pointers
  template<class T> struct atomic;
  template<class T> struct atomic<shared_ptr<T>>;
  template<class T> struct atomic<weak_ptr<T>>;
}
```

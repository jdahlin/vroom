# 20.14.1 Header `<functional>` synopsis [functional.syn]

[Contents](../../README.md) · up: [20.14 Function objects \[function.objects\]](function.objects.md) · prev: [20.14 Function objects \[function.objects\]](function.objects.md) · next: [20.14.2 Definitions \[func.def\]](func.def.md)

```cpp
namespace std {
  // [func.invoke], invoke
  template<class F, class... Args>
    constexpr invoke_result_t<F, Args...> invoke(F&& f, Args&&... args)
      noexcept(is_nothrow_invocable_v<F, Args...>);

  // [refwrap], reference_wrapper
  template<class T> class reference_wrapper;

  template<class T> constexpr reference_wrapper<T> ref(T&) noexcept;
  template<class T> constexpr reference_wrapper<const T> cref(const T&) noexcept;
  template<class T> void ref(const T&&) = delete;
  template<class T> void cref(const T&&) = delete;

  template<class T> constexpr reference_wrapper<T> ref(reference_wrapper<T>) noexcept;
  template<class T> constexpr reference_wrapper<const T> cref(reference_wrapper<T>) noexcept;

  // [arithmetic.operations], arithmetic operations
  template<class T = void> struct plus;
  template<class T = void> struct minus;
  template<class T = void> struct multiplies;
  template<class T = void> struct divides;
  template<class T = void> struct modulus;
  template<class T = void> struct negate;
  template<> struct plus<void>;
  template<> struct minus<void>;
  template<> struct multiplies<void>;
  template<> struct divides<void>;
  template<> struct modulus<void>;
  template<> struct negate<void>;

  // [comparisons], comparisons
  template<class T = void> struct equal_to;
  template<class T = void> struct not_equal_to;
  template<class T = void> struct greater;
  template<class T = void> struct less;
  template<class T = void> struct greater_equal;
  template<class T = void> struct less_equal;
  template<> struct equal_to<void>;
  template<> struct not_equal_to<void>;
  template<> struct greater<void>;
  template<> struct less<void>;
  template<> struct greater_equal<void>;
  template<> struct less_equal<void>;

  // [comparisons.three.way], class compare_three_way
  struct compare_three_way;

  // [logical.operations], logical operations
  template<class T = void> struct logical_and;
  template<class T = void> struct logical_or;
  template<class T = void> struct logical_not;
  template<> struct logical_and<void>;
  template<> struct logical_or<void>;
  template<> struct logical_not<void>;

  // [bitwise.operations], bitwise operations
  template<class T = void> struct bit_and;
  template<class T = void> struct bit_or;
  template<class T = void> struct bit_xor;
  template<class T = void> struct bit_not;
  template<> struct bit_and<void>;
  template<> struct bit_or<void>;
  template<> struct bit_xor<void>;
  template<> struct bit_not<void>;

  // [func.identity], identity
  struct identity;

  // [func.not.fn], function template not_fn
  template<class F> constexpr unspecified not_fn(F&& f);

  // [func.bind.front], function template bind_front
  template<class F, class... Args> constexpr unspecified bind_front(F&&, Args&&...);

  // [func.bind], bind
  template<class T> struct is_bind_expression;
  template<class T>
    inline constexpr bool is_bind_expression_v = is_bind_expression<T>::value;
  template<class T> struct is_placeholder;
  template<class T>
    inline constexpr int is_placeholder_v = is_placeholder<T>::value;

  template<class F, class... BoundArgs>
    constexpr unspecified bind(F&&, BoundArgs&&...);
  template<class R, class F, class... BoundArgs>
    constexpr unspecified bind(F&&, BoundArgs&&...);

  namespace placeholders {
    // M is the implementation-defined number of placeholders
    see below _1;
    see below _2;
               .
               .
               .
    see below _M;
  }

  // [func.memfn], member function adaptors
  template<class R, class T>
    constexpr unspecified mem_fn(R T::*) noexcept;

  // [func.wrap], polymorphic function wrappers
  class bad_function_call;

  template<class> class function;       // not defined
  template<class R, class... ArgTypes> class function<R(ArgTypes...)>;

  template<class R, class... ArgTypes>
    void swap(function<R(ArgTypes...)>&, function<R(ArgTypes...)>&) noexcept;

  template<class R, class... ArgTypes>
    bool operator==(const function<R(ArgTypes...)>&, nullptr_t) noexcept;

  // [func.search], searchers
  template<class ForwardIterator, class BinaryPredicate = equal_to<>>
    class default_searcher;

  template<class RandomAccessIterator,
           class Hash = hash<typename iterator_traits<RandomAccessIterator>::value_type>,
           class BinaryPredicate = equal_to<>>
    class boyer_moore_searcher;

  template<class RandomAccessIterator,
           class Hash = hash<typename iterator_traits<RandomAccessIterator>::value_type>,
           class BinaryPredicate = equal_to<>>
    class boyer_moore_horspool_searcher;

  // [unord.hash], class template hash
  template<class T>
    struct hash;

  namespace ranges {
    // [range.cmp], concept-constrained comparisons
    struct equal_to;
    struct not_equal_to;
    struct greater;
    struct less;
    struct greater_equal;
    struct less_equal;
  }
}
```

**1** [*Example 1*: If a C++ program wants to have a by-element addition of two vectors `a` and `b` containing `double` and put the result into `a`, it can do:

```cpp
transform(a.begin(), a.end(), b.begin(), a.begin(), plus<double>());
```

— *end example*]

**2** [*Example 2*: To negate every element of `a`:

```cpp
transform(a.begin(), a.end(), a.begin(), negate<double>());
```

— *end example*]

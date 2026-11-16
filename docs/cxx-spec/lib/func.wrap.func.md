# 20.14.16.2 Class template `function` [func.wrap.func]

[Contents](../README.md) · up: [20.14.16 Polymorphic function wrappers \[func.wrap\]](func.wrap.md) · prev: [20.14.16.1 Class `bad_function_call` \[func.wrap.badcall\]](func.wrap.badcall.md) · next: [20.14.16.2.1 Constructors and destructor \[func.wrap.func.con\]](func.wrap.func.con.md)

```cpp
namespace std {
  template<class> class function;       // not defined

  template<class R, class... ArgTypes>
  class function<R(ArgTypes...)> {
  public:
    using result_type = R;

    // [func.wrap.func.con], construct/copy/destroy
    function() noexcept;
    function(nullptr_t) noexcept;
    function(const function&);
    function(function&&) noexcept;
    template<class F> function(F);

    function& operator=(const function&);
    function& operator=(function&&);
    function& operator=(nullptr_t) noexcept;
    template<class F> function& operator=(F&&);
    template<class F> function& operator=(reference_wrapper<F>) noexcept;

    ~function();

    // [func.wrap.func.mod], function modifiers
    void swap(function&) noexcept;

    // [func.wrap.func.cap], function capacity
    explicit operator bool() const noexcept;

    // [func.wrap.func.inv], function invocation
    R operator()(ArgTypes...) const;

    // [func.wrap.func.targ], function target access
    const type_info& target_type() const noexcept;
    template<class T>       T* target() noexcept;
    template<class T> const T* target() const noexcept;
  };

  template<class R, class... ArgTypes>
    function(R(*)(ArgTypes...)) -> function<R(ArgTypes...)>;

  template<class F> function(F) -> function<see below>;

  // [func.wrap.func.nullptr], null pointer comparison functions
  template<class R, class... ArgTypes>
    bool operator==(const function<R(ArgTypes...)>&, nullptr_t) noexcept;

  // [func.wrap.func.alg], specialized algorithms
  template<class R, class... ArgTypes>
    void swap(function<R(ArgTypes...)>&, function<R(ArgTypes...)>&) noexcept;
}
```

**1** The `function` class template provides polymorphic wrappers that generalize the notion of a function pointer. Wrappers can store, copy, and call arbitrary callable objects ([[func.def]](func.def.md)), given a call signature ([[func.def]](func.def.md)), allowing functions to be first-class objects.

**2** A callable type ([[func.def]](func.def.md)) `F` is *Lvalue-Callable* for argument types `ArgTypes` and return type `R` if the expression `INVOKE<R>(declval<F&>(), declval<ArgTypes>()...)`, considered as an unevaluated operand ([[expr.prop]](../expr.prop.md)), is well-formed ([[func.require]](func.require.md)).

**3** The `function` class template is a call wrapper ([[func.def]](func.def.md)) whose call signature ([[func.def]](func.def.md)) is `R(ArgTypes...)`.

**4** [*Note 1*: The types deduced by the deduction guides for `function` may change in future versions of this International Standard. — *end note*]

## Subclauses

- [20.14.16.2.1 Constructors and destructor \[func.wrap.func.con\]](func.wrap.func.con.md)
- [20.14.16.2.2 Modifiers \[func.wrap.func.mod\]](func.wrap.func.mod.md)
- [20.14.16.2.3 Capacity \[func.wrap.func.cap\]](func.wrap.func.cap.md)
- [20.14.16.2.4 Invocation \[func.wrap.func.inv\]](func.wrap.func.inv.md)
- [20.14.16.2.5 Target access \[func.wrap.func.targ\]](func.wrap.func.targ.md)
- [20.14.16.2.6 Null pointer comparison functions \[func.wrap.func.nullptr\]](func.wrap.func.nullptr.md)
- [20.14.16.2.7 Specialized algorithms \[func.wrap.func.alg\]](func.wrap.func.alg.md)

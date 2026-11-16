# 20.14.14.3 Function template `bind` [func.bind.bind]

[Contents](../README.md) · up: [20.14.14 Function object binders \[func.bind\]](func.bind.md) · prev: [20.14.14.2 Class template `is_placeholder` \[func.bind.isplace\]](func.bind.isplace.md) · next: [20.14.14.4 Placeholders \[func.bind.place\]](func.bind.place.md)

**1** In the text that follows:

- `g` is a value of the result of a `bind` invocation,
- `FD` is the type `decay_t<F>`,
- `fd` is an lvalue that is a target object of `g` ([[func.def]](func.def.md)) of type `FD` direct-non-list-initialized with `std::forward<F>(f)`,
- `T`<sub>*i*</sub> is the *i*<sup></sup>*th* type in the template parameter pack `BoundArgs`,
- `TD`<sub>*i*</sub> is the type `decay_t<T_i>`,
- `t`<sub>*i*</sub> is the *i*<sup></sup>*th* argument in the function parameter pack `bound_args`,
- `td`<sub>*i*</sub> is a bound argument entity of `g` ([[func.def]](func.def.md)) of type `TD`<sub>*i*</sub> direct-non-list-initialized with `std::forward<T_i>(t_i)`,
- `U`<sub>*j*</sub> is the *j*<sup></sup>*th* deduced type of the `UnBoundArgs&&...` parameter of the argument forwarding call wrapper, and
- `u`<sub>*j*</sub> is the *j*<sup></sup>*th* argument associated with `U`<sub>*j*</sub>.

```cpp
template<class F, class... BoundArgs>
  constexpr unspecified bind(F&& f, BoundArgs&&... bound_args);
template<class R, class F, class... BoundArgs>
  constexpr unspecified bind(F&& f, BoundArgs&&... bound_args);
```

**2** *Mandates:* `is_constructible_v<FD, F>` is `true`. For each `T`<sub>*i*</sub> in `BoundArgs`, `is_constructible_v<TD_i, T_i>` is `true`.

**3** *Preconditions:* `FD` and each `TD`<sub>*i*</sub> meet the *Cpp17MoveConstructible* and *Cpp17Destructible* requirements. `INVOKE(fd, w_1, w_2, ..., w_N)` ([[func.require]](func.require.md)) is a valid expression for some values `w`<sub>1</sub>, `w`<sub>2</sub>, …, `w`<sub>*N*</sub>, where *N* has the value `sizeof...(bound_args)`.

**4** *Returns:* An argument forwarding call wrapper `g` ([[func.require]](func.require.md)). A program that attempts to invoke a volatile-qualified `g` is ill-formed. When `g` is not volatile-qualified, invocation of `g(u_1, u_2, ..., u_M)` is expression-equivalent ([[defns.expression-equivalent]](definitions.md#defns.expression-equivalent)) to

```cpp
INVOKE(static_cast<V_()fd>(v_()fd),
       static_cast<V_1>(v_1), static_cast<V_2>(v_2), ..., static_cast<V_N>(v_N))
```

for the first overload, and

```cpp
INVOKE<R>(static_cast<V_()fd>(v_()fd),
          static_cast<V_1>(v_1), static_cast<V_2>(v_2), ..., static_cast<V_N>(v_N))
```

for the second overload, where the values and types of the target argument `v`<sub></sub>*fd* and of the bound arguments `v`<sub>1</sub>, `v`<sub>2</sub>, …, `v`<sub>*N*</sub> are determined as specified below.

**5** *Throws:* Any exception thrown by the initialization of the state entities of `g`.

**6** [*Note 1*: If all of `FD` and `TD`<sub>*i*</sub> meet the requirements of *Cpp17CopyConstructible*, then the return type meets the requirements of *Cpp17CopyConstructible*. — *end note*]

**7** The values of the *bound arguments* `v`<sub>1</sub>, `v`<sub>2</sub>, …, `v`<sub>*N*</sub> and their corresponding types `V`<sub>1</sub>, `V`<sub>2</sub>, …, `V`<sub>*N*</sub> depend on the types `TD`<sub>*i*</sub> derived from the call to `bind` and the cv-qualifiers *cv* of the call wrapper `g` as follows:

- if `TD`<sub>*i*</sub> is `reference_wrapper<T>`, the argument is `td_i.get()` and its type `V`<sub>*i*</sub> is `T&`;

- if the value of `is_bind_expression_v<TD_i>` is `true`, the argument is

  ```cpp
  static_cast<cv TD_i&>(td_i)(std::forward<U_j>(u_j)...)
  ```

  and its type `V`<sub>*i*</sub> is `invoke_result_t<cv TD_i&, U_j...>&&`;

- if the value `j` of `is_placeholder_v<TD_i>` is not zero, the argument is `std::forward<U_j>(u_j)` and its type `V`<sub>*i*</sub> is `U_j&&`;

- otherwise, the value is `td`<sub>*i*</sub> and its type `V`<sub>*i*</sub> is `cv TD_i&`.

**8** The value of the target argument `v`<sub></sub>*fd* is `fd` and its corresponding type `V`<sub></sub>*fd* is `cv FD&`.

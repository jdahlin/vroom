# 20.8.4 Non-member functions [any.nonmembers]

[Contents](../README.md) · up: [20.8 Storage for any type \[any\]](any.md) · prev: [20.8.3.4 Observers \[any.observers\]](any.observers.md) · next: [20.9 Bitsets \[bitset\]](bitset.md)

```cpp
void swap(any& x, any& y) noexcept;
```

**1** *Effects:* Equivalent to `x.swap(y)`.

```cpp
template<class T, class... Args>
  any make_any(Args&&... args);
```

**2** *Effects:* Equivalent to: `return any(in_place_type<T>, std::forward<Args>(args)...);`

```cpp
template<class T, class U, class... Args>
  any make_any(initializer_list<U> il, Args&&... args);
```

**3** *Effects:* Equivalent to: `return any(in_place_type<T>, il, std::forward<Args>(args)...);`

```cpp
template<class T>
  T any_cast(const any& operand);
template<class T>
  T any_cast(any& operand);
template<class T>
  T any_cast(any&& operand);
```

**4** Let `U` be the type `remove_cvref_t<T>`.

**5** *Mandates:* For the first overload, `is_constructible_v<T, const U&>` is `true`. For the second overload, `is_constructible_v<T, U&>` is `true`. For the third overload, `is_constructible_v<T, U>` is `true`.

**6** *Returns:* For the first and second overload, `static_cast<T>(*any_cast<U>(&operand))`. For the third overload, `static_cast<T>(std::move(*any_cast<U>(&operand)))`.

**7** *Throws:* `bad_any_cast` if `operand.type() != typeid(remove_reference_t<T>)`.

**8** [*Example 1*:

```cpp
any x(5);                                   // x holds int
assert(any_cast<int>(x) == 5);              // cast to value
any_cast<int&>(x) = 10;                     // cast to reference
assert(any_cast<int>(x) == 10);

x = "Meow";                                 // x holds const char*
assert(strcmp(any_cast<const char*>(x), "Meow") == 0);
any_cast<const char*&>(x) = "Harry";
assert(strcmp(any_cast<const char*>(x), "Harry") == 0);

x = string("Meow");                         // x holds string
string s, s2("Jane");
s = move(any_cast<string&>(x));             // move from any
assert(s == "Meow");
any_cast<string&>(x) = move(s2);            // move to any
assert(any_cast<const string&>(x) == "Jane");

string cat("Meow");
const any y(cat);                           // const y holds string
assert(any_cast<const string&>(y) == cat);

any_cast<string&>(y);                       // error: cannot any_cast away const
```

— *end example*]

```cpp
template<class T>
  const T* any_cast(const any* operand) noexcept;
template<class T>
  T* any_cast(any* operand) noexcept;
```

**9** *Returns:* If `operand != nullptr && operand->type() == typeid(T)`, a pointer to the object contained by `operand`; otherwise, `nullptr`.

**10** [*Example 2*:

```cpp
bool is_string(const any& operand) {
  return any_cast<string>(&operand) != nullptr;
}
```

— *end example*]

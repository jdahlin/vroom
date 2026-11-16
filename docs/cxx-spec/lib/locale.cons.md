# 28.3.1.2 Constructors and destructor [locale.cons]

[Contents](../README.md) · up: [28.3.1 Class `locale` \[locale\]](locale.md) · prev: [28.3.1.1.3 Class `locale::id` \[locale.id\]](locale.id.md) · next: [28.3.1.3 Members \[locale.members\]](locale.members.md)

```cpp
locale() noexcept;
```

**1** *Effects:* Constructs a copy of the argument last passed to `locale::global(locale&)`, if it has been called; else, the resulting facets have virtual function semantics identical to those of `locale::classic()`. [*Note 1*: This constructor yields a copy of the current global locale. It is commonly used as a default argument for function parameters of type `const locale&`. — *end note*]

```cpp
explicit locale(const char* std_name);
```

**2** *Effects:* Constructs a locale using standard C locale names, e.g., `"POSIX"`. The resulting locale implements semantics defined to be associated with that name.

**3** *Throws:* `runtime_error` if the argument is not valid, or is null.

**4** *Remarks:* The set of valid string argument values is `"C"`, `""`, and any implementation-defined values.

```cpp
explicit locale(const string& std_name);
```

**5** *Effects:* The same as `locale(std_name.c_str())`.

```cpp
locale(const locale& other, const char* std_name, category);
```

**6** *Effects:* Constructs a locale as a copy of `other` except for the facets identified by the `category` argument, which instead implement the same semantics as `locale(std_name)`.

**7** *Throws:* `runtime_error` if the argument is not valid, or is null.

**8** *Remarks:* The locale has a name if and only if `other` has a name.

```cpp
locale(const locale& other, const string& std_name, category cat);
```

**9** *Effects:* The same as `locale(other, std_name.c_str(), cat)`.

```cpp
template<class Facet> locale(const locale& other, Facet* f);
```

**10** *Effects:* Constructs a locale incorporating all facets from the first argument except that of type `Facet`, and installs the second argument as the remaining facet. If `f` is null, the resulting object is a copy of `other`.

**11** *Remarks:* The resulting locale has no name.

```cpp
locale(const locale& other, const locale& one, category cats);
```

**12** *Effects:* Constructs a locale incorporating all facets from the first argument except those that implement `cats`, which are instead incorporated from the second argument.

**13** *Remarks:* The resulting locale has a name if and only if the first two arguments have names.

```cpp
const locale& operator=(const locale& other) noexcept;
```

**14** *Effects:* Creates a copy of `other`, replacing the current value.

**15** *Returns:* `*this`.

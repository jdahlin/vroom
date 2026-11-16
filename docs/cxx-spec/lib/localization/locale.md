# 28.3.1 Class `locale` [locale]

[Contents](../../README.md) · up: [28.3 Locales \[locales\]](locales.md) · prev: [28.3 Locales \[locales\]](locales.md) · next: [28.3.1.1 Types \[locale.types\]](locale.types.md)

```cpp
namespace std {
  class locale {
  public:
    // types
    class facet;
    class id;
    using category = int;
    static const category   // values assigned here are for exposition only
      none     = 0,
      collate  = 0x010, ctype    = 0x020,
      monetary = 0x040, numeric  = 0x080,
      time     = 0x100, messages = 0x200,
      all = collate | ctype | monetary | numeric | time  | messages;

    // construct/copy/destroy
    locale() noexcept;
    locale(const locale& other) noexcept;
    explicit locale(const char* std_name);
    explicit locale(const string& std_name);
    locale(const locale& other, const char* std_name, category);
    locale(const locale& other, const string& std_name, category);
    template<class Facet> locale(const locale& other, Facet* f);
    locale(const locale& other, const locale& one, category);
    ~locale();                  // not virtual
    const locale& operator=(const locale& other) noexcept;
    template<class Facet> locale combine(const locale& other) const;

    // locale operations
    string name() const;

    bool operator==(const locale& other) const;

    template<class charT, class traits, class Allocator>
      bool operator()(const basic_string<charT, traits, Allocator>& s1,
                      const basic_string<charT, traits, Allocator>& s2) const;

    // global locale objects
    static       locale  global(const locale&);
    static const locale& classic();
  };
}
```

**1** Class `locale` implements a type-safe polymorphic set of facets, indexed by facet *type*. In other words, a facet has a dual role: in one sense, it’s just a class interface; at the same time, it’s an index into a locale’s set of facets.

**2** Access to the facets of a `locale` is via two function templates, `use_facet<>` and `has_facet<>`.

**3** [*Example 1*: An iostream `operator<<` might be implemented as:[^1]

```cpp
template<class charT, class traits>
basic_ostream<charT, traits>&
operator<< (basic_ostream<charT, traits>& s, Date d) {
  typename basic_ostream<charT, traits>::sentry cerberos(s);
  if (cerberos) {
    tm tmbuf; d.extract(tmbuf);
    bool failed =
      use_facet<time_put<charT, ostreambuf_iterator<charT, traits>>>(
        s.getloc()).put(s, s, s.fill(), &tmbuf, 'x').failed();
    if (failed)
      s.setstate(s.badbit);     // might throw
  }
  return s;
}
```

— *end example*]

**4** In the call to `use_facet<Facet>(loc)`, the type argument chooses a facet, making available all members of the named type. If `Facet` is not present in a locale, it throws the standard exception `bad_cast`. A C++ program can check if a locale implements a particular facet with the function template `has_facet<Facet>()`. User-defined facets may be installed in a locale, and used identically as may standard facets.

**5** [*Note 1*: All locale semantics are accessed via `use_facet<>` and `has_facet<>`, except that:

- A member operator template `operator()(const basic_string<C, T, A>&, const basic_string<C, T, A>&)` is provided so that a locale may be used as a predicate argument to the standard collections, to collate strings.
- Convenient global interfaces are provided for traditional `ctype` functions such as `isdigit()` and `isspace()`, so that given a locale object `loc` a C++ program can call `isspace(c, loc)`. (This eases upgrading existing extractors ([[istream.formatted]](../io/istream.formatted.md)).)

— *end note*]

**6** Once a facet reference is obtained from a locale object by calling `use_facet<>`, that reference remains usable, and the results from member functions of it may be cached and re-used, as long as some locale object refers to that facet.

**7** In successive calls to a locale facet member function on a facet object installed in the same locale, the returned result shall be identical.

**8** A `locale` constructed from a name string (such as `"POSIX"`), or from parts of two named locales, has a name; all others do not. Named locales may be compared for equality; an unnamed locale is equal only to (copies of) itself. For an unnamed locale, `locale::name()` returns the string `"*"`.

**9** Whether there is one global locale object for the entire program or one global locale object per thread is implementation-defined. Implementations should provide one global locale object per thread. If there is a single global locale object for the entire program, implementations are not required to avoid data races on it ([[res.on.data.races]](../library/res.on.data.races.md)).

## Subclauses

- [28.3.1.1 Types \[locale.types\]](locale.types.md)
  - [28.3.1.1.1 Type `locale::category` \[locale.category\]](locale.category.md)
  - [28.3.1.1.2 Class `locale::facet` \[locale.facet\]](locale.facet.md)
  - [28.3.1.1.3 Class `locale::id` \[locale.id\]](locale.id.md)
- [28.3.1.2 Constructors and destructor \[locale.cons\]](locale.cons.md)
- [28.3.1.3 Members \[locale.members\]](locale.members.md)
- [28.3.1.4 Operators \[locale.operators\]](locale.operators.md)
- [28.3.1.5 Static members \[locale.statics\]](locale.statics.md)

[^1]: Note that in the call to `put`, the stream is implicitly converted to an `ostreambuf_iterator<charT, traits>`.

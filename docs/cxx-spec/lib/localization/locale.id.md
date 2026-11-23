# 28.3.1.1.3 Class `locale::id` [locale.id]

[Contents](../../README.md) · up: [28.3.1.1 Types \[locale.types\]](locale.types.md) · prev: [28.3.1.1.2 Class `locale::facet` \[locale.facet\]](locale.facet.md) · next: [28.3.1.2 Constructors and destructor \[locale.cons\]](locale.cons.md)

```cpp
namespace std {
  class locale::id {
  public:
    id();
    void operator=(const id&) = delete;
    id(const id&) = delete;
  };
}
```

**1** The class `locale::id` provides identification of a locale facet interface, used as an index for lookup and to encapsulate initialization.

**2** [*Note 1*: Because facets are used by iostreams, potentially while static constructors are running, their initialization cannot depend on programmed static initialization. One initialization strategy is for `locale` to initialize each facet’s `id` member the first time an instance of the facet is installed into a locale. This depends only on static storage being zero before constructors run ([[basic.start.static]](../../basic/basic.start.static.md)). — *end note*]

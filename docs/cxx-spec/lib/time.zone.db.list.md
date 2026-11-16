# 27.11.2.2 Class `tzdb_list` [time.zone.db.list]

[Contents](../README.md) · up: [27.11.2 Time zone database \[time.zone.db\]](time.zone.db.md) · prev: [27.11.2.1 Class `tzdb` \[time.zone.db.tzdb\]](time.zone.db.tzdb.md) · next: [27.11.2.3 Time zone database access \[time.zone.db.access\]](time.zone.db.access.md)

```cpp
namespace std::chrono {
  class tzdb_list {
  public:
    tzdb_list(const tzdb_list&) = delete;
    tzdb_list& operator=(const tzdb_list&) = delete;

    // unspecified additional constructors

    class const_iterator;

    const tzdb& front() const noexcept;

    const_iterator erase_after(const_iterator p);

    const_iterator begin() const noexcept;
    const_iterator end()   const noexcept;

    const_iterator cbegin() const noexcept;
    const_iterator cend()   const noexcept;
  };
}
```

**1** The `tzdb_list` database is a singleton; the unique object of type `tzdb_list` can be accessed via the `get_tzdb_list()` function. [*Note 1*: This access is only needed for those applications that need to have long uptimes and have a need to update the time zone database while running. Other applications can implicitly access the `front()` of this list via the read-only namespace scope functions `get_tzdb()`, `locate_zone()`, and `current_zone()`. — *end note*] The `tzdb_list` object contains a list of `tzdb` objects.

**2** `tzdb_list::const_iterator` is a constant iterator which meets the *Cpp17ForwardIterator* requirements and has a value type of `tzdb`.

```cpp
const tzdb& front() const noexcept;
```

**3** *Synchronization:* This operation is thread-safe with respect to `reload_tzdb()`. [*Note 2*: `reload_tzdb()` pushes a new `tzdb` onto the front of this container. — *end note*]

**4** *Returns:* A reference to the first `tzdb` in the container.

```cpp
const_iterator erase_after(const_iterator p);
```

**5** *Preconditions:* The iterator following `p` is dereferenceable.

**6** *Effects:* Erases the `tzdb` referred to by the iterator following `p`.

**7** *Returns:* An iterator pointing to the element following the one that was erased, or `end()` if no such element exists.

**8** *Postconditions:* No pointers, references, or iterators are invalidated except those referring to the erased `tzdb`. [*Note 3*: It is not possible to erase the `tzdb` referred to by `begin()`. — *end note*]

**9** *Throws:* Nothing.

```cpp
const_iterator begin() const noexcept;
```

**10** *Returns:* An iterator referring to the first `tzdb` in the container.

```cpp
const_iterator end() const noexcept;
```

**11** *Returns:* An iterator referring to the position one past the last `tzdb` in the container.

```cpp
const_iterator cbegin() const noexcept;
```

**12** *Returns:* `begin()`.

```cpp
const_iterator cend() const noexcept;
```

**13** *Returns:* `end()`.

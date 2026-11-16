# 21.3.2 Class template `basic_string` [basic.string]

[Contents](../../README.md) · up: [21.3 String classes \[string.classes\]](string.classes.md) · prev: [21.3.1 Header `<string>` synopsis \[string.syn\]](string.syn.md) · next: [21.3.2.1 General requirements \[string.require\]](string.require.md)

**1** The class template `basic_string` describes objects that can store a sequence consisting of a varying number of arbitrary char-like objects with the first element of the sequence at position zero. Such a sequence is also called a “string” if the type of the char-like objects that it holds is clear from context. In the rest of this Clause, the type of the char-like objects held in a `basic_string` object is designated by `charT`.

**2** A specialization of `basic_string` is a contiguous container ([[container.requirements.general]](../containers/container.requirements.general.md)).

**3** In all cases, `[data(), data() + size()]` is a valid range, `data() + size()` points at an object with value `charT()` (a “null terminator”), and `size() <= capacity()` is `true`.

```cpp
namespace std {
  template<class charT, class traits = char_traits<charT>,
           class Allocator = allocator<charT>>
  class basic_string {
  public:
    // types
    using traits_type            = traits;
    using value_type             = charT;
    using allocator_type         = Allocator;
    using size_type              = typename allocator_traits<Allocator>::size_type;
    using difference_type        = typename allocator_traits<Allocator>::difference_type;
    using pointer                = typename allocator_traits<Allocator>::pointer;
    using const_pointer          = typename allocator_traits<Allocator>::const_pointer;
    using reference              = value_type&;
    using const_reference        = const value_type&;

    using iterator               = implementation-defined; // see [container.requirements]
    using const_iterator         = implementation-defined; // see [container.requirements]
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    static const size_type npos  = -1;

    // [string.cons], construct/copy/destroy
    constexpr basic_string() noexcept(noexcept(Allocator())) : basic_string(Allocator()) { }
    constexpr explicit basic_string(const Allocator& a) noexcept;
    constexpr basic_string(const basic_string& str);
    constexpr basic_string(basic_string&& str) noexcept;
    constexpr basic_string(const basic_string& str, size_type pos,
                           const Allocator& a = Allocator());
    constexpr basic_string(const basic_string& str, size_type pos, size_type n,
                           const Allocator& a = Allocator());
    template<class T>
      constexpr basic_string(const T& t, size_type pos, size_type n,
                             const Allocator& a = Allocator());
    template<class T>
      constexpr explicit basic_string(const T& t, const Allocator& a = Allocator());
    constexpr basic_string(const charT* s, size_type n, const Allocator& a = Allocator());
    constexpr basic_string(const charT* s, const Allocator& a = Allocator());
    constexpr basic_string(size_type n, charT c, const Allocator& a = Allocator());
    template<class InputIterator>
      constexpr basic_string(InputIterator begin, InputIterator end,
                             const Allocator& a = Allocator());
    constexpr basic_string(initializer_list<charT>, const Allocator& = Allocator());
    constexpr basic_string(const basic_string&, const Allocator&);
    constexpr basic_string(basic_string&&, const Allocator&);
    constexpr ~basic_string();

    constexpr basic_string& operator=(const basic_string& str);
    constexpr basic_string& operator=(basic_string&& str)
      noexcept(allocator_traits<Allocator>::propagate_on_container_move_assignment::value ||
               allocator_traits<Allocator>::is_always_equal::value);
    template<class T>
      constexpr basic_string& operator=(const T& t);
    constexpr basic_string& operator=(const charT* s);
    constexpr basic_string& operator=(charT c);
    constexpr basic_string& operator=(initializer_list<charT>);

    // [string.iterators], iterators
    constexpr iterator       begin() noexcept;
    constexpr const_iterator begin() const noexcept;
    constexpr iterator       end() noexcept;
    constexpr const_iterator end() const noexcept;

    constexpr reverse_iterator       rbegin() noexcept;
    constexpr const_reverse_iterator rbegin() const noexcept;
    constexpr reverse_iterator       rend() noexcept;
    constexpr const_reverse_iterator rend() const noexcept;

    constexpr const_iterator         cbegin() const noexcept;
    constexpr const_iterator         cend() const noexcept;
    constexpr const_reverse_iterator crbegin() const noexcept;
    constexpr const_reverse_iterator crend() const noexcept;

    // [string.capacity], capacity
    constexpr size_type size() const noexcept;
    constexpr size_type length() const noexcept;
    constexpr size_type max_size() const noexcept;
    constexpr void resize(size_type n, charT c);
    constexpr void resize(size_type n);
    constexpr size_type capacity() const noexcept;
    constexpr void reserve(size_type res_arg);
    constexpr void shrink_to_fit();
    constexpr void clear() noexcept;
    [[nodiscard]] constexpr bool empty() const noexcept;

    // [string.access], element access
    constexpr const_reference operator[](size_type pos) const;
    constexpr reference       operator[](size_type pos);
    constexpr const_reference at(size_type n) const;
    constexpr reference       at(size_type n);

    constexpr const charT& front() const;
    constexpr charT&       front();
    constexpr const charT& back() const;
    constexpr charT&       back();

    // [string.modifiers], modifiers
    constexpr basic_string& operator+=(const basic_string& str);
    template<class T>
      constexpr basic_string& operator+=(const T& t);
    constexpr basic_string& operator+=(const charT* s);
    constexpr basic_string& operator+=(charT c);
    constexpr basic_string& operator+=(initializer_list<charT>);
    constexpr basic_string& append(const basic_string& str);
    constexpr basic_string& append(const basic_string& str, size_type pos, size_type n = npos);
    template<class T>
      constexpr basic_string& append(const T& t);
    template<class T>
      constexpr basic_string& append(const T& t, size_type pos, size_type n = npos);
    constexpr basic_string& append(const charT* s, size_type n);
    constexpr basic_string& append(const charT* s);
    constexpr basic_string& append(size_type n, charT c);
    template<class InputIterator>
      constexpr basic_string& append(InputIterator first, InputIterator last);
    constexpr basic_string& append(initializer_list<charT>);

    constexpr void push_back(charT c);

    constexpr basic_string& assign(const basic_string& str);
    constexpr basic_string& assign(basic_string&& str)
      noexcept(allocator_traits<Allocator>::propagate_on_container_move_assignment::value ||
               allocator_traits<Allocator>::is_always_equal::value);
    constexpr basic_string& assign(const basic_string& str, size_type pos, size_type n = npos);
    template<class T>
      constexpr basic_string& assign(const T& t);
    template<class T>
      constexpr basic_string& assign(const T& t, size_type pos, size_type n = npos);
    constexpr basic_string& assign(const charT* s, size_type n);
    constexpr basic_string& assign(const charT* s);
    constexpr basic_string& assign(size_type n, charT c);
    template<class InputIterator>
      constexpr basic_string& assign(InputIterator first, InputIterator last);
    constexpr basic_string& assign(initializer_list<charT>);

    constexpr basic_string& insert(size_type pos, const basic_string& str);
    constexpr basic_string& insert(size_type pos1, const basic_string& str,
                                   size_type pos2, size_type n = npos);
    template<class T>
      constexpr basic_string& insert(size_type pos, const T& t);
    template<class T>
      constexpr basic_string& insert(size_type pos1, const T& t,
                                     size_type pos2, size_type n = npos);
    constexpr basic_string& insert(size_type pos, const charT* s, size_type n);
    constexpr basic_string& insert(size_type pos, const charT* s);
    constexpr basic_string& insert(size_type pos, size_type n, charT c);
    constexpr iterator insert(const_iterator p, charT c);
    constexpr iterator insert(const_iterator p, size_type n, charT c);
    template<class InputIterator>
      constexpr iterator insert(const_iterator p, InputIterator first, InputIterator last);
    constexpr iterator insert(const_iterator p, initializer_list<charT>);

    constexpr basic_string& erase(size_type pos = 0, size_type n = npos);
    constexpr iterator erase(const_iterator p);
    constexpr iterator erase(const_iterator first, const_iterator last);

    constexpr void pop_back();

    constexpr basic_string& replace(size_type pos1, size_type n1, const basic_string& str);
    constexpr basic_string& replace(size_type pos1, size_type n1, const basic_string& str,
                                    size_type pos2, size_type n2 = npos);
    template<class T>
      constexpr basic_string& replace(size_type pos1, size_type n1, const T& t);
    template<class T>
      constexpr basic_string& replace(size_type pos1, size_type n1, const T& t,
                                      size_type pos2, size_type n2 = npos);
    constexpr basic_string& replace(size_type pos, size_type n1, const charT* s, size_type n2);
    constexpr basic_string& replace(size_type pos, size_type n1, const charT* s);
    constexpr basic_string& replace(size_type pos, size_type n1, size_type n2, charT c);
    constexpr basic_string& replace(const_iterator i1, const_iterator i2,
                                    const basic_string& str);
    template<class T>
      constexpr basic_string& replace(const_iterator i1, const_iterator i2, const T& t);
    constexpr basic_string& replace(const_iterator i1, const_iterator i2, const charT* s,
                                    size_type n);
    constexpr basic_string& replace(const_iterator i1, const_iterator i2, const charT* s);
    constexpr basic_string& replace(const_iterator i1, const_iterator i2, size_type n, charT c);
    template<class InputIterator>
      constexpr basic_string& replace(const_iterator i1, const_iterator i2,
                                      InputIterator j1, InputIterator j2);
    constexpr basic_string& replace(const_iterator, const_iterator, initializer_list<charT>);

    constexpr size_type copy(charT* s, size_type n, size_type pos = 0) const;

    constexpr void swap(basic_string& str)
      noexcept(allocator_traits<Allocator>::propagate_on_container_swap::value ||
               allocator_traits<Allocator>::is_always_equal::value);

    // [string.ops], string operations
    constexpr const charT* c_str() const noexcept;
    constexpr const charT* data() const noexcept;
    constexpr charT* data() noexcept;
    constexpr operator basic_string_view<charT, traits>() const noexcept;
    constexpr allocator_type get_allocator() const noexcept;

    template<class T>
      constexpr size_type find(const T& t, size_type pos = 0) const noexcept(see below);
    constexpr size_type find(const basic_string& str, size_type pos = 0) const noexcept;
    constexpr size_type find(const charT* s, size_type pos, size_type n) const;
    constexpr size_type find(const charT* s, size_type pos = 0) const;
    constexpr size_type find(charT c, size_type pos = 0) const noexcept;
    template<class T>
      constexpr size_type rfind(const T& t, size_type pos = npos) const noexcept(see below);
    constexpr size_type rfind(const basic_string& str, size_type pos = npos) const noexcept;
    constexpr size_type rfind(const charT* s, size_type pos, size_type n) const;
    constexpr size_type rfind(const charT* s, size_type pos = npos) const;
    constexpr size_type rfind(charT c, size_type pos = npos) const noexcept;

    template<class T>
      constexpr size_type find_first_of(const T& t, size_type pos = 0) const noexcept(see below);
    constexpr size_type find_first_of(const basic_string& str, size_type pos = 0) const noexcept;
    constexpr size_type find_first_of(const charT* s, size_type pos, size_type n) const;
    constexpr size_type find_first_of(const charT* s, size_type pos = 0) const;
    constexpr size_type find_first_of(charT c, size_type pos = 0) const noexcept;
    template<class T>
      constexpr size_type find_last_of(const T& t,
                                       size_type pos = npos) const noexcept(see below);
    constexpr size_type find_last_of(const basic_string& str,
                                     size_type pos = npos) const noexcept;
    constexpr size_type find_last_of(const charT* s, size_type pos, size_type n) const;
    constexpr size_type find_last_of(const charT* s, size_type pos = npos) const;
    constexpr size_type find_last_of(charT c, size_type pos = npos) const noexcept;

    template<class T>
      constexpr size_type find_first_not_of(const T& t,
                                            size_type pos = 0) const noexcept(see below);
    constexpr size_type find_first_not_of(const basic_string& str,
                                          size_type pos = 0) const noexcept;
    constexpr size_type find_first_not_of(const charT* s, size_type pos, size_type n) const;
    constexpr size_type find_first_not_of(const charT* s, size_type pos = 0) const;
    constexpr size_type find_first_not_of(charT c, size_type pos = 0) const noexcept;
    template<class T>
      constexpr size_type find_last_not_of(const T& t,
                                           size_type pos = npos) const noexcept(see below);
    constexpr size_type find_last_not_of(const basic_string& str,
                                         size_type pos = npos) const noexcept;
    constexpr size_type find_last_not_of(const charT* s, size_type pos, size_type n) const;
    constexpr size_type find_last_not_of(const charT* s, size_type pos = npos) const;
    constexpr size_type find_last_not_of(charT c, size_type pos = npos) const noexcept;

    constexpr basic_string substr(size_type pos = 0, size_type n = npos) const;

    template<class T>
      constexpr int compare(const T& t) const noexcept(see below);
    template<class T>
      constexpr int compare(size_type pos1, size_type n1, const T& t) const;
    template<class T>
      constexpr int compare(size_type pos1, size_type n1, const T& t,
                            size_type pos2, size_type n2 = npos) const;
    constexpr int compare(const basic_string& str) const noexcept;
    constexpr int compare(size_type pos1, size_type n1, const basic_string& str) const;
    constexpr int compare(size_type pos1, size_type n1, const basic_string& str,
                          size_type pos2, size_type n2 = npos) const;
    constexpr int compare(const charT* s) const;
    constexpr int compare(size_type pos1, size_type n1, const charT* s) const;
    constexpr int compare(size_type pos1, size_type n1, const charT* s, size_type n2) const;

    constexpr bool starts_with(basic_string_view<charT, traits> x) const noexcept;
    constexpr bool starts_with(charT x) const noexcept;
    constexpr bool starts_with(const charT* x) const;
    constexpr bool ends_with(basic_string_view<charT, traits> x) const noexcept;
    constexpr bool ends_with(charT x) const noexcept;
    constexpr bool ends_with(const charT* x) const;
  };

  template<class InputIterator,
           class Allocator = allocator<typename iterator_traits<InputIterator>::value_type>>
    basic_string(InputIterator, InputIterator, Allocator = Allocator())
      -> basic_string<typename iterator_traits<InputIterator>::value_type,
                      char_traits<typename iterator_traits<InputIterator>::value_type>,
                      Allocator>;

  template<class charT,
           class traits,
           class Allocator = allocator<charT>>
    explicit basic_string(basic_string_view<charT, traits>, const Allocator& = Allocator())
      -> basic_string<charT, traits, Allocator>;

  template<class charT,
           class traits,
           class Allocator = allocator<charT>>
    basic_string(basic_string_view<charT, traits>,
                 typename see below::size_type, typename see below::size_type,
                 const Allocator& = Allocator())
      -> basic_string<charT, traits, Allocator>;
}
```

**4** A `size_type` parameter type in a `basic_string` deduction guide refers to the `size_type` member type of the type deduced by the deduction guide.

## Subclauses

- [21.3.2.1 General requirements \[string.require\]](string.require.md)
- [21.3.2.2 Constructors and assignment operators \[string.cons\]](string.cons.md)
- [21.3.2.3 Iterator support \[string.iterators\]](string.iterators.md)
- [21.3.2.4 Capacity \[string.capacity\]](string.capacity.md)
- [21.3.2.5 Element access \[string.access\]](string.access.md)
- [21.3.2.6 Modifiers \[string.modifiers\]](string.modifiers.md)
  - [21.3.2.6.1 `basic_string::operator+=` \[string.op.append\]](string.op.append.md)
  - [21.3.2.6.2 `basic_string::append` \[string.append\]](string.append.md)
  - [21.3.2.6.3 `basic_string::assign` \[string.assign\]](string.assign.md)
  - [21.3.2.6.4 `basic_string::insert` \[string.insert\]](string.insert.md)
  - [21.3.2.6.5 `basic_string::erase` \[string.erase\]](string.erase.md)
  - [21.3.2.6.6 `basic_string::replace` \[string.replace\]](string.replace.md)
  - [21.3.2.6.7 `basic_string::copy` \[string.copy\]](string.copy.md)
  - [21.3.2.6.8 `basic_string::swap` \[string.swap\]](string.swap.md)
- [21.3.2.7 String operations \[string.ops\]](string.ops.md)
  - [21.3.2.7.1 Accessors \[string.accessors\]](string.accessors.md)
  - [21.3.2.7.2 Searching \[string.find\]](string.find.md)
  - [21.3.2.7.3 `basic_string::substr` \[string.substr\]](string.substr.md)
  - [21.3.2.7.4 `basic_string::compare` \[string.compare\]](string.compare.md)
  - [21.3.2.7.5 `basic_string::starts_with` \[string.starts.with\]](string.starts.with.md)
  - [21.3.2.7.6 `basic_string::ends_with` \[string.ends.with\]](string.ends.with.md)

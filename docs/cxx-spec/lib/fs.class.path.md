# 29.11.7 Class `path` [fs.class.path]

[Contents](../README.md) · up: [29.11 File systems \[filesystems\]](filesystems.md) · prev: [29.11.6 Error reporting \[fs.err.report\]](fs.err.report.md) · next: [29.11.7.1 Generic pathname format \[fs.path.generic\]](fs.path.generic.md)

**1** An object of class `path` represents a path and contains a pathname. Such an object is concerned only with the lexical and syntactic aspects of a path. The path does not necessarily exist in external storage, and the pathname is not necessarily valid for the current operating system or for a particular file system.

**2** [*Note 1*: Class `path` is used to support the differences between the string types used by different operating systems to represent pathnames, and to perform conversions between encodings when necessary. — *end note*]

**3** A *path* is a sequence of elements that identify the location of a file within a filesystem. The elements are the *root-name*ₒₚₜ, *root-directory*ₒₚₜ, and an optional sequence of *filename*s ([[fs.path.generic]](fs.path.generic.md)). The maximum number of elements in the sequence is operating system dependent ([[fs.conform.os]](fs.conform.os.md)).

**4** An *absolute path* is a path that unambiguously identifies the location of a file without reference to an additional starting location. The elements of a path that determine if it is absolute are operating system dependent. A *relative path* is a path that is not absolute, and as such, only unambiguously identifies the location of a file when resolved relative to an implied starting location. The elements of a path that determine if it is relative are operating system dependent. [*Note 2*: Pathnames “.” and “..” are relative paths. — *end note*]

**5** A *pathname* is a character string that represents the name of a path. Pathnames are formatted according to the generic pathname format grammar ([[fs.path.generic]](fs.path.generic.md)) or according to an operating system dependent *native pathname format* accepted by the host operating system.

**6** *Pathname resolution* is the operating system dependent mechanism for resolving a pathname to a particular file in a file hierarchy. There may be multiple pathnames that resolve to the same file. [*Example 1*: POSIX specifies the mechanism in section 4.11, Pathname resolution. — *end example*]

```cpp
namespace std::filesystem {
  class path {
  public:
    using value_type  = see below;
    using string_type = basic_string<value_type>;
    static constexpr value_type preferred_separator = see below;

    // [fs.enum.path.format], enumeration format
    enum format;

    // [fs.path.construct], constructors and destructor
    path() noexcept;
    path(const path& p);
    path(path&& p) noexcept;
    path(string_type&& source, format fmt = auto_format);
    template<class Source>
      path(const Source& source, format fmt = auto_format);
    template<class InputIterator>
      path(InputIterator first, InputIterator last, format fmt = auto_format);
    template<class Source>
      path(const Source& source, const locale& loc, format fmt = auto_format);
    template<class InputIterator>
      path(InputIterator first, InputIterator last, const locale& loc, format fmt = auto_format);
    ~path();

    // [fs.path.assign], assignments
    path& operator=(const path& p);
    path& operator=(path&& p) noexcept;
    path& operator=(string_type&& source);
    path& assign(string_type&& source);
    template<class Source>
      path& operator=(const Source& source);
    template<class Source>
      path& assign(const Source& source);
    template<class InputIterator>
      path& assign(InputIterator first, InputIterator last);

    // [fs.path.append], appends
    path& operator/=(const path& p);
    template<class Source>
      path& operator/=(const Source& source);
    template<class Source>
      path& append(const Source& source);
    template<class InputIterator>
      path& append(InputIterator first, InputIterator last);

    // [fs.path.concat], concatenation
    path& operator+=(const path& x);
    path& operator+=(const string_type& x);
    path& operator+=(basic_string_view<value_type> x);
    path& operator+=(const value_type* x);
    path& operator+=(value_type x);
    template<class Source>
      path& operator+=(const Source& x);
    template<class EcharT>
      path& operator+=(EcharT x);
    template<class Source>
      path& concat(const Source& x);
    template<class InputIterator>
      path& concat(InputIterator first, InputIterator last);

    // [fs.path.modifiers], modifiers
    void  clear() noexcept;
    path& make_preferred();
    path& remove_filename();
    path& replace_filename(const path& replacement);
    path& replace_extension(const path& replacement = path());
    void  swap(path& rhs) noexcept;

    // [fs.path.nonmember], non-member operators
    friend bool operator==(const path& lhs, const path& rhs) noexcept;
    friend strong_ordering operator<=>(const path& lhs, const path& rhs) noexcept;

    friend path operator/ (const path& lhs, const path& rhs);

    // [fs.path.native.obs], native format observers
    const string_type& native() const noexcept;
    const value_type*  c_str() const noexcept;
    operator string_type() const;

    template<class EcharT, class traits = char_traits<EcharT>,
             class Allocator = allocator<EcharT>>
      basic_string<EcharT, traits, Allocator>
        string(const Allocator& a = Allocator()) const;
    std::string    string() const;
    std::wstring   wstring() const;
    std::u8string  u8string() const;
    std::u16string u16string() const;
    std::u32string u32string() const;

    // [fs.path.generic.obs], generic format observers
    template<class EcharT, class traits = char_traits<EcharT>,
             class Allocator = allocator<EcharT>>
      basic_string<EcharT, traits, Allocator>
        generic_string(const Allocator& a = Allocator()) const;
    std::string    generic_string() const;
    std::wstring   generic_wstring() const;
    std::u8string  generic_u8string() const;
    std::u16string generic_u16string() const;
    std::u32string generic_u32string() const;

    // [fs.path.compare], compare
    int compare(const path& p) const noexcept;
    int compare(const string_type& s) const;
    int compare(basic_string_view<value_type> s) const;
    int compare(const value_type* s) const;

    // [fs.path.decompose], decomposition
    path root_name() const;
    path root_directory() const;
    path root_path() const;
    path relative_path() const;
    path parent_path() const;
    path filename() const;
    path stem() const;
    path extension() const;

    // [fs.path.query], query
    [[nodiscard]] bool empty() const noexcept;
    bool has_root_name() const;
    bool has_root_directory() const;
    bool has_root_path() const;
    bool has_relative_path() const;
    bool has_parent_path() const;
    bool has_filename() const;
    bool has_stem() const;
    bool has_extension() const;
    bool is_absolute() const;
    bool is_relative() const;

    // [fs.path.gen], generation
    path lexically_normal() const;
    path lexically_relative(const path& base) const;
    path lexically_proximate(const path& base) const;

    // [fs.path.itr], iterators
    class iterator;
    using const_iterator = iterator;

    iterator begin() const;
    iterator end() const;

    // [fs.path.io], path inserter and extractor
    template<class charT, class traits>
      friend basic_ostream<charT, traits>&
        operator<<(basic_ostream<charT, traits>& os, const path& p);
    template<class charT, class traits>
      friend basic_istream<charT, traits>&
        operator>>(basic_istream<charT, traits>& is, path& p);
  };
}
```

**7** `value_type` is a `typedef` for the operating system dependent encoded character type used to represent pathnames.

**8** The value of the `preferred_separator` member is the operating system dependent *preferred-separator* character ([[fs.path.generic]](fs.path.generic.md)).

**9** [*Example 2*: For POSIX-based operating systems, `value_type` is `char` and `preferred_separator` is the slash character (`'/'`). For Windows-based operating systems, `value_type` is `wchar_t` and `preferred_separator` is the backslash character (`L'\\'`). — *end example*]

## Subclauses

- [29.11.7.1 Generic pathname format \[fs.path.generic\]](fs.path.generic.md)
- [29.11.7.2 Conversions \[fs.path.cvt\]](fs.path.cvt.md)
  - [29.11.7.2.1 Argument format conversions \[fs.path.fmt.cvt\]](fs.path.fmt.cvt.md)
  - [29.11.7.2.2 Type and encoding conversions \[fs.path.type.cvt\]](fs.path.type.cvt.md)
- [29.11.7.3 Requirements \[fs.path.req\]](fs.path.req.md)
- [29.11.7.4 Members \[fs.path.member\]](fs.path.member.md)
  - [29.11.7.4.1 Constructors \[fs.path.construct\]](fs.path.construct.md)
  - [29.11.7.4.2 Assignments \[fs.path.assign\]](fs.path.assign.md)
  - [29.11.7.4.3 Appends \[fs.path.append\]](fs.path.append.md)
  - [29.11.7.4.4 Concatenation \[fs.path.concat\]](fs.path.concat.md)
  - [29.11.7.4.5 Modifiers \[fs.path.modifiers\]](fs.path.modifiers.md)
  - [29.11.7.4.6 Native format observers \[fs.path.native.obs\]](fs.path.native.obs.md)
  - [29.11.7.4.7 Generic format observers \[fs.path.generic.obs\]](fs.path.generic.obs.md)
  - [29.11.7.4.8 Compare \[fs.path.compare\]](fs.path.compare.md)
  - [29.11.7.4.9 Decomposition \[fs.path.decompose\]](fs.path.decompose.md)
  - [29.11.7.4.10 Query \[fs.path.query\]](fs.path.query.md)
  - [29.11.7.4.11 Generation \[fs.path.gen\]](fs.path.gen.md)
- [29.11.7.5 Iterators \[fs.path.itr\]](fs.path.itr.md)
- [29.11.7.6 Inserter and extractor \[fs.path.io\]](fs.path.io.md)
- [29.11.7.7 Non-member functions \[fs.path.nonmember\]](fs.path.nonmember.md)

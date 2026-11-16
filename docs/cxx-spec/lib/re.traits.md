# 30.7 Class template `regex_traits` [re.traits]

[Contents](../README.md) · up: [30 Regular expressions library \[re\]](re.md) · prev: [30.6 Class `regex_error` \[re.badexp\]](re.badexp.md) · next: [30.8 Class template `basic_regex` \[re.regex\]](re.regex.md)

```cpp
namespace std {
  template<class charT>
    struct regex_traits {
      using char_type       = charT;
      using string_type     = basic_string<char_type>;
      using locale_type     = locale;
      using char_class_type = bitmask_type;

      regex_traits();
      static size_t length(const char_type* p);
      charT translate(charT c) const;
      charT translate_nocase(charT c) const;
      template<class ForwardIterator>
        string_type transform(ForwardIterator first, ForwardIterator last) const;
      template<class ForwardIterator>
        string_type transform_primary(
          ForwardIterator first, ForwardIterator last) const;
      template<class ForwardIterator>
        string_type lookup_collatename(
          ForwardIterator first, ForwardIterator last) const;
      template<class ForwardIterator>
        char_class_type lookup_classname(
          ForwardIterator first, ForwardIterator last, bool icase = false) const;
      bool isctype(charT c, char_class_type f) const;
      int value(charT ch, int radix) const;
      locale_type imbue(locale_type l);
      locale_type getloc() const;
    };
}
```

**1** The specializations `regex_traits<char>` and `regex_traits<wchar_t>` meet the requirements for a regular expression traits class ([[re.req]](re.req.md)).

```cpp
using char_class_type = bitmask_type;
```

**2** The type `char_class_type` is used to represent a character classification and is capable of holding an implementation specific set returned by `lookup_classname`.

```cpp
static size_t length(const char_type* p);
```

**3** *Returns:* `char_traits<charT>::length(p)`.

```cpp
charT translate(charT c) const;
```

**4** *Returns:* `c`.

```cpp
charT translate_nocase(charT c) const;
```

**5** *Returns:* `use_facet<ctype<charT>>(getloc()).tolower(c)`.

```cpp
template<class ForwardIterator>
  string_type transform(ForwardIterator first, ForwardIterator last) const;
```

**6** *Effects:* As if by:

```cpp
string_type str(first, last);
return use_facet<collate<charT>>(
  getloc()).transform(str.data(), str.data() + str.length());
```

```cpp
template<class ForwardIterator>
  string_type transform_primary(ForwardIterator first, ForwardIterator last) const;
```

**7** *Effects:* If

```cpp
typeid(use_facet<collate<charT>>) == typeid(collate_byname<charT>)
```

and the form of the sort key returned by `collate_byname<charT>::transform(first, last)` is known and can be converted into a primary sort key then returns that key, otherwise returns an empty string.

```cpp
template<class ForwardIterator>
  string_type lookup_collatename(ForwardIterator first, ForwardIterator last) const;
```

**8** *Returns:* A sequence of one or more characters that represents the collating element consisting of the character sequence designated by the iterator range `[first, last)`. Returns an empty string if the character sequence is not a valid collating element.

```cpp
template<class ForwardIterator>
  char_class_type lookup_classname(
    ForwardIterator first, ForwardIterator last, bool icase = false) const;
```

**9** *Returns:* An unspecified value that represents the character classification named by the character sequence designated by the iterator range `[first, last)`. If the parameter `icase` is `true` then the returned mask identifies the character classification without regard to the case of the characters being matched, otherwise it does honor the case of the characters being matched.[^1] The value returned shall be independent of the case of the characters in the character sequence. If the name is not recognized then returns `char_class_type()`.

**10** *Remarks:* For `regex_traits<char>`, at least the narrow character names in [Table 139](re.traits.md#tab:re.traits.classnames) shall be recognized. For `regex_traits<wchar_t>`, at least the wide character names in [Table 139](re.traits.md#tab:re.traits.classnames) shall be recognized.

```cpp
bool isctype(charT c, char_class_type f) const;
```

**11** *Effects:* Determines if the character `c` is a member of the character classification represented by `f`.

**12** *Returns:* Given the following function declaration:

```cpp
// for exposition only
template<class C>
  ctype_base::mask convert(typename regex_traits<C>::char_class_type f);
```

that returns a value in which each `ctype_base::mask` value corresponding to a value in `f` named in [Table 139](re.traits.md#tab:re.traits.classnames) is set, then the result is determined as if by:

```cpp
ctype_base::mask m = convert<charT>(f);
const ctype<charT>& ct = use_facet<ctype<charT>>(getloc());
if (ct.is(m, c)) {
  return true;
} else if (c == ct.widen('_')) {
  charT w[1] = { ct.widen('w') };
  char_class_type x = lookup_classname(w, w+1);
  return (f&x) == x;
} else {
  return false;
}
```

[*Example 1*:

```cpp
regex_traits<char> t;
string d("d");
string u("upper");
regex_traits<char>::char_class_type f;
f = t.lookup_classname(d.begin(), d.end());
f |= t.lookup_classname(u.begin(), u.end());
ctype_base::mask m = convert<char>(f);  // m == ctype_base::digit|ctype_base::upper
```

— *end example*] [*Example 2*:

```cpp
regex_traits<char> t;
string w("w");
regex_traits<char>::char_class_type f;
f = t.lookup_classname(w.begin(), w.end());
t.isctype('A', f);  // returns true
t.isctype('_', f);  // returns true
t.isctype(' ', f);  // returns false
```

— *end example*]

```cpp
int value(charT ch, int radix) const;
```

**13** *Preconditions:* The value of `radix` is 8, 10, or 16.

**14** *Returns:* The value represented by the digit `ch` in base `radix` if the character `ch` is a valid digit in base `radix`; otherwise returns `-1`.

```cpp
locale_type imbue(locale_type loc);
```

**15** *Effects:* Imbues `this` with a copy of the locale `loc`. [*Note 1*: Calling `imbue` with a different locale than the one currently in use invalidates all cached data held by `*this`. — *end note*]

**16** *Returns:* If no locale has been previously imbued then a copy of the global locale in effect at the time of construction of `*this`, otherwise a copy of the last argument passed to `imbue`.

**17** *Postconditions:* `getloc() == loc`.

```cpp
locale_type getloc() const;
```

**18** *Returns:* If no locale has been imbued then a copy of the global locale in effect at the time of construction of `*this`, otherwise a copy of the last argument passed to `imbue`.

<a id="tab:re.traits.classnames"></a>
**Table 139: Character class names and corresponding `ctype` masks** [tab:re.traits.classnames]

| Narrow character name | Wide character name | Corresponding `ctype_base::mask` value |
| --- | --- | --- |
| `"alnum"` | `L"alnum"` | `ctype_base::alnum` |
| `"alpha"` | `L"alpha"` | `ctype_base::alpha` |
| `"blank"` | `L"blank"` | `ctype_base::blank` |
| `"cntrl"` | `L"cntrl"` | `ctype_base::cntrl` |
| `"digit"` | `L"digit"` | `ctype_base::digit` |
| `"d"` | `L"d"` | `ctype_base::digit` |
| `"graph"` | `L"graph"` | `ctype_base::graph` |
| `"lower"` | `L"lower"` | `ctype_base::lower` |
| `"print"` | `L"print"` | `ctype_base::print` |
| `"punct"` | `L"punct"` | `ctype_base::punct` |
| `"space"` | `L"space"` | `ctype_base::space` |
| `"s"` | `L"s"` | `ctype_base::space` |
| `"upper"` | `L"upper"` | `ctype_base::upper` |
| `"w"` | `L"w"` | `ctype_base::alnum` |
| `"xdigit"` | `L"xdigit"` | `ctype_base::xdigit` |

[^1]: For example, if the parameter `icase` is `true` then `[[:lower:]]` is the same as `[[:alpha:]]`.

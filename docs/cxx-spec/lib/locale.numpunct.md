# 28.4.3.1 Class template `numpunct` [locale.numpunct]

[Contents](../README.md) · up: [28.4.3 The numeric punctuation facet \[facet.numpunct\]](facet.numpunct.md) · prev: [28.4.3 The numeric punctuation facet \[facet.numpunct\]](facet.numpunct.md) · next: [28.4.3.1.1 Members \[facet.numpunct.members\]](facet.numpunct.members.md)

```cpp
namespace std {
  template<class charT>
    class numpunct : public locale::facet {
    public:
      using char_type   = charT;
      using string_type = basic_string<charT>;

      explicit numpunct(size_t refs = 0);

      char_type    decimal_point()   const;
      char_type    thousands_sep()   const;
      string       grouping()        const;
      string_type  truename()        const;
      string_type  falsename()       const;

      static locale::id id;

    protected:
      ~numpunct();                                              // virtual
      virtual char_type    do_decimal_point() const;
      virtual char_type    do_thousands_sep() const;
      virtual string       do_grouping()      const;
      virtual string_type  do_truename()      const;            // for bool
      virtual string_type  do_falsename()     const;            // for bool
    };
}
```

**1** `numpunct<>` specifies numeric punctuation. The specializations required in [Table 102](locale.category.md#tab:locale.category.facets) ([[locale.category]](locale.category.md)), namely `numpunct<wchar_t>` and `numpunct<char>`, provide classic `"C"` numeric formats, i.e., they contain information equivalent to that contained in the `"C"` locale or their wide character counterparts as if obtained by a call to `widen`.

**2** The syntax for number formats is as follows, where *digit* represents the radix set specified by the `fmtflags` argument value, and *thousands-sep* and *decimal-point* are the results of corresponding `numpunct<charT>` members. Integer values have the format:

```bnf
intval:
    signₒₚₜ units
```

```bnf
sign:
    '+'
    '-'
```

```bnf
units:
    digits
    digits thousands-sep units
```

```bnf
digits:
    digit digitsₒₚₜ
```

and floating-point values have:

```bnf
floatval:
    signₒₚₜ units fractionalₒₚₜ exponentₒₚₜ
    signₒₚₜ decimal-point digits exponentₒₚₜ
```

```bnf
fractional:
    decimal-point digitsₒₚₜ
```

```bnf
exponent:
    e signₒₚₜ digits
```

```bnf
e:
    'e'
    'E'
```

where the number of digits between *thousands-sep*s is as specified by `do_grouping()`. For parsing, if the *digits* portion contains no thousands-separators, no grouping constraint is applied.

## Subclauses

- [28.4.3.1.1 Members \[facet.numpunct.members\]](facet.numpunct.members.md)
- [28.4.3.1.2 Virtual functions \[facet.numpunct.virtuals\]](facet.numpunct.virtuals.md)

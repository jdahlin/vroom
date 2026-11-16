# 5.13.8 User-defined literals [lex.ext]

[Contents](README.md) · up: [5.13 Literals \[lex.literal\]](lex.literal.md) · prev: [5.13.7 Pointer literals \[lex.nullptr\]](lex.nullptr.md) · next: [6 Basics \[basic\]](basic.md)

```bnf
user-defined-literal:
    user-defined-integer-literal
    user-defined-floating-point-literal
    user-defined-string-literal
    user-defined-character-literal
```

```bnf
user-defined-integer-literal:
    decimal-literal ud-suffix
    octal-literal ud-suffix
    hexadecimal-literal ud-suffix
    binary-literal ud-suffix
```

```bnf
user-defined-floating-point-literal:
    fractional-constant exponent-partₒₚₜ ud-suffix
    digit-sequence exponent-part ud-suffix
    hexadecimal-prefix hexadecimal-fractional-constant binary-exponent-part ud-suffix
    hexadecimal-prefix hexadecimal-digit-sequence binary-exponent-part ud-suffix
```

```bnf
user-defined-string-literal:
    string-literal ud-suffix
```

```bnf
user-defined-character-literal:
    character-literal ud-suffix
```

```bnf
ud-suffix:
    identifier
```

**1** If a token matches both *user-defined-literal* and another *literal* kind, it is treated as the latter. [*Example 1*: `123_km` is a *user-defined-literal*, but `12LL` is an *integer-literal*. — *end example*] The syntactic non-terminal preceding the *ud-suffix* in a *user-defined-literal* is taken to be the longest sequence of characters that could match that non-terminal.

**2** A *user-defined-literal* is treated as a call to a literal operator or literal operator template ([[over.literal]](over.literal.md)). To determine the form of this call for a given *user-defined-literal* *L* with *ud-suffix* *X*, the *literal-operator-id* whose literal suffix identifier is *X* is looked up in the context of *L* using the rules for unqualified name lookup ([[basic.lookup.unqual]](basic.lookup.unqual.md)). Let *S* be the set of declarations found by this lookup. *S* shall not be empty.

**3** If *L* is a *user-defined-integer-literal*, let *n* be the literal without its *ud-suffix*. If *S* contains a literal operator with parameter type `unsigned long long`, the literal *L* is treated as a call of the form

```cpp
operator "" X(nULL)
```

Otherwise, *S* shall contain a raw literal operator or a numeric literal operator template ([[over.literal]](over.literal.md)) but not both. If *S* contains a raw literal operator, the literal *L* is treated as a call of the form

```cpp
operator "" X("n")
```

Otherwise (*S* contains a numeric literal operator template), *L* is treated as a call of the form

```cpp
operator "" X<'c_1', 'c_2', ... 'c_k'>()
```

where *n* is the source character sequence *c*<sub>1</sub>*c*<sub>2</sub>...*c*<sub>*k*</sub>. [*Note 1*: The sequence *c*<sub>1</sub>*c*<sub>2</sub>...*c*<sub>*k*</sub> can only contain characters from the basic source character set. — *end note*]

**4** If *L* is a *user-defined-floating-point-literal*, let *f* be the literal without its *ud-suffix*. If *S* contains a literal operator with parameter type `long double`, the literal *L* is treated as a call of the form

```cpp
operator "" X(fL)
```

Otherwise, *S* shall contain a raw literal operator or a numeric literal operator template ([[over.literal]](over.literal.md)) but not both. If *S* contains a raw literal operator, the *literal* *L* is treated as a call of the form

```cpp
operator "" X("f")
```

Otherwise (*S* contains a numeric literal operator template), *L* is treated as a call of the form

```cpp
operator "" X<'c_1', 'c_2', ... 'c_k'>()
```

where *f* is the source character sequence *c*<sub>1</sub>*c*<sub>2</sub>...*c*<sub>*k*</sub>. [*Note 2*: The sequence *c*<sub>1</sub>*c*<sub>2</sub>...*c*<sub>*k*</sub> can only contain characters from the basic source character set. — *end note*]

**5** If *L* is a *user-defined-string-literal*, let *str* be the literal without its *ud-suffix* and let *len* be the number of code units in *str* (i.e., its length excluding the terminating null character). If *S* contains a literal operator template with a non-type template parameter for which *str* is a well-formed *template-argument*, the literal *L* is treated as a call of the form

```cpp
operator "" X<str>()
```

Otherwise, the literal *L* is treated as a call of the form

```cpp
operator "" X(str, len)
```

**6** If *L* is a *user-defined-character-literal*, let *ch* be the literal without its *ud-suffix*. *S* shall contain a literal operator ([[over.literal]](over.literal.md)) whose only parameter has the type of *ch* and the literal *L* is treated as a call of the form

```cpp
operator "" X(ch)
```

**7** [*Example 2*:

```cpp
long double operator "" _w(long double);
std::string operator "" _w(const char16_t*, std::size_t);
unsigned operator "" _w(const char*);
int main() {
  1.2_w;            // calls operator "" _w(1.2L)
  u"one"_w;         // calls operator "" _w(u"one", 3)
  12_w;             // calls operator "" _w("12")
  "two"_w;          // error: no applicable literal operator
}
```

— *end example*]

**8** In translation phase 6 ([[lex.phases]](lex.phases.md)), adjacent *string-literal*s are concatenated and *user-defined-string-literal*s are considered *string-literal*s for that purpose. During concatenation, *ud-suffix*es are removed and ignored and the concatenation process occurs as described in [[lex.string]](lex.string.md). At the end of phase 6, if a *string-literal* is the result of a concatenation involving at least one *user-defined-string-literal*, all the participating *user-defined-string-literal*s shall have the same *ud-suffix* and that suffix is applied to the result of the concatenation.

**9** [*Example 3*:

```cpp
int main() {
  L"A" "B" "C"_x;   // OK: same as L"ABC"_x
  "P"_x "Q" "R"_y;  // error: two different ud-suffixes
}
```

— *end example*]

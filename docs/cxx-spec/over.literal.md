# 12.8 User-defined literals [over.literal]

[Contents](README.md) · up: [12 Overloading \[over\]](over.md) · prev: [12.7 Built-in operators \[over.built\]](over.built.md) · next: [13 Templates \[temp\]](temp.md)

```bnf
literal-operator-id:
    'operator' string-literal identifier
    'operator' user-defined-string-literal
```

**1** The *string-literal* or *user-defined-string-literal* in a *literal-operator-id* shall have no *encoding-prefix* and shall contain no characters other than the implicit terminating `'\0'`. The *ud-suffix* of the *user-defined-string-literal* or the *identifier* in a *literal-operator-id* is called a *literal suffix identifier*. Some literal suffix identifiers are reserved for future standardization; see [[usrlit.suffix]](lib/usrlit.suffix.md). A declaration whose *literal-operator-id* uses such a literal suffix identifier is ill-formed, no diagnostic required.

**2** A declaration whose *declarator-id* is a *literal-operator-id* shall be a declaration of a namespace-scope function or function template (it could be a friend function ([[class.friend]](class.friend.md))), an explicit instantiation or specialization of a function template, or a *using-declaration* ([[namespace.udecl]](namespace.udecl.md)). A function declared with a *literal-operator-id* is a *literal operator*. A function template declared with a *literal-operator-id* is a *literal operator template*.

**3** The declaration of a literal operator shall have a *parameter-declaration-clause* equivalent to one of the following:

```cpp
const char*
unsigned long long int
long double
char
wchar_t
char8_t
char16_t
char32_t
const char*, std::size_t
const wchar_t*, std::size_t
const char8_t*, std::size_t
const char16_t*, std::size_t
const char32_t*, std::size_t
```

If a parameter has a default argument ([[dcl.fct.default]](dcl.fct.default.md)), the program is ill-formed.

**4** A *raw literal operator* is a literal operator with a single parameter whose type is `const char*`.

**5** A *numeric literal operator template* is a literal operator template whose *template-parameter-list* has a single *template-parameter* that is a non-type template parameter pack ([[temp.variadic]](temp.variadic.md)) with element type `char`. A *string literal operator template* is a literal operator template whose *template-parameter-list* comprises a single non-type *template-parameter* of class type. The declaration of a literal operator template shall have an empty *parameter-declaration-clause* and shall declare either a numeric literal operator template or a string literal operator template.

**6** Literal operators and literal operator templates shall not have C language linkage.

**7** [*Note 1*: Literal operators and literal operator templates are usually invoked implicitly through user-defined literals ([[lex.ext]](lex.ext.md)). However, except for the constraints described above, they are ordinary namespace-scope functions and function templates. In particular, they are looked up like ordinary functions and function templates and they follow the same overload resolution rules. Also, they can be declared `inline` or `constexpr`, they may have internal, module, or external linkage, they can be called explicitly, their addresses can be taken, etc. — *end note*]

**8** [*Example 1*:

```cpp
void operator "" _km(long double);                  // OK
string operator "" _i18n(const char*, std::size_t); // OK
template <char...> double operator "" _\u03C0();    // OK: UCN for lowercase pi
float operator ""_e(const char*);                   // OK
float operator ""E(const char*);                    // error: reserved literal suffix ([usrlit.suffix], [lex.ext])
double operator""_Bq(long double);                  // OK: does not use the reserved identifier _Bq ([lex.name])
double operator"" _Bq(long double);                 // uses the reserved identifier _Bq ([lex.name])
float operator " " B(const char*);                  // error: non-empty string-literal
string operator "" 5X(const char*, std::size_t);    // error: invalid literal suffix identifier
double operator "" _miles(double);                  // error: invalid parameter-declaration-clause
template <char...> int operator "" _j(const char*); // error: invalid parameter-declaration-clause
extern "C" void operator "" _m(long double);        // error: C language linkage
```

— *end example*]

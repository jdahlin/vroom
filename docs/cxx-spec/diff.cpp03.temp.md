# C.4.5 [[temp]](temp.md): templates [diff.cpp03.temp]

[Contents](README.md) · up: [C.4 C++ and ISO C++ 2003 \[diff.cpp03\]](diff.cpp03.md) · prev: [C.4.4 [[class]](class.md): classes \[diff.cpp03.class\]](diff.cpp03.class.md) · next: [C.4.6 [[library]](lib/library.md): library introduction \[diff.cpp03.library\]](diff.cpp03.library.md)

**1** **Affected subclause:** [[temp.param]](temp.param.md) <br>**Change:** Remove `export`. <br>**Rationale:** No implementation consensus. <br>**Effect on original feature:** A valid C++ 2003 declaration containing `export` is ill-formed in this International Standard.

**2** **Affected subclause:** [[temp.arg]](temp.arg.md) <br>**Change:** Remove whitespace requirement for nested closing template right angle brackets. <br>**Rationale:** Considered a persistent but minor annoyance. Template aliases representing non-class types would exacerbate whitespace issues. <br>**Effect on original feature:** Change to semantics of well-defined expression. A valid C++ 2003 expression containing a right angle bracket (“`>`”) followed immediately by another right angle bracket may now be treated as closing two templates. For example, the following code is valid in C++ 2003 because “`>>`” is a right-shift operator, but invalid in this International Standard because “`>>`” closes two templates.

```cpp
template <class T> struct X { };
template <int N> struct Y { };
X< Y< 1 >> 2 > > x;
```

**3** **Affected subclause:** [[temp.dep.candidate]](temp.dep.candidate.md) <br>**Change:** Allow dependent calls of functions with internal linkage. <br>**Rationale:** Overly constrained, simplify overload resolution rules. <br>**Effect on original feature:** A valid C++ 2003 program could get a different result than this International Standard.

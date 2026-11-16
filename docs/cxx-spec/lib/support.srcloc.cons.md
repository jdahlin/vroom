# 17.8.2.1 Creation [support.srcloc.cons]

[Contents](../README.md) · up: [17.8.2 Class `source_location` \[support.srcloc.class\]](support.srcloc.class.md) · prev: [17.8.2 Class `source_location` \[support.srcloc.class\]](support.srcloc.class.md) · next: [17.8.2.2 Observers \[support.srcloc.obs\]](support.srcloc.obs.md)

```cpp
static consteval source_location current() noexcept;
```

**1** *Returns:*

- When invoked by a function call whose *postfix-expression* is a (possibly parenthesized) *id-expression* naming `current`, returns a `source_location` with an implementation-defined value. The value should be affected by `#line` ([[cpp.line]](../cpp.line.md)) in the same manner as for `__LINE__` and `__FILE__`. The values of the exposition-only data members of the returned `source_location` object are indicated in [Table 38](support.srcloc.cons.md#tab:support.srcloc.current).

  <a id="tab:support.srcloc.current"></a>
  **Table 38: Value of object returned by `current`** [tab:support.srcloc.current]

  | Element | Value |
  | --- | --- |
  | `line_` | A presumed line number ([[cpp.predefined]](../cpp.predefined.md)). Line numbers are presumed to be 1-indexed; however, an implementation is encouraged to use 0 when the line number is unknown. |
  | `column_` | An implementation-defined value denoting some offset from the start of the line denoted by `line_`. Column numbers are presumed to be 1-indexed; however, an implementation is encouraged to use 0 when the column number is unknown. |
  | `file_name_` | A presumed name of the current source file ([[cpp.predefined]](../cpp.predefined.md)) as an NTBS. |
  | `function_name_` | A name of the current function such as in `__func__` ([[dcl.fct.def.general]](../dcl.fct.def.general.md)) if any, an empty string otherwise. |

- Otherwise, when invoked in some other way, returns a `source_location` whose data members are initialized with valid but unspecified values.

**2** *Remarks:* Any call to `current` that appears as a default member initializer ([[class.mem]](../class.mem.md)), or as a subexpression thereof, should correspond to the location of the constructor definition or aggregate initialization that uses the default member initializer. Any call to `current` that appears as a default argument ([[dcl.fct.default]](../dcl.fct.default.md)), or as a subexpression thereof, should correspond to the location of the invocation of the function that uses the default argument ([[expr.call]](../expr.call.md)).

**3** [*Example 1*:

```cpp
struct s {
  source_location member = source_location::current();
  int other_member;
  s(source_location loc = source_location::current())
    : member(loc)               // values of member refer to the location of the calling function ([dcl.fct.default])
  {}
  s(int blather) :              // values of member refer to this location
    other_member(blather)
  {}
  s(double)                     // values of member refer to this location
  {}
};
void f(source_location a = source_location::current()) {
  source_location b = source_location::current();       // values in b refer to this line
}

void g() {
  f();                          // f's first argument corresponds to this line of code

  source_location c = source_location::current();
  f(c);                         // f's first argument gets the same values as c, above
}
```

— *end example*]

```cpp
constexpr source_location() noexcept;
```

**4** *Effects:* The data members are initialized with valid but unspecified values.

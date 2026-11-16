# 13.10 Function template specializations [temp.fct.spec]

[Contents](README.md) · up: [13 Templates \[temp\]](temp.md) · prev: [13.9.3 Explicit specialization \[temp.expl.spec\]](temp.expl.spec.md) · next: [13.10.1 Explicit template argument specification \[temp.arg.explicit\]](temp.arg.explicit.md)

**1** A function instantiated from a function template is called a function template specialization; so is an explicit specialization of a function template. Template arguments can be explicitly specified when naming the function template specialization, deduced from the context (e.g., deduced from the function arguments in a call to the function template specialization, see [[temp.deduct]](temp.deduct.md)), or obtained from default template arguments.

**2** Each function template specialization instantiated from a template has its own copy of any static variable. [*Example 1*:

```cpp
template<class T> void f(T* p) {
  static T s;
};

void g(int a, char* b) {
  f(&a);            // calls f<int>(int*)
  f(&b);            // calls f<char*>(char**)
}
```

Here `f<int>(int*)` has a static variable `s` of type `int` and `f<char*>(char**)` has a static variable `s` of type `char*`. — *end example*]

## Subclauses

- [13.10.1 Explicit template argument specification \[temp.arg.explicit\]](temp.arg.explicit.md)
- [13.10.2 Template argument deduction \[temp.deduct\]](temp.deduct.md)
  - [13.10.2.1 Deducing template arguments from a function call \[temp.deduct.call\]](temp.deduct.call.md)
  - [13.10.2.2 Deducing template arguments taking the address of a function template \[temp.deduct.funcaddr\]](temp.deduct.funcaddr.md)
  - [13.10.2.3 Deducing conversion function template arguments \[temp.deduct.conv\]](temp.deduct.conv.md)
  - [13.10.2.4 Deducing template arguments during partial ordering \[temp.deduct.partial\]](temp.deduct.partial.md)
  - [13.10.2.5 Deducing template arguments from a type \[temp.deduct.type\]](temp.deduct.type.md)
  - [13.10.2.6 Deducing template arguments from a function declaration \[temp.deduct.decl\]](temp.deduct.decl.md)
- [13.10.3 Overload resolution \[temp.over\]](temp.over.md)

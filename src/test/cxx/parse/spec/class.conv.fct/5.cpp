// [class.conv.fct] Example 5 (paragraph 6), docs/cxx-spec/class/class.conv.fct.md
// needs: P7
// status: annotated

struct S {
  operator auto() const { return 10; }      // OK
  template<class T>
  operator auto() const { return 1.2; }     // error: conversion function template // semantic
};

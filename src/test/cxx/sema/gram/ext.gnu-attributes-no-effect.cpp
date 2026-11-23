// [ext:gnu_attributes] attributes clang accepts that change no code vcc
// generates.
// RUN: %clang_cc1 -std=c++20 -fsyntax-only %s
void a() __attribute__((optnone));
struct __attribute__((consumable(unconsumed))) C {
  void f() __attribute__((callable_when("unconsumed")));
  bool h() __attribute__((test_typestate(consumed)));
};
struct __attribute__((reentrant_capability)) M {};

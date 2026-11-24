// [ext:gnu_attributes] attributes of other targets: on clang's target
// (-triple aarch64-unknown-linux-gnu) ignored with a warning, accepted with
// no effect.
// RUN: %clang_cc1 -std=c++20 -fms-extensions -fsyntax-only %s
void a() __attribute__((interrupt));
void b() __attribute__((no_caller_saved_registers));

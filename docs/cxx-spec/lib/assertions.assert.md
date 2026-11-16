# 19.3.2 The `assert` macro [assertions.assert]

[Contents](../README.md) · up: [19.3 Assertions \[assertions\]](assertions.md) · prev: [19.3.1 Header `<cassert>` synopsis \[cassert.syn\]](cassert.syn.md) · next: [19.4 Error numbers \[errno\]](errno.md)

**1** An expression `assert(E)` is a constant subexpression ([[defns.const.subexpr]](definitions.md#defns.const.subexpr)), if

- `NDEBUG` is defined at the point where `assert` is last defined or redefined, or
- `E` contextually converted to `bool` ([[conv]](../conv.md)) is a constant subexpression that evaluates to the value `true`.

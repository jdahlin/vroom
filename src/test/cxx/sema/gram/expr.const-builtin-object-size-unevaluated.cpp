// [expr.const]/5, clang's __builtin_object_size: the pointer argument is not
// evaluated for its value, so a call of a function that is not constexpr in
// it does not make the constexpr variable ill-formed.
struct S { const char *name; };
S invalid_base();
constexpr unsigned long bos_name = __builtin_object_size(invalid_base().name, 1);

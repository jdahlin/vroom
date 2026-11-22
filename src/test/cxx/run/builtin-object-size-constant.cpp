/* [ext:gnu_library_builtins] __builtin_object_size and
   __builtin_dynamic_object_size of a pointer into a constant array (a string
   literal) of the types 1 and 3 are the bytes from the pointer to the end of
   the array in a constant expression, as clang's; __builtin_iszero, __builtin_issubnormal
   and __builtin_isfpclass of a constant fold. */
static_assert(__builtin_object_size("abc", 1) == 4, "");
static_assert(__builtin_object_size("abc" + 1, 1) == 3, "");
static_assert(__builtin_object_size(L"ab", 3) == 12, "");
static_assert(__builtin_dynamic_object_size("abcd", 1) == 5, "");
static_assert(__builtin_iszero(0.0) && !__builtin_iszero(1.5), "");
static_assert(__builtin_issubnormal(1e-310) && !__builtin_issubnormal(1.0), "");
static_assert(__builtin_isfpclass(1.5, 0x100) && !__builtin_isfpclass(1.5, 0x8),
              "");
static_assert(__builtin_isfpclass(-1.5, 0x8) && __builtin_isfpclass(-0.0, 0x20),
              "");

int main()
{
    return 0;
}

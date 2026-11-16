// T x{.a = ...}: designated initializers in a direct-list-initialization (libc++'s
// __format/parser_std_format_spec.h: __fields __fields_bool{.__type_ = true})
struct F { bool a : 1 {false}; bool b : 1 {false}; int c; };
inline constexpr F f{.a = true, .b = true};
F g = {.a = true, .c = 3};
void h() { F l{.b = true, .c{4}}; (void)l; }

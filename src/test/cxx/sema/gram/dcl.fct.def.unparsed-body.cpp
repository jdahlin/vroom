// [dcl.fct.def.general]/1: a function-definition whose body is kept as
// tokens still declares only its declarator-id: the return type and
// parameter types it names (the GNU vector type here, as clang's
// <f16cintrin.h> uses them) stay usable in the declarations after it.
typedef float v4 __attribute__((__vector_size__(16), __aligned__(16)));
typedef long long v2u __attribute__((__vector_size__(16), __aligned__(1), __may_alias__));

v4 widen(unsigned short a) {
  return (v4){__builtin_bit_cast(__fp16, a), 0, 0, 0};
}

v4 add(v4 x, v4 y) { return x + y; }
v2u copy(v2u x) { return x; }
v4 again(v4 x) { return add(x, x); }

/* AArch64 libc declares scalable vector APIs even in a baseline target. */
#if defined(__aarch64__)
typedef __SVFloat32_t sv_f32;
typedef __SVFloat64_t sv_f64;
typedef __SVBool_t sv_bool;
sv_f32 scalable_math(sv_f32, sv_bool);
sv_f64 scalable_math64(sv_f64, sv_bool);
sv_f32 *pointer;
_Static_assert(__builtin_types_compatible_p(sv_f32, __SVFloat32_t), "alias");
_Static_assert(!__builtin_types_compatible_p(sv_f32, sv_f64), "distinct floats");
_Static_assert(!__builtin_types_compatible_p(__SVInt32_t, __SVUint32_t), "signedness");
_Static_assert(!__builtin_types_compatible_p(sv_bool, __SVCount_t), "distinct predicates");
_Static_assert(sizeof(pointer) == sizeof(void *), "ordinary pointer");
#endif
int main(void) { return 0; }

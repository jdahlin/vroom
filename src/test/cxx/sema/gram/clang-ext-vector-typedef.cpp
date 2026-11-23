// [ext:clang_ext_vector_type] ext_vector_type(N) on a typedef: in the decl-specifier-seq or after the declarator, either spelling;
// the layout is clang's on AArch64 (size, alignment).
typedef __attribute__((ext_vector_type(4))) int VI4;
typedef int EV4Int __attribute__((__ext_vector_type__(4)));
typedef float F2 __attribute__((ext_vector_type(2)));
typedef short S3 __attribute__((ext_vector_type(3)));
static_assert(sizeof(VI4) == 16, "");
static_assert(sizeof(EV4Int) == 16, "");
static_assert(sizeof(F2) == 8, "");
static_assert(sizeof(S3) == 8, "");
struct Holds { VI4 v; F2 f; };
static_assert(sizeof(Holds) == 32, "");

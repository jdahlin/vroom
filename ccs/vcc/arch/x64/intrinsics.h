#ifndef VCC_X64_INTRINSICS_H
#define VCC_X64_INTRINSICS_H
#include "../../lib/x86features.h"
/* Generated identities and declarative operand plans are shared by sema and
 * lowering. Signatures describe C types; instruction plans never expand C ASTs. */
enum x86_elementwise_op { XE_MIN, XE_MAX, XE_ADD_SAT, XE_SUB_SAT, XE_POPCOUNT, XE_REDUCE_ADD };
enum x86_intrinsic_class {
 XI_UNSUPPORTED, XI_DIRECT, XI_TIED, XI_SHIFT, XI_MASK, XI_MASKZ, XI_LOAD, XI_REDUCE, XI_XGETBV, XI_LANE, XI_EXTRACT
};
typedef struct x86_intrinsic {
 const char *name, *sig, *features, *mnemonic, *order, *constraints;
 unsigned char kind;
 unsigned short width;
} x86_intrinsic;
#define X86_INTRINSIC(ID, NAME, SIG, FEATURES, CLASS, MNEMONIC, ORDER, WIDTH, CONSTRAINTS) XI_ID_##ID,
enum x86_intrinsic_id {
#include "intrinsics.inc"
 XI_ID_COUNT
};
#undef X86_INTRINSIC
const x86_intrinsic *cc_x86_intrinsic(unsigned id);
/* LLVM feature lists are alternatives of comma-separated conjunctions. */
static inline bool x86_intrinsic_features(x86_features f, const char *s)
{
 bool conjunction = true;
 while (*s) {
  char name[80]; unsigned n = 0;
  while (*s && *s != ',' && *s != '|') { if (n + 1 < sizeof name) name[n++] = *s; s++; }
  name[n] = 0;
  int bit = x86_feature_find(name);
  conjunction &= bit >= 0 && x86_feature_has(f, bit);
  if (!*s) return conjunction;
  if (*s++ == '|') { if (conjunction) return true; conjunction = true; }
 }
 return conjunction;
}

#endif

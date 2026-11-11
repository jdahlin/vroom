/* diag-flags: -std=c23 -ferror-limit=0 */
/* diag-differs: vcc describes incompatible representations without Clang's C++ trivially-copyable terminology for complete/size constraints; constexpr failures point at the object being declared. Vcc also folds constexpr bit-field and _BitInt destinations that Clang23 explicitly reports as not yet supported. */
/* These accepted constraints also lock in the actual interpreted values. */
struct Fields { unsigned a:3,b:5; };
constexpr struct Fields fields = __builtin_bit_cast(struct Fields, 0x5du);
static_assert(fields.a == 5 && fields.b == 11);
static_assert(__builtin_bit_cast(unsigned,fields.b) == 11);
constexpr _BitInt(9) signed_bits = __builtin_bit_cast(_BitInt(9), (unsigned short)511);
static_assert(signed_bits == -1);
struct Incomplete;
void invalid(int n, _Atomic int atomic) {
  (void)__builtin_bit_cast(unsigned long, 1);
  (void)__builtin_bit_cast(void, 1);
  (void)__builtin_bit_cast(struct Incomplete, 1);
  (void)__builtin_bit_cast(unsigned, atomic);
  int vla[n];
  (void)__builtin_bit_cast(unsigned, vla);
}
constexpr unsigned long pointer_bits = __builtin_bit_cast(unsigned long, (void *)0);
union U { unsigned x; };
constexpr unsigned union_bits = __builtin_bit_cast(unsigned, (union U){1});
struct Padding { unsigned char c; unsigned value; };
constexpr unsigned long padding_bits = __builtin_bit_cast(unsigned long, (struct Padding){1,2});
constexpr _Bool invalid_bool = __builtin_bit_cast(_Bool, (unsigned char)2);
int side_effect(void);
constexpr unsigned effect = __builtin_bit_cast(unsigned, (side_effect(), 1));
struct ByteView { unsigned char b[8]; };
constexpr struct ByteView uninitialized_byte = __builtin_bit_cast(struct ByteView, (struct Padding){1,2});
constexpr unsigned long regenerated_padding = __builtin_bit_cast(unsigned long,
    __builtin_bit_cast(struct Padding, 0x0000000200000001ul));
constexpr unsigned char hidden_invalid_bool = __builtin_bit_cast(unsigned char,
    __builtin_bit_cast(_Bool, (unsigned char)2));

const unsigned ordinary_constant = 1;
constexpr unsigned not_constexpr_object = __builtin_bit_cast(unsigned,ordinary_constant);

union ActiveUnion { unsigned u; float f; };
constexpr union ActiveUnion active_union = {.u=0x3f800000u};
constexpr unsigned inactive_member = __builtin_bit_cast(unsigned,active_union.f);
struct NestedUnion { union ActiveUnion member; };
constexpr struct NestedUnion nested_union = {{.u=0x3f800000u}};
constexpr unsigned nested_inactive_member = __builtin_bit_cast(unsigned,nested_union.member.f);
constexpr union ActiveUnion union_array[1] = {{.u=0x3f800000u}};
constexpr unsigned indexed_inactive_member = __builtin_bit_cast(unsigned,union_array[0].f);
constexpr unsigned integer_array[1] = {0x3f800000u};
constexpr unsigned reinterpreted_index = __builtin_bit_cast(unsigned,((const float*)integer_array)[0]);
constexpr unsigned live_logical_call = __builtin_bit_cast(unsigned,1 && side_effect());
struct UnnamedBits { unsigned :8; unsigned value:24; };
constexpr struct UnnamedBits unnamed_bits = {.value=1};
constexpr unsigned exposed_unnamed_padding = __builtin_bit_cast(unsigned,unnamed_bits);

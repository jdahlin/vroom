/* [ext:gnu_attributes] packed bit-fields: at the next bit whatever the
   type's alignment, the class aligned to one byte (libc++'s basic_string
   __long: struct _LIBCPP_PACKED { size_type __is_long_ : 1; ... }) */
struct __attribute__((packed)) P {
  unsigned a : 1;
  unsigned b : 31;
};
struct __attribute__((packed)) P2 {
  char c;
  unsigned a : 3;
  unsigned b : 30;
  short s;
};
struct Q {
  struct __attribute__((packed)) {
    unsigned long a : 1;
    unsigned long b : 63;
  };
  char tail;
};

static_assert(sizeof(P) == 4 && alignof(P) == 1, "");
static_assert(sizeof(P2) == 8 && alignof(P2) == 1, "");
static_assert(sizeof(Q) == 9 && alignof(Q) == 1, "");

int main()
{
  P p;
  p.a = 1;
  p.b = 0x12345;
  if (p.a != 1 || p.b != 0x12345)
    return 1;
  P2 q;
  q.c = 'x';
  q.a = 5;
  q.b = 0x2bcdef01;
  q.s = -3;
  if (q.c != 'x' || q.a != 5 || q.b != 0x2bcdef01 || q.s != -3)
    return 2;
  Q r;
  r.a = 1;
  r.b = 123456789;
  r.tail = 'z';
  if (r.a != 1 || r.b != 123456789 || r.tail != 'z')
    return 3;
  return 0;
}

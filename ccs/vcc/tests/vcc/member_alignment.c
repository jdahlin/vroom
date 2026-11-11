struct S { char a __attribute__((aligned(4))); int b __attribute__((packed)); };
#pragma pack(push, 2)
struct P { char a; int b; };
#pragma pack(pop)
struct __attribute__((packed)) Q { char a; long b; };
_Static_assert(__alignof__(((struct S *)0)->a) == 4, "explicit field alignment");
_Static_assert(__alignof__(((struct S *)0)->b) == 1, "packed field alignment");
_Static_assert(__alignof__(((struct P *)0)->b) == 2, "pragma field alignment");
_Static_assert(__alignof__(((struct Q *)0)->b) == 1, "packed record alignment");
int main(void) { return 0; }

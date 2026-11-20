/* [dcl.align]/1, [dcl.attr.grammar]/4: alignas before a member's
   decl-specifier-seq raises the member's alignment, so the class's: of a
   constant, a constexpr variable (libc++'s <any> storage union), a type, the
   strictest of several; in a union too ([class.union]/1).  The size is a
   multiple of the class's alignment ([basic.align]/3). */
constexpr unsigned long small_buffer_alignment = alignof(void *);
struct S {
    char a;
    alignas(16) char b;
    alignas(2) alignas(int) char c;
};
union U {
    alignas(small_buffer_alignment) char buf[3];
    char c;
};
struct V {
    char a;
    alignas(double) char b[3];
};
static_assert(alignof(S) == 16 && sizeof(S) == 32);
static_assert(alignof(U) == 8 && sizeof(U) == 8);
static_assert(alignof(V) == 8 && sizeof(V) == 16);

int main()
{
    S s;
    if (reinterpret_cast<unsigned long>(&s.b) % 16 ||
        reinterpret_cast<unsigned long>(&s.c) % 4)
        return 1;
    return 0;
}

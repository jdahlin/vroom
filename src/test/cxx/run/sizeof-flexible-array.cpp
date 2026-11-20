/* [ext:gnu_flexible_array] a flexible array member (clang's extension in
   C++): it adds nothing to sizeof, only to the alignment; [expr.sizeof]/1 of
   the class and of the member's expression is the class's size without it
   (sizeof of the array itself is an incomplete type's: ill-formed) */
struct Flex { int n; int a[]; };
struct Flex8 { char c; long a[]; };
static_assert(sizeof(Flex) == 4 && alignof(Flex) == 4);
static_assert(sizeof(Flex8) == 8 && alignof(Flex8) == 8);
int main()
{
    long buffer[4] = {};
    Flex8 *p = reinterpret_cast<Flex8 *>(buffer);
    p->a[1] = 5;
    return buffer[2] != 5 || sizeof(*p) != 8;
}

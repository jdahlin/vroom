/* [dcl.typedef]/2, [dcl.type.elab]/2: an alias-declaration's type-id may be an
   elaborated-type-specifier naming a class or enumeration, which a function of
   the same name does not hide */
struct stat {
    int size;
};
static int stat(struct stat *s) { return s->size; }
enum color { red, green };
static int run()
{
    using Stat = struct stat;
    using Color = enum color;
    Stat s{4};
    Color c = green;
    return stat(&s) + c;
}
using global_stat = struct stat;
int main()
{
    global_stat g{1};
    return run() != 5 || g.size != 1;
}

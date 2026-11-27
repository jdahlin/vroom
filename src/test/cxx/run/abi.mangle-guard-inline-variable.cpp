/* [abi:mangling-special-guards]: the guard variable of an inline variable
   of the global namespace is GV and the variable's encoding (_ZGV3abc, as
   clang), though the variable's own symbol is its identifier */
static int calls = 0;
int next_value() { return ++calls; }

inline int abc = next_value();
namespace n
{
inline int def = next_value();
}
int plain = next_value();

int main()
{
    if (abc + n::def + plain != 6)
        return 1;
    if (calls != 3)
        return 2;
    return 0;
}

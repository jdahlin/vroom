// A variable template's specialization has no "Tn" type before its
// arguments in its name.
template <auto> int y = 3;
struct S {
    template <auto> static int w;
};
template <auto N> int S::w = 4;

static_assert(sizeof(int) == 4);

int main()
{
    if (y<0L> != 3)
        return 1;
    if (S::w<2L> != 4)
        return 2;
    return 0;
}

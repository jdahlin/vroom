/* [dcl.init]/6, /9: a value-initialized array member inside a partly
   dynamic static initializer is zeroed where it is (OpenTTD's
   _original_objects) */
template <int N>
struct array {
    int e[N];
};
struct Std {
    array<6> groups{};
};
struct E {
};
struct Spec {
    int c;
    Std grf;
    E x;
    unsigned char name;
};
int one = 1;
Spec table[] = {{one, Std{}, E{}, 1}};
int main()
{
    return table[0].c + table[0].grf.groups.e[5] - 1;
}

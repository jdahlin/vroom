/* [dcl.array]/8, [basic.link]/7: a bound given by a block's extern
   declaration is not the namespace scope's: the later definition there
   takes its bound from its own declarator or initializer */
struct C {
    int x, y;
};
inline int second(int i)
{
    extern const C table[2];
    return table[i].y;
}
inline int third(int i)
{
    extern const int bits[];
    return bits[i];
}
extern const C table[] = {{1, 2}, {3, 4}};
extern const int bits[3] = {5, 6, 7};
int main()
{
    if (second(0) != 2 || second(1) != 4)
        return 1;
    if (third(2) != 7)
        return 2;
    if (sizeof table != 2 * sizeof(C) || sizeof bits != 3 * sizeof(int))
        return 3;
    return 0;
}

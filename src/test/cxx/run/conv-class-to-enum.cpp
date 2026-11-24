/* [conv], [over.best.ics]/4: no standard conversion takes a class to bool or
   an enumeration, so such a candidate is not viable */
enum E { A, B };
struct C {};
struct Both {
    Both(int) {}
};

int pick(bool)
{
    return 1;
}
int pick(C)
{
    return 2;
}
int pick(E)
{
    return 3;
}

int main()
{
    C c;
    if (pick(c) != 2)
        return 1;
    if (pick(B) != 3)
        return 2;
    if (pick(true) != 1)
        return 3;
    return 0;
}

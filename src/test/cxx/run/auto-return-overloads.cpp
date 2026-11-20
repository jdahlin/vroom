/* [dcl.spec.auto]/3, [over.load]/3: functions of one name, with deduced
   return types, whose parameter lists differ: deleted and defined ones,
   with a function template of the name declared first ([temp.over]/1) */
template <class T, int = 0> auto pick(T) = delete;
auto pick(long double) = delete;
auto pick(int x)
{
    return x + 1;
}
auto pick(char c)
{
    return c;
}

int global = 4;
int main()
{
    if (pick(global) != 5)
        return 1;
    if (pick('a') != 'a')
        return 2;
    return 0;
}

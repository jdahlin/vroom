/* [temp.expl.spec]/12, [dcl.constexpr]/1: an explicit specialization of a
   member declared constexpr is inline: not emitted unused, weak used. */
template <class T> struct Check {
    static constexpr int Safe();
    static constexpr int Used();
};
template <> constexpr int Check<char>::Safe() { return 1; }
template <> constexpr int Check<char>::Used() { return 2; }
int main()
{
    int (*volatile f)() = &Check<char>::Used;
    return f() - 2;
}

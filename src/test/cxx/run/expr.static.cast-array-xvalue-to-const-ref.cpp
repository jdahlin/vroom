/* [expr.static.cast]/4: an array xvalue cast to a reference to an array of
   const ([basic.type.qualifier]/3: the array is const), as
   std::common_reference_with<char(&)[8], char(&&)[8]> needs */
int f(const char (&a)[3]) { return a[1]; }
int main()
{
    char a[3] = {1, 2, 3};
    const char(&q)[3] = static_cast<const char(&)[3]>(static_cast<char(&&)[3]>(a));
    if (f(static_cast<const char(&)[3]>(static_cast<char(&&)[3]>(a))) != 2)
        return 1;
    a[0] = 5;
    return q[0] - 5;
}

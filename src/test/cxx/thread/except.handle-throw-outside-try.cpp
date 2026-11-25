/* [except.throw]: a throw outside every try of a function that has one
   propagates to the caller's handler (the function's table covers it) */
void may()
{
}
void at(bool inside)
{
    if (inside) {
        try {
            may();
        } catch (int) {
        }
    } else {
        throw 1;
    }
}
int main()
{
    try {
        at(false);
    } catch (int k) {
        return k - 1;
    }
    return 2;
}

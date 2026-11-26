/* [ext:gnu_restrict] a restricted reference parameter and variable
   (Highway's VU32& HWY_RESTRICT ofs0) */
static void set(int &__restrict__ a, int &&__restrict b) { a = b; }
int main()
{
    int x = 0;
    int &__restrict__ r = x;
    set(r, 3);
    return x != 3;
}

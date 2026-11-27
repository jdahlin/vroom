/* The address one past the end of the topmost local, in a function that also
 * saves registers: it is at offset 0 from the frame base. */
__attribute__((noinline)) long past(void)
{
    int a[4] = {3, 5, 7, 11};
    int *p = a;
    int sum = 0;
    for (int i = 0; i < 4; i++)
        sum += *p++;
    return sum + (p - a);
}
int main(void)
{
    return past() != 30;
}

/* diag-flags: -Wno-unused-value */
struct Forward;
void test(void (*function)(void), struct Forward *object, int *array, void *bytes)
{
    function[0];
    0[function];
    object[0];
    0[object];
    array[1.5];
    1.5[array];
    /* GNU void-pointer subscripting remains valid. */
    bytes[0];
    0[bytes];
}

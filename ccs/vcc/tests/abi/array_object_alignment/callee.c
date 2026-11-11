/* Large arrays have target ABI object alignment beyond element alignment. */
char preceding = 1;
char short_array[15] = {8};
_Thread_local char tls_bytes[17] = {9};
int values[10] = {100,104,108,112,116,120,124,128,132,136};
char bytes[17] = {1};
char zeroes[17];
const char readonly_bytes[17] = {2};
static char local_static[17] = {3};
char nested[2][8] = {{4}};
char explicit_small[17] __attribute__((aligned(1))) = {5};
struct Record { char bytes[17]; } record = {{6}};

extern int check_pointer(void *);
int tls_array(void) { return check_pointer(tls_bytes); }
char *static_array(void) { return local_static; }
int local_array(void)
{
    char small = 7;
    char local[17];
    local[0] = small;
    return check_pointer(local);
}

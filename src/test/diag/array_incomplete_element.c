struct Forward;
typedef int UnknownBound[];
void void_array[2];
struct Forward record_array[2];
UnknownBound nested_array[2];
int multidimensional[2][];
void parameter(int a[][]);
void local(void)
{
    int a[2][];
}
/* These incomplete outer dimensions and pointer elements are valid. */
extern int valid_outer[][2];
extern struct Forward *valid_pointers[2];
void prototype_vla(int n, int a[][n], int b[][*]);

/* Guaranteed-native queries are integer constant expressions. Pointer
 * expressions in resolved lock-free queries are unevaluated. */
_Static_assert(__atomic_always_lock_free(1,0),"byte");
_Static_assert(__atomic_always_lock_free(2,(short*)0),"short");
_Static_assert(__atomic_always_lock_free(4,(int*)0),"int");
_Static_assert(__atomic_always_lock_free(8,(long long*)0),"long long");
_Static_assert(!__atomic_always_lock_free(3,0),"non-native width");
_Static_assert(!__atomic_always_lock_free(32,0),"wide");
_Static_assert(!__atomic_always_lock_free(8,(void*)1),"misaligned");
static int calls;
static int *ptr(void){calls++;return 0;}
struct __attribute__((packed)) P {char c;int i;};
_Alignas(8) static struct P packed;
int main(void){
 char *p=0;int *q=0;
 if(__atomic_always_lock_free(4,p))return 1;
 if(!__atomic_always_lock_free(4,q))return 2;
 if(__atomic_always_lock_free(4,(char *)&packed+1))return 3;
 if(!__atomic_always_lock_free(4,ptr())||calls)return 4;
 if(!__atomic_is_lock_free(4,ptr())||calls)return 5;
 return 0;
}

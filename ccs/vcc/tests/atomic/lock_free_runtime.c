/* Hosted: link -latomic. Compare unresolved size/alignment queries against
 * the target runtime ABI, including zero, non-power-of-two and wide sizes. */
extern _Bool runtime_query(unsigned long,const volatile void*) __asm__("__atomic_is_lock_free");
static unsigned size_calls,pointer_calls;
static unsigned long get_size(unsigned long n){size_calls++;return n;}
static void *get_pointer(void *p){pointer_calls++;return p;}
int main(void){
 _Alignas(64) unsigned char storage[128];
 for(unsigned long n=0;n<=64;n++)for(unsigned offset=0;offset<17;offset++){
  void *p=storage+offset;
  _Bool want=runtime_query(n,p);
  size_calls=pointer_calls=0;
  _Bool got=__atomic_is_lock_free(get_size(n),get_pointer(p));
  if(got!=want||size_calls!=1||pointer_calls!=1)return 1;
 }
 return 0;
}

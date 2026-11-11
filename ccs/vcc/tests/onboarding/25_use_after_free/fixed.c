typedef __SIZE_TYPE__ size_t;
void *malloc(size_t); void free(void *);
int main(void) { int *p=malloc(sizeof *p); if(!p)return 1; *p=7; int result=*p != 7; free(p); return result; }

typedef __SIZE_TYPE__ size_t;
void *malloc(size_t); void free(void *);
int main(void) { int *p=malloc(sizeof *p); if(!p)return 1; free(p); p=0; return p != 0; }

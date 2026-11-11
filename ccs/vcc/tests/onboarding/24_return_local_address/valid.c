typedef __SIZE_TYPE__ size_t;
void *malloc(size_t); void free(void *);
int *make(void) { int *p=malloc(sizeof *p); if(p)*p=3; return p; }
int main(void) { int *p=make(); if(!p)return 1; int result=*p != 3; free(p); return result; }

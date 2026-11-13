typedef _Bool V __attribute__((ext_vector_type(65)));
typedef union { V v; unsigned long integer; } B;
#define LANES 65
B flip(B,int,B);
B pressure(long,long,long,long,long,long,long,long,B);

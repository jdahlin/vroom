/* char/short/_Bool in registers: on Apple arm64 the producer extends them
   to 32 bits (callee for returns, caller for arguments) and the consumer
   trusts that; on Linux the consumer extends.  Values here have dirty
   high bits before the conversion. */
enum __attribute__((packed)) small { S_A = 1, S_B = 200 };

signed char retc(int x);
unsigned char retuc(int x);
short adds(short a, short b);
unsigned short retus(int x);
_Bool retb(int x);
enum small rete(int x);
signed char tailc(int x);
int takes(signed char c, unsigned short u, _Bool b, short s);
long takes_many(long a, long b, long c, long d, long e, long f, long g, long h, signed char i, short j, unsigned char k);
int via(signed char (*f)(int), int x);
int pass_on(int x);

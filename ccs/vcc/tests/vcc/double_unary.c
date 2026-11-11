static volatile unsigned observed;
static unsigned calls;
static unsigned next(void) { calls++; return observed; }
int du_neg(int x) { return -(-x); }
unsigned long long du_not(unsigned long long x) { return ~(~x); }
unsigned du_call(void) { return ~(~next()); }
unsigned du_volatile(void) { return ~(~observed); }
unsigned du_narrow(unsigned x) { return ~(unsigned short)~x; }
double du_float(double x) { return -(-x); }
int main(void)
{
 if (du_neg(-100) != -100 || du_neg(5) != 5 || du_not(~0ULL) != ~0ULL ||
     du_not(0) != 0 || du_narrow(0x12345678U) != 0xffff5678U || du_float(3.0) != 3.0) return 1;
 observed = 0x12345678U; calls = 0;
 if (du_call() != observed || calls != 1 || du_volatile() != observed) return 2;
 return 0;
}

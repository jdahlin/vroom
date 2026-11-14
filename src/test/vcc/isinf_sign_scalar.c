/* Signed classification of real scalar values evaluates its argument once. */
static int calls;
static volatile double values[] = {0.,-0.,1.,-1.,0x1p-1022,-0x1p-1022,
 __builtin_inf(),-__builtin_inf(),__builtin_nan("0x123"),-__builtin_nan("0x123")};
static double once(double x) { calls++; return x; }
static float oncef(float x) { calls++; return x; }
int main(void)
{
 for (unsigned i=0;i<sizeof(values)/sizeof(*values);i++) {
  int expected=i==6?1:i==7?-1:0;
  if (__builtin_isinf_sign(once(values[i]))!=expected || calls!=(int)(2*i+1)) return 1;
  if (__builtin_isinf_sign(oncef((float)values[i]))!=expected || calls!=(int)(2*i+2)) return 2;
 }
 return 0;
}

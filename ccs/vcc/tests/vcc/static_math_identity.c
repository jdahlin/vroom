/* A file-local function with a library name keeps its own semantics. */
static double fabs(double x) { return -x; }
int main(void)
{
	volatile double input = 1.0;
	double value = fabs(input), zero = 0.0;
	if (!(value < zero)) return 1;
	return value < zero ? 0 : 2;
}

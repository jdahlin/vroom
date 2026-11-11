static const unsigned short file_array[] = (const unsigned short[]){ 0x0d2b, 0x1234 };
static int matrix[][2] = (int[][2]){ { 1, 2 }, { 3, 4 } };
static int input = 7;
__attribute__((const)) static int twice(int n) { return n * 2; }
int main(void)
{
	static const unsigned short local_array[] = (const unsigned short[]){ 0xabcd, 0x7654 };
	int n = input;
	int values[] = (int[]){ n, n + 1, twice(n) };
	int fixed[4] = (int[4]){ 11, 22 };
	char chars[] = (char[]){ 'x', 'y', 0 };
	if (sizeof file_array != 4 || file_array[0] != 0x0d2b || file_array[1] != 0x1234) return 1;
	if (sizeof local_array != 4 || local_array[0] != 0xabcd || local_array[1] != 0x7654) return 2;
	if (sizeof values != 3 * sizeof(int) || values[0] != 7 || values[1] != 8 || values[2] != 14) return 3;
	if (fixed[0] != 11 || fixed[1] != 22 || fixed[2] || fixed[3]) return 4;
	if (sizeof chars != 3 || chars[1] != 'y' || chars[2]) return 5;
	return matrix[0][1] != 2 || matrix[1][0] != 3;
}

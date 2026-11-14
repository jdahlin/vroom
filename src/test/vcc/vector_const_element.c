/* A vector of const elements is not itself const: clang assigns to one (libc++'s __simd_vector<const T, N>). */
typedef const short cv4 __attribute__((ext_vector_type(4)));
typedef const int ci4 __attribute__((vector_size(16)));

int main(void)
{
	cv4 v = {1, 2, 3, 4}, w;
	ci4 x = {5, 6, 7, 8}, y;

	w = v;
	y = x;
	return w[2] == 3 && y[3] == 8 ? 0 : 1;
}

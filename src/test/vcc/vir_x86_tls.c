/* A TLS address and its interior pointer must survive calls and control-flow joins. */
#if defined(__linux__) && defined(__x86_64__)
static _Thread_local int values[3] = { 1, 2, 3 };

__attribute__((noinline)) int *tls_member(int last)
{
	return last ? &values[2] : &values[1];
}

__attribute__((noinline)) int tls_update(int last)
{
	int *p = tls_member(last);
	*p += values[0];
	return *p;
}

int main(void)
{
	return tls_update(0) != 3 || tls_update(1) != 4 || tls_update(0) != 4;
}
#else
int main(void) { return 0; }
#endif

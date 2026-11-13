static int value;

static int update(int *p)
{
	value = 1;
	__asm__ volatile ("" : : : "memory");
	*p = 2;
	__asm__ volatile ("" : : : "memory", "cc");
	return value;
}

int main(void)
{
	__asm__("");
	return update(&value) != 2;
}

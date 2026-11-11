/* Void dereference is a GNU extension: keep address side effects without
   reading memory, including through a volatile-qualified void pointer. */
static int calls;

static void *pointer(void)
{
	calls++;
	return (void *)0;
}

static void check(int condition)
{
	void *p = 0;
	const void *cp = 0;
	volatile void *vp = 0;

	*p;
	*cp;
	*vp;
	condition ? *p : *vp;
	*pointer();
	condition ? *pointer() : *(volatile void *)pointer();
	*pointer(), *pointer();
}

int main(void)
{
	check(0);
	check(1);
	return calls != 8;
}

extern char incomplete[];
static char internal[4];
char hidden[5];
static int probe(void)
{
	extern char nested[10];
	{
		extern char nested[];
		if (sizeof nested != 10) return 1;
		nested[9] = 7;
	}
	{
		extern char incomplete[10];
		if (sizeof incomplete != 10) return 2;
	}
	/* Completion in the inner scope does not change the outer array type. */
	__typeof__(incomplete) a = { 1, 2, 3 };
	if (sizeof a != 3) return 3;
	{
		extern char local_incomplete[];
		{
			extern char local_incomplete[12];
			if (sizeof local_incomplete != 12) return 4;
		}
		__typeof__(local_incomplete) b = { 4, 5 };
		if (sizeof b != 2) return 5;
	}
	{
		extern char internal[];
		if (sizeof internal != 4) return 6;
		internal[3] = 9;
	}
	{
		char hidden[8] = { 0 };
		{
			extern char hidden[];
			__typeof__(hidden) fresh = { 1, 2 };
			if (sizeof fresh != 2) return 7;
			hidden[4] = 11;
		}
		if (sizeof hidden != 8 || hidden[4]) return 8;
	}
	return 0;
}
char nested[10], incomplete[10], local_incomplete[12];
int main(void)
{
	int r = probe();
	if (r) return r;
	return nested[9] != 7 || internal[3] != 9 || hidden[4] != 11;
}

enum e { A, B };
int f(enum e x)
{
	switch (x) {
	case A: return 1;
	}
	return 0;
}

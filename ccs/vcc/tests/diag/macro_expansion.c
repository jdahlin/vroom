/* diag-formats: JSON and SARIF snapshots (.json, .sarif) */
#define USE x
#define ADD(a) ((a) + nope)
int f(int v)
{
	int y = USE;
	return ADD(v) + y;
}

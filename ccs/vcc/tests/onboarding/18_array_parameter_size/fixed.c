unsigned long count(const int *a, unsigned long n) { (void)a; return n; }
int main(void) { int a[10]={0}; return count(a, sizeof a/sizeof a[0]) != 10; }

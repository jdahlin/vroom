int main(void) { int a[10]={0}; return sizeof a / sizeof a[0] != 10; }

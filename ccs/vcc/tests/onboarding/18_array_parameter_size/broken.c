unsigned long count(int a[10]) {
    return sizeof a / sizeof a[0];
}
int main(void) { int a[10]={0}; return count(a) != 10; }

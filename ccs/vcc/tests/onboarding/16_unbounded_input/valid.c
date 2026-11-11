int scanf(const char *, ...);
int main(void) { char bytes[8]; return scanf("%8c", bytes) == 1 ? 0 : 1; }

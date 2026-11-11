/* GCC accepts this extension when return-mismatch is downgraded. */
#pragma clang diagnostic warning "-Wreturn-type"
static int calls;
static int side_effect(void) { calls++; return 17; }
static void f(void) { return side_effect(); }
int main(void) { f(); return calls != 1; }

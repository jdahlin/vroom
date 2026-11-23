// void(): an expression of type void (libc++ __hash_table: for (p = q, void(), q = q->next; ...))
int f(int a) { int b; b = a, void(), b = b + 1; return b; }
int main() { (void)0, void(); return f(1) == 2 ? 0 : 1; }

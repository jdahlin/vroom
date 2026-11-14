// asm("text") and asm volatile("text" : :) with no operands (doctest's DOCTEST_BREAK_INTO_DEBUGGER)
int f(int x) {
  if (x > 100)
    asm("int $3\n" : :);
  asm volatile("nop");
  __asm__("");
  return x + 1;
}
int main() { return f(1) == 2 ? 0 : 1; }

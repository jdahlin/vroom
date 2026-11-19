// [ext:gnu_attributes] several gnu-attribute-specifiers after a function's
// parameters (macOS's sys/stdio.h: renamex_np's availability attributes)
int sum(int, int) __attribute__((availability(macosx, introduced = 10.12)))
__attribute__((availability(ios, introduced = 10.0))) __attribute__((cold));

int sum(int a, int b) { return a + b; }

int main() { return sum(2, 3) == 5 ? 0 : 1; }

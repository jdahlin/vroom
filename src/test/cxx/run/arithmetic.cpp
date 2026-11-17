// operators and conversions: [expr.mul], [expr.add], [expr.shift],
// [expr.rel], [expr.log.and], [expr.ass], [conv]
long mix(int a, unsigned b, long c) {
    long r = a * 3 - (long)b / 2 + c % 7;
    r <<= 2;
    r ^= a >> 1;
    r |= b & 0xf0;
    return r;
}

double average(const double *v, int n) {
    double total = 0;
    for (int i = 0; i < n; i++)
        total += v[i];
    return total / n;
}

int pointers(int *p, int n) {
    int *end = p + n;
    int count = 0;
    for (int *q = p; q < end; q++)
        if (*q > 2 && !(*q == 9) || *q < 0)
            count++;
    long distance = end - p;
    int *mid = 2 + p;
    return count + (int)distance + *mid + (end[-1]);
}

bool parity(unsigned x) {
    bool odd = false;
    while (x) {
        odd = !odd;
        x &= x - 1;
    }
    return odd;
}

int main() {
    double v[3] = {1.5, 2.5, 5.0};
    int a[5] = {1, 9, 3, -2, 7};
    int i = 10;
    int j = i++ + ++i;
    float f = 2.75f;
    int g = (int)(f * 4);
    char ch = 'A';
    ch += 2;
    unsigned char u = 250;
    u += 10;
    long m = mix(7, 300u, -20);
    int r = (int)average(v, 3) + pointers(a, 5) + parity(7) + parity(6) +
            j + g + ch + u + (int)(m & 0xff);
    return r & 255;
}

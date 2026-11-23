// statements: [stmt.select], [stmt.iter], [stmt.jump], [stmt.label]
int collatz(int n) {
    int steps = 0;
    while (n != 1) {
        if (n % 2 == 0)
            n /= 2;
        else
            n = 3 * n + 1;
        ++steps;
    }
    return steps;
}

int classify(int x) {
    switch (x) {
    case 0:
        return 10;
    case 1:
    case 2:
        x += 5;
        break;
    case 7: {
        int y = x * 2;
        return y;
    }
    default:
        x = -x;
    }
    return x;
}

int with_goto(int n) {
    int total = 0;
again:
    total += n;
    if (--n > 0)
        goto again;
    return total;
}

int conditions(int n) {
    int sum = 0;
    if (int d = n - 3)
        sum += d;
    else
        sum += 100;
    if (int k = n; k > 2)
        sum += k;
    int i = n;
    while (int j = i--)
        sum += j;
    for (int a = 0, b = 10; a < b; a += 2, --b) {
        if (a == 4)
            continue;
        sum += a * b;
    }
    switch (int s = n & 3; s) {
    case 1:
        sum += 7;
        break;
    default:
        sum += s;
    }
    do
        sum -= 3;
    while (sum > 200);
    for (;;) {
        if (sum > 50)
            break;
        sum += 11;
    }
    return sum;
}

int main() {
    int r = collatz(27) + classify(0) + classify(2) + classify(7) +
            classify(-4) + with_goto(5) + conditions(5) + conditions(3);
    return r & 255;
}

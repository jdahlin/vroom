double average(int total, int count) {
    return (double)total / count;
}
int main(void) { return average(5, 2) != 2.5; }

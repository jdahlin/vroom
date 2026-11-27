template <> int which<1>() { return 42; }
int main() { return use() != 42; }

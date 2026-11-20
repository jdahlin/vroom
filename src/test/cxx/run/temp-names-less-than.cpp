/* [temp.names]: less-than when the name is not a template */
int a = 1, b = 2;
int main() { bool x = a < b; bool y = (a < b) > (b > a); return x && !y ? 0 : 1; }

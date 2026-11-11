int strcmp(const char *, const char *);
int main(void) {
    char answer[] = "yes";
    return strcmp(answer, "yes") == 0 ? 0 : 1;
}

void f(void) {
    (void)(u8"a" "b" "c" "d" u"e");
    (void)(U"a" "b" "c" "d" L"e");
    (void)("\x10000" "b" "c" "d" u"e");
}

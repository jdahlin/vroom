int iv; short s;
void f(void) {
	__sync_fetch_and_nand(&iv, 6);
	__sync_nand_and_fetch(&s, 1);
	__sync_fetch_and_max(&iv, 2);
}

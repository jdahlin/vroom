// return inside a try block's body (json's at(): try { return v.at(i); } catch (...) { throw ...; }):
// void, scalar and reference results, the throwing path and the normal one
static int arr[3] = { 10, 20, 30 };
static int &at(int i) { if (i > 2) throw i; return arr[i]; }
static int &get(int i) {
	try { return at(i); }
	catch (int k) { throw k + 100; }
}
static long twice(int i) {
	try { if (i < 0) throw -1; return at(i) * 2L; }
	catch (int) { }
	return -7;
}
static int hits;
static void touch(int i) {
	try { if (i) return; hits += 10; }
	catch (...) { }
	hits++;
}
int main()
{
	int r = 0;
	get(1) = 21;
	try { get(5); } catch (int k) { r = k; }
	touch(1), touch(0);
	return arr[1] == 21 && r == 105 && twice(2) == 60 && twice(-1) == -7 && twice(9) == -7 && hits == 11 ? 0 : 1;
}

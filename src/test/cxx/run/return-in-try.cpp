// return inside a try block (outlined into a function of its own): the value kept in the enclosing
// function's slot, which then returns it; by value, by reference, void, from a handler, and nested
// (json's basic_json::at(size_t): try { return m_data.m_value.array->at(idx); } catch (...) { throw ...; })
struct J {
	int v[3] = {1, 2, 3};
	int &get(int i) { if (i < 0 || i > 2) throw i; return v[i]; }
	int &at(int i) { try { return get(i); } catch (int) { throw -1; } }
	int val(int i) { try { return get(i) * 10; } catch (int) { return -5; } }
	void set(int i, int x) { try { if (i == 1) return; get(i) = x; } catch (int) { } }
	int nested(int i) { try { try { return get(i) + 100; } catch (int) { return 7; } } catch (...) { return 8; } }
};
int main() {
	J j; int r = 0;
	j.at(1) = 20;
	try { j.at(5); } catch (int e) { r += e == -1; }
	j.set(0, 9); j.set(1, 99);
	return j.v[1] == 20 && j.val(2) == 30 && j.val(9) == -5 && j.v[0] == 9 && j.nested(0) == 109 && j.nested(4) == 7 && r == 1 ? 0 : 1;
}

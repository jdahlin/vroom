int single(_BitInt(310) x) {
 switch (x) { case ((_BitInt(310))1 << 200): return 1; default: return 0; }
}
int range(_BitInt(310) x) {
 switch (x) { case 0 ... ((_BitInt(310))1 << 201): return 1; default: return 0; }
}
int unsigned_single(unsigned _BitInt(193) x) {
 switch (x) { case (unsigned __int128)1 << 127: return 1; default: return 0; }
}
int unsigned_range(unsigned _BitInt(193) x) {
 switch (x) { case 0 ... (unsigned __int128)1 << 127: return 1; default: return 0; }
}

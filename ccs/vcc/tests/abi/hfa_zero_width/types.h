struct complex_float { _Complex float x; char : 0; };
struct doubles { int : 0; double x; long : 0; double y; };
struct padded { float x; long : 0; float y; };
struct bits { float x; unsigned b : 1; };
struct complex_float cf_roundtrip(struct complex_float);
struct doubles doubles_roundtrip(struct doubles);
struct padded padded_roundtrip(struct padded);
struct bits bits_roundtrip(struct bits);

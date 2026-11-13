/* diag-flags: -std=c23 */
enum A : unsigned char;
enum A : unsigned short { A0 };
enum B;
enum B : int { B0 };
enum C : int;
enum C { C0 };
enum D : unsigned char { D0 = 256 };
enum E : unsigned char { E0 = 255, E1 };
enum F : signed char { F0 = -129 };
enum G : unsigned long long { G0 = 18446744073709551615ULL, G1 };
enum H : float { H0 };
enum J : int j;

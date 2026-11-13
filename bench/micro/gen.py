#!/usr/bin/env python3
"""Generates a micro project: one big header (big.h) and N small .c files.

The header should resemble the kernel's headers: many structs, enums, macros, prototypes and
above all many `static inline` functions, of which each .c file uses only
a few. No system headers are used, so all compilers parse the same code.

    python3 gen.py [--out DIR] [--files 10] [--scale 1.0] [--seed 1]
"""
import argparse
import os
import random

TYPES = ["int", "unsigned int", "long", "unsigned long", "short", "unsigned char",
         "u8", "u16", "u32", "u64", "s32", "s64"]


def gen_header(rng, scale):
    n_struct = int(600 * scale)
    n_enum = int(300 * scale)
    n_macro = int(800 * scale)
    n_proto = int(2500 * scale)
    n_inline = int(2000 * scale)

    out = []
    w = out.append
    w("#ifndef BIG_H\n#define BIG_H\n")
    w("typedef unsigned char u8;\ntypedef unsigned short u16;\n"
      "typedef unsigned int u32;\ntypedef unsigned long long u64;\n"
      "typedef int s32;\ntypedef long long s64;\ntypedef unsigned long size_t;\n"
      "#define NULL ((void *)0)\n")

    for i in range(n_macro):
        k = i % 4  # MASK_i exists when i % 4 == 1
        if k == 0:
            w(f"#define CONST_{i} {rng.randrange(1, 1 << 20)}\n")
        elif k == 1:
            w(f"#define MASK_{i}(x) (((x) >> {rng.randrange(0, 16)}) & 0x{rng.randrange(1, 0xffff):x})\n")
        elif k == 2:
            w(f"#define MAX_{i}(a, b) ((a) > (b) ? (a) : (b))\n")
        else:
            w(f"#define FLAG_{i} (1u << {rng.randrange(0, 31)})\n")

    for i in range(n_enum):
        vals = ", ".join(f"E{i}_V{j} = {j * rng.randrange(1, 5)}" for j in range(10))
        w(f"enum e{i} {{ {vals} }};\n")

    for i in range(n_struct):
        w(f"struct s{i} {{\n\tlong f0;\n")
        for j in range(1, rng.randrange(6, 14)):
            if i > 0 and rng.random() < 0.15:
                w(f"\tstruct s{rng.randrange(0, i)} *p{j};\n")
            elif rng.random() < 0.1:
                w(f"\t{rng.choice(TYPES)} arr{j}[{rng.randrange(2, 16)}];\n")
            else:
                w(f"\t{rng.choice(TYPES)} f{j};\n")
        w("};\n")
        if rng.random() < 0.3:
            w(f"typedef struct s{i} s{i}_t;\n")

    for i in range(n_proto):
        args = ", ".join(f"{rng.choice(TYPES)} a{j}" for j in range(rng.randrange(0, 5))) or "void"
        if rng.random() < 0.3:
            args = f"struct s{rng.randrange(n_struct)} *s" + ("" if args == "void" else ", " + args)
        w(f"extern {rng.choice(TYPES)} ext_{i}({args});\n")

    for i in range(n_inline):
        sn = rng.randrange(n_struct)
        w(f"static inline long inl_{i}(struct s{sn} *s, long x)\n{{\n")
        w("\tlong r = x;\n")
        for j in range(rng.randrange(3, 10)):
            k = rng.randrange(5)
            if k == 0:
                m = rng.randrange(n_macro)
                use = f"MASK_{m}(r)" if m % 4 == 1 else f"(r >> {rng.randrange(1, 7)})"
                w(f"\tr = r * {rng.randrange(2, 9)} + {use};\n")
            elif k == 1:
                w(f"\tif (r & {1 << rng.randrange(0, 12)}) r ^= {rng.randrange(1, 1000)};\n")
            elif k == 2:
                w(f"\tfor (int i{j} = 0; i{j} < {rng.randrange(2, 8)}; i{j}++) r += i{j} * {rng.randrange(1, 5)};\n")
            elif k == 3 and i > 0:
                w(f"\tr += inl_{rng.randrange(max(0, i - 50), i)}((void *)s, r);\n")
            else:
                w(f"\tr -= (long)sizeof(struct s{rng.randrange(n_struct)});\n")
        w("\treturn s ? r + (long)s->f0 : r;\n}\n")
    w("#endif\n")
    return "".join(out), n_struct, n_inline, n_proto


def gen_c(rng, idx, n_struct, n_inline, n_proto):
    out = ['#include "big.h"\n\n']
    w = out.append
    for f in range(8):
        sn = rng.randrange(n_struct)
        w(f"long file{idx}_fn{f}(struct s{sn} *s, long n)\n{{\n\tlong acc = 0;\n")
        for _ in range(rng.randrange(8, 16)):
            k = rng.randrange(3)
            if k == 0:
                # No struct cast: the inline functions take different struct types, so we pass NULL
                w(f"\tacc += inl_{rng.randrange(n_inline)}((void *)0, acc + n);\n")
            elif k == 1:
                w(f"\tfor (long i = 0; i < n; i++) acc = acc * 3 + (i ^ {rng.randrange(1, 255)});\n")
            else:
                w(f"\tif (acc > {rng.randrange(100, 10000)}) acc -= n; else acc += {rng.randrange(1, 99)};\n")
        w("\treturn s ? acc + (long)s->f0 : acc;\n}\n\n")
    return "".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "proj"))
    ap.add_argument("--files", type=int, default=10)
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()

    rng = random.Random(a.seed)
    os.makedirs(a.out, exist_ok=True)
    hdr, n_struct, n_inline, n_proto = gen_header(rng, a.scale)
    with open(os.path.join(a.out, "big.h"), "w") as f:
        f.write(hdr)
    for i in range(a.files):
        with open(os.path.join(a.out, f"f{i}.c"), "w") as f:
            f.write(gen_c(rng, i, n_struct, n_inline, n_proto))
    with open(os.path.join(a.out, "empty.c"), "w") as f:
        f.write('#include "big.h"\n')
    with open(os.path.join(a.out, "nothing.c"), "w") as f:
        f.write("int nothing;\n")
    print(f"big.h: {hdr.count(chr(10))} lines, {len(hdr) / 1e6:.2f} MB; "
          f"{a.files} .c files in {a.out}")


if __name__ == "__main__":
    main()

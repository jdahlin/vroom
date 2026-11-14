#!/usr/bin/env python3
"""PLAN-cxx.md X0: where clang spends a C++ build, and how much repeats across TUs.

  bench/cxx-profile.py build TARGET [-j 6]   compile with clang -ftime-trace into work/cxx/out/TARGET
  bench/cxx-profile.py summarize TARGET     reduce leftover raw traces to TU.sum.json
  bench/cxx-profile.py report TARGET         phases, duplicated instantiations, COMDAT identity
  bench/cxx-profile.py base TARGET [-j 6]    wall/CPU: plain, PCH, unity (json only)

TARGET: json (nlohmann/json unit tests), libcxx (libc++ tests for a header
subset), duckdb, simdjson, catch2 (amalgamations, one TU each).  Sources come
from work/cxx (see PLAN-cxx.md X0 for how they were fetched).  Objects are
-O0 -g0 with libc++.

COMDAT identity: for every section group (a COMDAT function or variable) the
member sections' bytes and relocations (offset, type, symbol name, addend)
are hashed; a group whose hash differs between objects is code a cache keyed
on the mangled name alone would get wrong.
"""
import argparse, collections, glob, hashlib, json, os, resource, struct, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.environ.get("VROOM_WORK") or os.path.join(ROOT, "work")
CXX = os.path.join(WORK, "cxx")
CLANG = os.environ.get("CLANGXX", "clang++-23")
BASE = ["-O0", "-g0", "-stdlib=libc++", "-fno-color-diagnostics", "-w"]


def tus(target):
    """(name, source, flags) per TU."""
    if target == "json":
        j = os.path.join(CXX, "json")
        fl = ["-std=c++20", "-I", j + "/single_include", "-I", j + "/tests/thirdparty/doctest",
              "-I", j + "/tests/thirdparty/fifo_map", "-DDOCTEST_CONFIG_SUPER_FAST_ASSERTS",
              "-I", j + "/gen"]     # gen/test_data.hpp, as json's cmake/download_test_data.cmake writes it
        srcs = sorted(s for s in glob.glob(j + "/tests/src/unit*.cpp"))
        out = []
        for s in srcs:     # per-test options from json's tests/CMakeLists.txt
            n = os.path.basename(s)[:-4]
            extra = ["-DJSON_NOEXCEPTION", "-fno-exceptions"] if n == "unit-disabled_exceptions" else []
            out.append((n, s, fl + extra))
            if n == "unit-comparison":
                out.append(("unit-comparison_legacy", s, fl + ["-DJSON_USE_LEGACY_DISCARDED_VALUE_COMPARISON=1"]))
        return out
    if target == "libcxx":
        t = os.path.join(CXX, "llvm/libcxx/test")
        fl = ["-std=c++26", "-I", t + "/support", "-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE"]
        srcs = sorted(glob.glob(t + "/std/**/*.pass.cpp", recursive=True))
        return [(os.path.relpath(s, t + "/std").replace("/", "__")[:-9], s, fl) for s in srcs]
    a = os.path.join(CXX, "amalg")
    if target == "duckdb":
        # libstdc++: libc++'s <locale> pulls in glibc's <langinfo.h>, whose
        # GROUPING macro breaks DuckDB's parser enum.
        return [("duckdb", a + "/duckdb/duckdb.cpp", ["-std=c++17", "-I", a + "/duckdb", "-stdlib=libstdc++"])]
    if target == "simdjson":
        d = a + "/simdjson-4.2.2/singleheader"
        return [("simdjson", d + "/simdjson.cpp", ["-std=c++20", "-I", d])]
    if target == "catch2":
        d = a + "/Catch2-3.11.0/extras"
        return [("catch2", d + "/catch_amalgamated.cpp", ["-std=c++17", "-I", d])]
    sys.exit("unknown target " + target)


def outdir(target, kind="obj"):
    d = os.path.join(CXX, "out", target, kind)
    os.makedirs(d, exist_ok=True)
    return d


def run_cc(args):
    t0 = time.time()
    r = subprocess.run(args, capture_output=True, text=True)
    return r.returncode, time.time() - t0, r.stderr


def build(target, jobs, extra=(), kind="obj", trace=True):
    d = outdir(target, kind)
    cmds = []
    for name, src, fl in tus(target):
        o = os.path.join(d, name + ".o")
        a = [CLANG, *BASE, *fl, *extra, "-c", src, "-o", o]
        if trace:
            a += ["-ftime-trace", "-ftime-trace-granularity=0"]
        cmds.append((name, a))
    ru0 = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = time.time()
    fails = []
    with ThreadPoolExecutor(jobs) as ex:
        for (name, _), (rc, dt, err) in zip(cmds, ex.map(lambda c: run_cc(c[1]), cmds)):
            if rc:
                fails.append((name, err.splitlines()[:3]))
    wall = time.time() - t0
    ru1 = resource.getrusage(resource.RUSAGE_CHILDREN)
    cpu = (ru1.ru_utime - ru0.ru_utime) + (ru1.ru_stime - ru0.ru_stime)
    print(f"{target} {kind}: {len(cmds) - len(fails)}/{len(cmds)} TUs, wall {wall:.1f} s, cpu {cpu:.1f} s")
    for n, e in fails[:5]:
        print("  fail", n, *e[:1])
    with open(os.path.join(d, "failed.txt"), "w") as f:
        f.write("".join(n + "\n" for n, _ in fails))
    if trace:
        summarize_all(target, jobs)
    return wall, cpu


# --- time traces ---------------------------------------------------------------

PHASES = ["Total ExecuteCompiler", "Total Frontend", "Total Source", "Total ParseClass",
          "Total ParseTemplate", "Total InstantiateClass", "Total InstantiateFunction",
          "Total PerformPendingInstantiations", "Total CodeGen Function", "Total Backend",
          "Total Optimizer", "Total CodeGenPasses"]


INST = ("InstantiateFunction", "InstantiateClass")


def summarize(trace):
    """Reduce one -ftime-trace file (hundreds of MB at granularity 0) to
    TRACE.sum.json: phase totals and per-instantiation count and duration;
    the raw trace is deleted."""
    with open(trace) as f:
        ev = json.load(f)["traceEvents"]
    ph = collections.Counter()
    inst = {k: {} for k in INST}
    for e in ev:
        nm = e.get("name", "")
        if nm in PHASES:
            ph[nm] += e.get("dur", 0)
        elif nm in inst:
            det = e.get("args", {}).get("detail", "")
            c = inst[nm].setdefault(det, [0, 0])
            c[0] += 1
            c[1] += e.get("dur", 0)
    with open(trace[:-5] + ".sum.json", "w") as f:
        json.dump({"phases": ph, "inst": inst}, f)
    os.unlink(trace)


def summarize_all(target, jobs):
    from concurrent.futures import ProcessPoolExecutor
    ts = [t for t in glob.glob(outdir(target) + "/*.json") if not t.endswith(".sum.json")]
    with ProcessPoolExecutor(jobs) as ex:
        list(ex.map(summarize, ts))


def summaries(target):
    for t in sorted(glob.glob(outdir(target) + "/*.sum.json")):
        with open(t) as f:
            yield os.path.basename(t)[:-9], json.load(f)


# --- ELF section groups ----------------------------------------------------------

def elf_groups(path):
    """{signature: digest} for every SHT_GROUP in a relocatable ELF64 LE object, and its size."""
    b = open(path, "rb").read()
    shoff, = struct.unpack_from("<Q", b, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", b, 0x3A)
    sh0 = struct.unpack_from("<IIQQQQIIQQ", b, shoff)
    if shnum == 0:          # more than 0xff00 sections: the count is in section 0
        shnum = sh0[5]
    if shstrndx == 0xFFFF:
        shstrndx = sh0[6]
    sh = [struct.unpack_from("<IIQQQQIIQQ", b, shoff + i * shentsize) for i in range(shnum)]
    xndx = next((i for i, s in enumerate(sh) if s[1] == 18), None)   # SHT_SYMTAB_SHNDX
    def sec(i):
        _, _, _, _, off, size, *_ = sh[i]
        return b[off:off + size]
    def cstr(tab, o):
        return tab[o:tab.index(b"\0", o)].decode()
    shstr = sec(shstrndx)
    symtab = next(i for i, s in enumerate(sh) if s[1] == 2)
    strtab = sec(sh[symtab][6])
    syms = sec(symtab)
    xn = sec(xndx) if xndx is not None else b""
    def symshndx(i, shndx):
        return struct.unpack_from("<I", xn, i * 4)[0] if shndx == 0xFFFF else shndx
    def symname(i):
        name, info, _, shndx = struct.unpack_from("<IBBH", syms, i * 24)
        shndx = symshndx(i, shndx)
        if name == 0 and (info & 0xF) == 3:   # section symbol
            return "§" + cstr(shstr, sh[shndx][0])
        return cstr(strtab, name)
    def target(i, rtype, add):
        """A relocation's target in TU-independent terms: a local string or
        constant is named by its contents, since its offset depends on what
        else the TU put in the same merged section."""
        name, info, _, shndx = struct.unpack_from("<IBBH", syms, i * 24)
        shndx = symshndx(i, shndx)
        value, = struct.unpack_from("<Q", syms, i * 24 + 8)
        if (info >> 4) != 0 or shndx == 0 or shndx in (0xFFF1, 0xFFF2):   # global/weak or undefined: the name
            return symname(i).encode(), add
        s = sh[shndx]
        pcrel = rtype in (2, 4, 24)        # R_X86_64_PC32, PLT32, PC64: addend is -4 from the target
        off = value + add + (4 if pcrel else 0)
        data = sec(shndx) if s[1] != 8 else b""
        if s[2] & 0x20:                    # SHF_STRINGS
            end = data.index(b"\0", off) if b"\0" in data[off:] else len(data)
            return b"str:" + data[off:end], 0
        if s[2] & 0x10 and s[9]:           # SHF_MERGE constants (.LCPI pools): the entry's bytes
            return b"cst:" + data[off:off + s[9]], 0
        if not name or (info & 0xF) == 3:
            # a local function or object (a header's `static` function, a
            # local constant): its name and bytes, wherever the TU put it
            for v, sz, nm in local_syms.get(shndx, ()):
                if v <= off < v + max(sz, 1):
                    return b"loc:" + nm.encode() + b":" + data[v:v + sz], off - v
            return b"sec:" + cstr(shstr, s[0]).encode() + b":" + data[off:off + 64], 0
        return symname(i).encode(), add
    local_syms = collections.defaultdict(list)    # shndx -> (value, size, name) of named local functions/objects
    for k in range(len(syms) // 24):
        nm_, info_, _, shndx_ = struct.unpack_from("<IBBH", syms, k * 24)
        if (info_ >> 4) == 0 and (info_ & 0xF) in (1, 2) and nm_:
            v_, sz_ = struct.unpack_from("<QQ", syms, k * 24 + 8)
            local_syms[symshndx(k, shndx_)].append((v_, sz_, cstr(strtab, nm_)))
    relas = collections.defaultdict(list)
    for i, s in enumerate(sh):
        if s[1] == 4:
            relas[s[7]].append(i)
    out = {}
    for i, s in enumerate(sh):
        if s[1] != 17:     # SHT_GROUP
            continue
        sig = symname(s[7])
        g = sec(i)
        members = struct.unpack_from("<%dI" % (len(g) // 4), g)[1:]
        h = hashlib.blake2b(digest_size=16)
        size = 0
        for m in members:
            ms = sh[m]
            if ms[1] == 4:
                continue
            nm = cstr(shstr, ms[0])
            h.update(nm.encode() + b"\0")
            if ms[1] != 8:       # not NOBITS
                h.update(sec(m))
            size += ms[5]
            for r in relas.get(m, ()):
                rb = sec(r)
                for k in range(0, len(rb), 24):
                    off, info, add = struct.unpack_from("<QQq", rb, k)
                    tname, tadd = target(info >> 32, info & 0xFFFFFFFF, add)
                    h.update(struct.pack("<QIq", off, info & 0xFFFFFFFF, tadd) + tname + b"\0")
        out[sig] = (h.hexdigest(), size)
    return out


def text_size(path):
    b = open(path, "rb").read()
    shoff, = struct.unpack_from("<Q", b, 0x28)
    shentsize, shnum, _ = struct.unpack_from("<HHH", b, 0x3A)
    if shnum == 0:
        shnum = struct.unpack_from("<IIQQQQIIQQ", b, shoff)[5]
    tot = 0
    for i in range(shnum):
        s = struct.unpack_from("<IIQQQQIIQQ", b, shoff + i * shentsize)
        if s[1] == 1 and s[2] & 4:   # PROGBITS, executable
            tot += s[5]
    return tot


def report(target):
    phase = collections.Counter()
    inst_total = collections.Counter()      # kind -> events across TUs
    inst_names = {"InstantiateFunction": collections.Counter(), "InstantiateClass": collections.Counter()}
    inst_us = collections.Counter()          # kind -> microseconds of first-level events
    first_us = collections.Counter()
    n = 0
    for name, s in summaries(target):
        n += 1
        phase.update(s["phases"])
        for nm, dets in s["inst"].items():
            c = inst_names[nm]
            for det, (cnt, us) in dets.items():
                if det not in c:
                    first_us[nm] += us / cnt      # one instantiation's time, the first TU that needed it
                c[det] += cnt
                inst_total[nm] += cnt
                inst_us[nm] += us
    if not n:
        sys.exit("no trace summaries; run build first")
    tot = phase["Total ExecuteCompiler"] or 1
    print(f"== {target}: {n} TUs, clang time {tot / 1e6:.1f} s (sum over TUs)")
    for p in PHASES[1:]:
        if phase[p]:
            print(f"  {p[6:]:<28} {phase[p] / 1e6:8.2f} s  {100 * phase[p] / tot:5.1f} %")
    for k, c in inst_names.items():
        tot_e = inst_total[k]
        if not tot_e:
            continue
        print(f"  {k}: {tot_e} events, {len(c)} distinct ({100 * (1 - len(c) / tot_e):.1f} % repeats); "
              f"time {inst_us[k] / 1e6:.2f} s, of which first-seen {first_us[k] / 1e6:.2f} s "
              f"(nested events counted in their parent too)")
    # COMDAT groups
    groups = collections.defaultdict(list)
    tsize = 0
    nobj = 0
    for o in sorted(glob.glob(outdir(target) + "/*.o")):
        nobj += 1
        tsize += text_size(o)
        for sig, (dig, size) in elf_groups(o).items():
            groups[sig].append((dig, size, os.path.basename(o)))
    occ = sum(len(v) for v in groups.values())
    dup_bytes = sum(sum(s for _, s, _ in v[1:]) for v in groups.values())
    all_bytes = sum(sum(s for _, s, _ in v) for v in groups.values())
    differ = {k: v for k, v in groups.items() if len({d for d, _, _ in v}) > 1}
    print(f"  COMDAT groups: {occ} in {nobj} objects, {len(groups)} distinct; group bytes {all_bytes / 1e6:.2f} MB, "
          f"of which repeats {dup_bytes / 1e6:.2f} MB; executable bytes in all objects {tsize / 1e6:.2f} MB")
    print(f"  same name, different bytes or relocations: {len(differ)} groups")
    for k in sorted(differ, key=lambda k: -len(differ[k]))[:8]:
        v = differ[k]
        print(f"    {k[:100]}  {len({d for d, _, _ in v})} variants in {len(v)} objects, e.g. {v[0][2]}, "
              + next(o for d, _, o in v if d != v[0][0]))


def base(target, jobs):
    if target != "json":
        sys.exit("base: json only")
    j = os.path.join(CXX, "json")
    fl = tus(target)[0][2]
    w, c = build(target, jobs, kind="plain", trace=False)
    # PCH of the two shared headers
    d = outdir(target, "pch")
    hdr = os.path.join(d, "pch.hpp")
    with open(hdr, "w") as f:
        f.write("#include <nlohmann/json.hpp>\n#include \"doctest_compatibility.h\"\n")
    t0 = time.time()
    subprocess.run([CLANG, *BASE, *fl, "-I", j + "/tests/src", "-x", "c++-header", hdr, "-o", hdr + ".pch"], check=True)
    pw = time.time() - t0
    w2, c2 = build(target, jobs, extra=["-include-pch", hdr + ".pch", "-I", j + "/tests/src"], kind="pch", trace=False)
    print(f"  pch: {pw:.1f} s to build the PCH, then wall {w2:.1f} s, cpu {c2:.1f} s")
    # unity: all TUs in one, in jobs chunks
    d = outdir(target, "unity")
    srcs = [s for _, s, _ in tus(target)]
    chunks = [srcs[i::jobs] for i in range(jobs)]
    cmds = []
    for i, ch in enumerate(chunks):
        u = os.path.join(d, f"unity{i}.cpp")
        with open(u, "w") as f:
            f.write("".join(f'#include "{s}"\n' for s in ch))
        cmds.append([CLANG, *BASE, *fl, "-I", j + "/tests/src", "-c", u, "-o", u[:-4] + ".o"])
    ru0 = resource.getrusage(resource.RUSAGE_CHILDREN)
    t0 = time.time()
    with ThreadPoolExecutor(jobs) as ex:
        res = list(ex.map(run_cc, cmds))
    ru1 = resource.getrusage(resource.RUSAGE_CHILDREN)
    bad = [r[2].splitlines()[:2] for r in res if r[0]]
    print(f"  unity x{jobs}: wall {time.time() - t0:.1f} s, cpu {ru1.ru_utime - ru0.ru_utime + ru1.ru_stime - ru0.ru_stime:.1f} s"
          + (f", {len(bad)} chunks failed: {bad[0]}" if bad else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["build", "summarize", "report", "base"])
    ap.add_argument("target")
    ap.add_argument("-j", type=int, default=6)
    a = ap.parse_args()
    if a.cmd == "build":
        build(a.target, a.j)
    elif a.cmd == "summarize":
        summarize_all(a.target, a.j)
    elif a.cmd == "report":
        report(a.target)
    else:
        base(a.target, a.j)


if __name__ == "__main__":
    main()

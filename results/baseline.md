# Baslinje – första mätningen

**Datum:** 2026-11-07
**Maskin:** Apple M1 Pro, 10 kärnor, 32 GB RAM, macOS 27.2
**Verktyg:** Homebrew clang/lld 23.1.4 (`ARCH=arm64 LLVM=1`), GNU make 4.4.1
**Kärna:** v7.4-rc1 (`6628e7d4c`), `defconfig` + `env/fas1.config`
(ingen debuginfo, ingen LTO/CFI/KASAN, `MODULES=n` → alla `=m` byggs in)
**Kommando:** `kmake O=../build-def -j10 Image`

## Rent bygge (en körning, kall)

| Mått | Värde |
|------|-------|
| Väggtid | **575,6 s** (9 min 36 s) |
| User | 3 746 s |
| Sys | 754 s |
| Snittlast | 7,8 av 10 kärnor |
| C-filer (`CC`) | 9 155 |
| Assemblerfiler (`AS`) | 77 |
| CPU-tid per C-fil (snitt) | ≈ 0,49 s (user+sys / antal CC) |
| `vmlinux` | 112 MB |
| `Image` | 98 MB |

## Observationer

- **Sys-tiden är hög, 17 % av CPU-tiden.** Processstart, `exec` och
  filsystemsanrop på macOS är dyra. Det talar för klient/server-modellen.
- Snittlasten 7,8 av 10 visar att det finns seriella delar (start,
  host-verktyg, länkning, kallsyms) som inte fyller alla kärnor.
- Siffran gäller ett enda kallt bygge och inkluderar att host-verktygen byggs.
  Steg 1.2 kräver bästa av 3–5 körningar, plus no-op- och inkrementella bygg.

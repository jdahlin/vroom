# Mål: kallt kärnbygge < 30 s, -O0, som bootar Debian till userspace – uppnått

**Datum:** 2026-11-07 · **Maskin:** M1 Pro (8P+2E), 32 GB, macOS 27.2
**Kärna:** Linux v7.4-rc1 + `patches/0001-arm64-build-with-O0.patch`, arm64, **-O0**
**Kommando:** `./vroom-build.sh` (stoppar/startar vroomd, `rm -rf` av byggkatalogen, bygger `Image`)
**Boot:** `python3 bench/boot-test.py --kernel work/build-cold/arch/arm64/boot/Image`

| Körning | Tid inkl. serverstart | Userspace (QEMU hvf, Debian 13.7 nocloud) |
|---------|----------------------|-------------------------------------------|
| 1 | **23,8 s** | `is-system-running` = running, 0 failade units |
| 2 | **23,5 s** | running, 0 failade units |

- Inget byggcache: byggkatalogen tas bort, servern startas om och har inget tillstånd.
  Indata är källträdet och `env/vroom.config` (537 inbyggda options, standard-irqchips).
- Alla 838 kärn-C-filer byggs med `-O0` (`.vroom/opt.log`). 833 använder den delade
  PCH:n, och 5 byggs utan PCH efter ett misslyckat försök med den.
- PCH:n används med full validering: inga `-fno-validate-pch`, `-Wno-*` eller
  `KBUILD_MODNAME`-trick.
- Byggets enda varningar är 18 `-Wframe-larger-than`, eftersom -O0 ger stora stackramar.
- Userspace verifieras utan inloggning: `systemd.run=` startar en transient tjänst efter
  `multi-user.target`. Den skriver `uname -r`, `/etc/debian_version`,
  `systemctl is-system-running` och failade units till seriekonsolen.

## Hur -O0 blev möjligt

Upstream-kärnan bygger inte med -O0 på arm64. Ett `make -k`-bygge gav 454 av 828 filer
med fel, alla från `"i"`-operander i inline asm som får sina konstanter genom
`__always_inline`-funktioner. Konstantpropagering kräver mem2reg, som -O0 inte gör.

**Kärnpatch** (`patches/0001-arm64-build-with-O0.patch`, 10 filer, +119/−97). Ingen
ändring av semantik; samma kod blir konstant redan i frontend:

| Ändring | Varför |
|---------|--------|
| `alternative_has_cap_{likely,unlikely}`, `arch_static_branch{,_jump}`, `static_key_{true,false}`, `cpus_have_final{,_boot}_cap` → statement-expression-makron | `"i"`-operanden blir en konstant redan i frontend |
| `sched_feat(x)` väljer gren via en enum-konstant i stället för en `__always_inline`-funktion per feature | samma sak för `sched_feat_keys[]` |
| `BUILD_BUG()` → `__builtin_trap()` när `!__OPTIMIZE__` | noreturn, så `-Werror=return-type` går igenom. Grenen är död (en -O2-build bryts annars). `compiletime_assert` i övrigt är oförändrad (upstream: no-op utan `__OPTIMIZE__`) |
| `kvm_host.h`: `default: BUILD_BUG_ON(1)` → `BUILD_BUG()` | samma |
| `memblock_alloc_{raw,low,node}`, `set_smp_ipi_range`, `do_trace_initcall_*` → `__always_inline` | wrappers runt `__init`-funktioner. Utan inlining hamnar de i `.text` (modpost: section mismatch) |

**`ccs/vroom-ld`** – litet länkomslag för kärnan:
- Vid -O0 finns grenar kvar som -O2 bevisar döda med aktuell config. Exempel är
  SME/GCS-signalkod, IOMMU-DMA, THP och link-time-asserts som `____wrong_branch_error`.
  De refererar 92 symboler som inte finns.
- Om lld *bara* rapporterar odefinierade symboler genereras ett stubobjekt med 64 nollbyte
  per symbol i `.data`, och länkningen körs om.
- `.data` är skrivbar, så statiska nycklar fungerar, och NX, så ett anrop ger ett
  undantag i stil med `BUG()`.
- Listan sparas i `vmlinux.unstripped.vroom-stubs`.

**`vroomd`:** PCH:n byggs med samma `-O` som filerna. Tidigare följde `__OPTIMIZE__`
med från -O2 in i alla filer, och det var därför mitt första -O0-försök verkade
hopplöst. `net/tcp.h` togs bort ur prefixet, eftersom `flow_offload.h` använder
`KBUILD_MODNAME` i en inline-funktion.

## Vägen dit

| Steg | Tid | Vad |
|------|-----|-----|
| defconfig, alla `=m` → `=y` | 575,6 s | baslinje, 9 155 C-filer |
| Minimal config för QEMU virt + Debian | 97 s | 1 097 filer (`env/vroom-min.config`) |
| Trimmad config + sparad `.config` | 67 s | ingen KALLSYMS/BPF/io_uring |
| `vroomd` zygote + `vroom-cc` | 53 s | clangs uppstart 44 ms → 6 ms |
| Delad PCH i servern | 40 s | headers parsas en gång per bygge |
| (-O1, irqchip-patch, `-Wno-ignored-attributes`) | 22,8 s | mellanläge, nu borttaget |
| Nätverk + loopback tillbaka | 24,8 s | krävs för systemd-resolved |
| **-O0 + vroom-ld, validerad PCH, standard-irqchips** | **23,5–23,8 s** | slutläge |

Utan PCH tar ett -O0-bygge 50,8 s.

## Kvar att vinna

- Förberedelsen före första kärnfilen tar ~6 s: Kconfig kör ~130 flaggtester seriellt
  (~20 ms var i sh/mkdir/rm), plus asm-offsets m.fl.
- Assemblerjobb (`-cc1as`) körs fortfarande av riktig clang (~45 ms uppstart).
- Kompilator och länkare är clang/lld via servern, inte egna.

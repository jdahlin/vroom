# Snabb kärnkompilering – Fas 1: Mätning och grund

*Arbetsnamn: **ccs** (compile server). Skriven 2026-11-07.*

## Bakgrund

Långsiktigt mål: en kompilatorserver som bygger Linuxkärnan på `-O0`-nivå
10× snabbare än `gcc`/`clang`. `/usr/bin/cc` (eller `make CC=...`) blir en tunn
klient som skickar argv/cwd/fd:er över en Unix-socket till en långlivad server.

Hypotesen bakom hela projektet:

1. Frontend (preprocessor + parser + Sema) står för merparten av tiden per
   översättningsenhet, och nästan allt det arbetet är header-arbete som upprepas.
2. Med en cache nyckad på *(header, lästa makron, uppslagna deklarationer)*
   blir nästan allt header-arbete cacheträffar.
3. En enkelpass-backend (AST → maskinkod via värdestack, à la TCC/TPDE) tar
   resten av vägen.

**Fas 1 bygger ingen kompilator.** Den tar fram siffrorna som avgör om
hypotesen håller, och bygger infrastrukturen (miljö, mätning, klient/server)
som alla senare faser står på.

## Mål för fas 1

| # | Fråga / leverans | Svar i form av |
|---|------------------|----------------|
| M1 | Var går tiden i ett kärnbygge i dag? | Tabell: total, per Kbuild-steg, frontend vs backend |
| M2 | Hur mycket header-arbete upprepas? | Andel tokens/deklarationer som vore cacheträffar |
| M3 | Hur mycket kostar process-/make-overhead? | Mätning med proxyserver vs direkt kompilator |
| M4 | Fungerande tunn klient + server | Kärna byggd via proxy, byte-identisk, bootar i QEMU |
| M5 | Go/no-go för fas 2 | Kort rapport med beslut och val av frontend-bas |

## Avgränsningar

- Arkitektur: **x86-64** endast.
- Kärna: **v7.4-rc1**, låst commit. (Innehåller redan första halvan av
  Stoakes kbuild-speedup-serie; v5 av resten kan appliceras som jämförelse.)
- Konfigurationer:
  - `tinyconfig` – snabb iteration
  - `defconfig` + fragment `env/fas1.config` – huvudmätning
- Fragment `fas1.config`: `DEBUG_INFO=n`, `DEBUG_INFO_BTF=n`, ingen LTO,
  ingen KASAN/KCSAN/KCFI, `MODULES=n` (allt `=y`), `KERNEL_LZ4=y`
  (eller okomprimerat).
- Kompilatorer: aktuell GCC och Clang från distributionen.
- Utanför fas 1: egen preprocessor/parser/backend, länkning i minnet,
  inkrementellt läge, andra arkitekturer.

## Steg

### 1.1 Miljö

**Ändrat 2026-11-07:** vi bygger direkt på macOS (Apple Silicon, M1 Pro),
utan container. Mål-arkitektur i fas 1 blir därför **arm64** (`ARCH=arm64
LLVM=1`, Homebrew-LLVM); x86-64 som korskompilering senare (kräver objtool och
mer host-arbete). Linux-specifika saker (io_uring m.m.) portas till senare.

- `env/setup-macos.sh`: Homebrew-verktyg, skiftlägeskänslig sparsebundle
  `work.sparsebundle` monterad på `work/`, kloning av kärnan, konfigurationer.
- `env/macos-env.sh`: GNU-verktyg först i `PATH` och `kmake`-funktion.
- `env/host-include/`: det macOS saknar för kärnans host-verktyg:
  `elf.h` (musl, MIT), `byteswap.h`/`endian.h` (egna shims) och
  `vroom-prelude.h` (förhindrar Darwins `uuid_t`, som krockar med
  `scripts/mod/file2alias.c`). Ingen ändring i kärnträdet behövs.
- Ingen `gcc` som mål (bara clang/LLVM) i fas 1 på macOS.
- Reproducerbarhet: sätt `KBUILD_BUILD_TIMESTAMP`, `KBUILD_BUILD_USER`,
  `KBUILD_BUILD_HOST`, `SOURCE_DATE_EPOCH` så att två byggen kan jämföras
  byte för byte.
- Logga CPU-modell, antal kärnor, RAM, kompilatorversioner i varje resultat.

**Klart när:** `kmake defconfig && kmake -j$(sysctl -n hw.ncpu) Image` går
igenom på macOS och två byggen ger identisk `vmlinux`.

### 1.2 Baslinje (M1)

`bench/baseline.sh` kör, för gcc och clang, best-of-N (N=3–5) med varm page cache:

- rent bygge, no-op-bygge, inkrementellt bygge (touch av en `.c`-fil och av en
  vanligt inkluderad header, t.ex. `include/linux/sched.h`).

Uppdelning:

- **Per Kbuild-steg:** wrapper runt `CC`, `LD`, `AS` och scriptsteg som loggar
  start/stopp + argv till en fil (samma mekanism som klienten i 1.4 –
  skriv den en gång). Aggregera: kompilering, `.S`, länkning, kallsyms,
  objtool, modpost, komprimering, övrigt (make/sh).
- **Frontend vs backend:** `clang -O2 -ftime-trace` (kärnans riktiga flaggor)
  på alla TU:er, aggregera "Frontend" / "Backend" / "CodeGen" ur JSON.
  Komplettera med `-fsyntax-only` som snabb kontroll av frontend-andelen.
- **-O0-proxy:** samma sak med en variant som tvingar `-O0` på de filer som
  klarar det (bygget behöver inte länka) – ger en bild av backend-kostnaden
  vid -O0 jämfört med -O2.
- **Kritisk väg:** tidslinje från wrapper-loggen (Chrome trace-format) som
  visar seriella faser och kärnutnyttjande.

**Klart när:** `results/baseline.md` med tabeller och en tidslinje.

### 1.3 Analys av header-redundans (M2)

Det viktigaste mätvärdet i fasen. Två nivåer:

**a) Övre gräns (enkel):** `-E` med linemarkers på alla TU:er. Räkna tokens per
källfil (header) och hur många gånger samma header bearbetas totalt.
→ "X % av alla tokens kommer från headers som redan bearbetats i en annan TU."

**b) Exakt cachenyckel (riktig):** Clang-plugin (`analysis/ppdeps/`) med
`PPCallbacks` + Sema-hooks som per header-inkludering registrerar:

- makron som läses (`Ifdef`, `Ifndef`, `Defined`, `MacroExpands`, `#if`-uttryck)
  och deras definitioner (hash av token-sekvens),
- makron som definieras/odefinieras,
- namnuppslag som når deklarationer *utanför* headern (typedef-namn, taggar,
  enum-konstanter) och deras identitet (fil:rad + hash),
- antal tokens och deklarationer headern bidrar med.

Nyckel = hash(headerinnehåll, lästa makron, uppslagna deklarationer).
Aggregat över hela defconfig:

- andel tokens / deklarationer som vore cacheträffar,
- vilka headers som har många varianter (X-makron, `define_trace.h`,
  `-D`-flaggor per katalog) – kandidater för specialhantering,
- per TU: antal `static inline` som definieras vs faktiskt används
  (underlag för lat parsning och memoiserad kodgenerering).

**Klart när:** `results/header-redundancy.md` med siffrorna ovan och topp-50-listor.

### 1.4 Tunn klient + proxyserver (M3, M4)

Katalog `ccs/`.

**Klient (`ccs-cc`)**, C, statiskt länkad (prova `tools/include/nolibc` från kärnträdet):

- `connect()` till `$XDG_RUNTIME_DIR/ccs.sock` (eller `$CCS_SOCKET`).
- Skicka argv, envp (eller vitlistade variabler), och fd:er 0/1/2 + `O_PATH`-fd
  för cwd via `sendmsg` + `SCM_RIGHTS`, över `SOCK_SEQPACKET`.
- Vänta på exitstatus, returnera den. Om signal: återskapa via `kill(getpid())`.
- Fallback: om servern inte svarar, `execv` riktig kompilator (`$CCS_REAL_CC`).

**Server (`ccsd`)**, C eller Rust:

- Verifiera `SO_PEERCRED` (samma uid), socket `0600`.
- Trådpool; per förfrågan: `fchdir`/`openat`-baserad körning av riktig
  kompilator via `posix_spawn` med klientens fd:er, så diagnostik går rakt till
  make-terminalen.
- Logga per förfrågan: argv, kö-tid, körtid, exitstatus (matar 1.2).
- `--verify`-läge (förberedelse för fas 2): kör förfrågan två gånger och jämför
  utfiler.

**Integration:** `make CC=ccs-cc HOSTCC=gcc ...` – ingen ändring av
`/usr/bin/cc` behövs i fas 1. Kbuilds `cc-option`/`cc-version.sh`/`--version`
passerar transparent.

**Verifiering:**

- `vmlinux` byggd via proxy är byte-identisk med direktbygget.
- Bootar i QEMU (`-kernel bzImage -initrd` med busybox-initramfs som skriver en
  markör och stänger av); skript `bench/boot-test.sh`.
- Mät overhead: proxy vs direkt, rent och no-op-bygge. Förväntan: < 2–3 %.
  Bryt ut ren klient-RTT (mikrobenchmark med `--version`-förfrågningar).

**Klart när:** `results/proxy.md`, och boot-testet är grönt.

### 1.5 Rapport och beslut (M5)

`results/fas1-rapport.md`:

- Sammanfattning av 1.2–1.4.
- **Go-kriterier för fas 2** (föreslagna):
  - frontend ≥ 50 % av kompileringstiden per TU (vid -O0), **och**
  - ≥ 80 % av header-tokens är cacheträffar med den exakta nyckeln, **och**
  - proxy-overhead < 5 %.
- Val av frontend-bas för fas 2: **sparse** (kärn-GNU C redan täckt),
  **aro**, eller egen. Kriterier: GNU C-täckning på kärnan, hastighet på
  `-fsyntax-only`-nivå, hur lätt det är att göra deklarationer oföränderliga och
  delade.
- Lista över överraskningar och risker inför fas 2.

## Riktning för fas 2 (diskuterad 2026-11-07)

Utgå från **TinyCC** (https://repo.or.cz/w/tinycc.git) i stället för en ny
frontend: preprocessor, kodgenerering, assembler och ELF-länkare i ett pass,
backends för arm64 och x86-64. Få det att fungera på kärnan först, profilera,
och optimera sedan steg för steg.

Kända hinder att kartlägga i slutet av fas 1:

- Globalt tillstånd (`vtop`, `tok`, `file` …) och symboler som muteras/frigörs
  vid scope-slut – måste göras om för delad header-cache och trådar.
- Saknar det kärnan kräver: `always_inline`-inlining, `__builtin_constant_p`
  efter inlining, eliminering av död kod, delar av GNU C (t.ex. `asm goto`).
- Länkare: tanken är att bygga in en snabb länkare (mold/wild). Varken mold
  eller wild har fullt stöd för kärnans länkskript; lld (går att anropa som
  bibliotek) eller TinyCC:s egen länkare är realistiska alternativ för `vmlinux`.

## Katalogstruktur

```
PLAN.md
env/        Dockerfile, setup.sh, fas1.config
bench/      baseline.sh, boot-test.sh, trace-till-chrome.py
analysis/   ppdeps/ (clang-plugin), aggregeringsskript
ccs/        client/, server/, protocol.h
results/    baseline.md, header-redundancy.md, proxy.md, fas1-rapport.md
```

## Risker

| Risk | Hantering |
|------|-----------|
| Containerprestanda på macOS skiljer från bare-metal Linux | Mät relativt (före/efter på samma maskin); verifiera nyckeltal på en Linux-burk om möjligt |
| `-ftime-trace` ger missvisande fördelning för kärnflaggor | Korsvalidera med `-fsyntax-only` och `perf` på `cc1`/`clang` |
| Clang-pluginens namnuppslags-spårning är ofullständig | Börja med makronivån (räcker för en övre gräns), lägg till deklarationsberoenden iterativt; jämför mot (a) |
| Reproducerbara byggen blir inte identiska | Diffa med `diffoscope`; dokumentera kvarvarande skillnader |
| Kbuild beter sig annorlunda med wrapper-`CC` | Jämför `.config`, `include/generated/` och `cmd`-filer mellan direkt- och proxybygge |

## Ordning

1.1 → 1.2 och 1.4 parallellt (delar wrapper-/loggmekanism) → 1.3 → 1.5.
1.3 är fasens viktigaste resultat; 1.4 är den viktigaste infrastrukturen.

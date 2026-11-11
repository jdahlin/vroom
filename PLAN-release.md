# vcc 1.0: första release

**Mål:** vcc släpps som en snabb C-kompilator som går att använda direkt via
`CC=vcc`, under MIT-licens.  Den riktar sig till dem som vill ha en snabbare
kompilator med moderna felmeddelanden.  Kärnbygget (vroom) är vår
referensanvändare, inte produkten.

**Beslut (2026-11-09, frågerunda med Johan):**
- Produkt: vcc som C-kompilator (vcc/vas/vld + vccd).
- Kvalitetsribba: TODO.md tier 1–3 tomma (tier 4 får finnas kvar som kända begränsningar).
- Språk: C upp till C23, med GNU-tillägg.
- Kod: rimlig -O1 (registerallokering, enkel inlining, konstantpropagering).
- Diagnostik: bättre än clang; clang-paritet är golvet.
- Plattformar: macOS arm64, Linux arm64, korskompilering mac→Linux, Linux x86-64
  (ELF, SysV), macOS x86-64 (Mach-O) och wasm.
- Drop-in: `CC=vcc` räcker. configure, meson och cmake går igenom, och okända
  gcc/clang-flaggor ger varning, inte fel.
- vccd: default och transparent.  Shimmen startar servern själv.
- Fart: publicerad jämförelse mot tcc och clang.
- Distribution: källkod, förbyggda tarballs, Homebrew, Debian/apt, container.
- Licens: MIT.
- Styrning: releasen styrs av kriterier, inte datum.  Agenterna arbetar som
  nu.  Grindarna nedan är det gemensamma målet.

## Grindar

Releasen är klar när alla grindar är gröna på alla plattformar som grinden
gäller.  Varje grind ska ha ett skript med tydligt grönt/rött resultat
(se G0).

| # | Spår | Grind (mätbar) |
|---|------|----------------|
| G0 | Grindrigg | `bench/release-gate.py` kör G1–G9 (eller läser deras senaste resultat) och skriver en statustabell per plattform; CI kör den på varje plattform |
| G1 | Korrekthet | TODO.md tier 1–3 tomma, inga `xfail` kvar som pekar dit. Csmith och YARPGen differentiellt mot clang: N CPU-timmar per mål och optimeringsnivå utan diff |
| G2 | Korpus | Linux-kärnan bootar Debian till `running`; PostgreSQL `meson test`, SQLite (TCL-testerna) och vcc självbyggd (stage 2 = stage 3) gröna; Debian-urvalet bygger och klarar sina egna tester med `CC=vcc` |
| G3 | Drop-in | autoconf, meson och cmake konfigurerar med `CC=vcc` för hela korpusen utan projektpatchar. Okända flaggor ger varning. Flaggtabell mot gcc och clang (`bench/vcc-driver.py` utökad) |
| G4 | Plattformar | G1–G3 körs på varje mål: macOS arm64, Linux arm64, mac→Linux arm64, Linux x86-64, macOS x86-64, wasm. ABI-tester (`bench/vcc-abi.py`) clang/vcc × anropare/anropad på varje mål |
| G5 | Kodkvalitet | `-O1`: körtid inom X % av clang -O1 på en fast benchmarksvit; `-O0` oförändrat snabbt att kompilera |
| G6 | Diagnostik | `bench/vcc-diag.py` utan regressioner mot clang, och en egen svit med fall där vcc är bevisat bättre än clang (spann genom makron, fix-its, förklaringar) |
| G7 | Fart | `bench/` publicerar kompileringstid per TU och för hela projekt: vcc mot tcc och clang -O0 (vcc -O0) och mot clang -O1 (vcc -O1), både kallt i egen process och med vccd. Siffrorna står i README |
| G8 | vccd som default | TODO.md tier 2 tom. Autostart, versionskontroll mellan shim och server, fallback till ren process om vccd saknas eller kraschar. `vccd-test.py` + `vcc-stress.py --vccd` körs i timmar; fungerar i CI och containrar |
| G9 | Paketering | En tagg ger via CI: källtarball, binära tarballs per plattform, Homebrew-formel, .deb, containeravbild. README, LICENSE (MIT) och THIRD-PARTY med ursprung och licens för clang-tabellerna (`caps.c`, `ppdata*.inc`), gcc c-torture och tcc tests2 |

## Ordning

Ordningen följer beroenden, inte datum.  Spår som inte rör samma filer
kan gå parallellt.

1. **G0 grindrigg och CI.** Den gör status synlig för alla agenter.  Först
   samlas de befintliga testskripten, sedan läggs nya grindar till när de
   finns.
2. **G9 juridik.** LICENSE, THIRD-PARTY och kontroll av att genererade
   tabeller och importerade testsviter får distribueras under MIT-projektet.
   Det är billigt nu och dyrt senare.
3. **G1 tier 1 + differentiell fuzzning**, parallellt med **x86-64-backenden**
   (`arch/x86_64/`; katalogstrukturen är förberedd).
4. **G8 tier 2 och vccd som default.**
5. **Tier 3** (front end, C23, driverflaggor för G3).
6. **G5 -O1**, först när tier 1 är tom.  En optimerare ovanpå felaktig kod
   gör buggarna svårare att hitta.
7. **wasm-målet, Debian-urvalet (G2), G6:s egna svit och G7:s mätningar.**
8. **G9 paketering**, och sedan taggen.

## Öppna frågor

- **wasm:** menas en wasm-backend (wasm32 eller wasm64, WASI?) eller vcc
  körd som wasm (`ccs/wasm/`, finns redan)?
- **C23 fullt:** `_BitInt` med godtycklig bredd, `#embed`, `constexpr`: hela
  standarden eller clangs nivå?
- **-O1:** vilken benchmarksvit, och vilket X i G5?
- **Debian-urvalet:** vilka paket och hur många?  Fast lista eller topp N
  efter popcon bland rena C-paket?
- **"Bättre än clang":** vem bedömer det?  Kuraterad svit eller A/B-test
  med användare?
- **vccd som default:** beteende i CI och containrar, och socketens
  säkerhetsmodell på delade maskiner.
- **Namn:** "vcc" krockar med Microsofts VCC och verifieraren VCC.
- **Grindägare:** ska varje grind ha en ansvarig session?
- **Csmith/YARPGen:** hur många CPU-timmar är N?

## Status

### Status 2026-11-11: G5, audit av x86-kodkvalitet

`PLAN-x86.md` X4 har en prioriterad audit mot clang 23.1.6 och GCC 15.2.0
på Ryzen 7 9700X: lokala optimeringar med låg kompilatorkostnad utan
IR/SSA, belagda med 13 assemblerprober. Inget implementerat eller uppmätt
körtidsmål; G5 är fortsatt öppet och benchmarksvitens X är inte fastställt.

### Status 2026-11-11: externa tester på Linux x86-64
ARM64:s stackallokering och slots använder nu 64-bitars offset med kontrollerad
överflödeshantering. GCC `20031023-{1..4}` kompilerar (4/4); `frame_wide`
körs med 64 KiB under QEMU och kompilerar med 1 TiB både direkt till objekt
och via assemblertext. Isolerad egen ARM64-svit: 115/115. Detta flyttar G1/G2
men stänger inte grindarna.

G1 har fått mätbara baslinjer från clangs diagnostiktester, GCC:s
compile-svit och två små körsviter. Fel och krascher återstår; se
`TEST.plan` S2, S3 och S5. G1 och G2 är inte färdiga.

### Status 2026-11-09: plan skriven, inga grindar byggda
Inget av G0–G9 finns som skript än.  Det som finns i dag och blir delar av
grindarna: `vcc-diff.py`, `vcc-sema.py`, `vcc-ctests.py`, `vcc-abi.py`,
`vcc-diag.py`, `vcc-driver.py`, `vccd-test.py`, `vcc-stress.py`,
`bench/pg/`, `vccd-kbuild.sh` + `boot-test.py`.  Det saknas README,
LICENSE, x86-64-backend, wasm-backend, -O1, SQLite-korpus, Debian-urval,
Csmith/YARPGen-rigg och paketering.

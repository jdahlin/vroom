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

### Status 2026-11-11: G1/G4, objektstorlek på Linux

På basen `9d022c3` skiljer objektstorleksanalysen deklarerade delobjekt från
rå allokering och padding. `-fstrict-flex-arrays=0..3` ingår i cacheidentiteten;
läge 3 behåller deklarerad minsta storlek. `counted_by` på stabila pekarfält
använder aktuell räknare i dynamiska lägen 0–2; volatila baser/fält lämnas
okända utan läsning. Inlining bevarar bounds bara för direkta, oskrivna och
icke-aliaserade pekarparametrar med effektfria argument.

Verifiering omfattar unity/separatfilbygge, egna körsviter på Linux x86-64
följt av ARM64/QEMU, fem oförändrade GCC-fall (`pr101836_1/2/3`, `pr39343`,
`pointer-counted-by-8`), oberoende strikt-flex/volatile/allokeringsprober och
kärnans semantik i både process- och batchläge. Exakta resultat och binärhashar:
`work/results/objsize-main-validation.json` och
`work/results/objsize-main-independent/results.json`.
Båda byggena: ARM 185/185, semantik run/batch 887/887, GCC 5/5 per mål;
34 oberoende konfigurationer gav 102 matchande körningar (272 kommandon).
Ursprunglig x86-körning: 179/179 och sex skip, varav `branch_far` var en
Clang-referenstimeout efter 60 s, inte ett godkänt compilerresultat.
Separat supplement använder PR37-harness `761142b` och dess uttryckliga
300 s-kontrakt: 1/1 utan skip på båda mål med båda byggena. Resultat sparas i
`work/results/objsize-main-branchfar/results.json`. Ursprungliga loggar ändras inte.

Kvar: generell flödes-/aliasanalys, transitiva inline-parametrar,
`alloc_size`-kontrakt och dynamiska uttryck med sidoeffekter; okända bounds
ska fortsätta vara okända. Hela S4-korpusen och övriga mål har inte körts om
för denna milstolpe. G1/G4 är inte färdiga.

### Status 2026-11-11: G5/G7, registerlokaler på x86

Gemensamt behörighetsurval i `emit/opt/locals.c` används av alla tre
bakändar; x86 håller nu lönsamma skalärer i fyra callee-save-register.
Cold-prober är oförändrade; ett native loopprov går 7,830→6,345 ms men
divisionsfritt loopkorpus kostar cirka 11 % mer kompilator-CPU. Det är
inte en godkänd 1 %-grind. Snapshot/ABI-säkerhet för builtin setjmp och
sena/inlinade justerade ramar är testad. Båda byggsätten: 116 fokuserade
kombinationer; x86-egensvit 137/137, arm64 142/142, kors-ABI 22/22.
CI kör nu x86-prober under qemu-user. Se OPT3 i `PLAN-fas2.md` för
hashar, mätmetod och återstående arbete. G5/G7/O2-målet fortsatt öppna.

### Status 2026-11-11: G5/G7, direkt parameterbindning

Utvalda x86-parametrar binds direkt till registerhem utan extra ABI-spill
eller efterladdning. Smala typer, stackargument, mixed GP/FP, hidden sret
och varargs verifierade O0–O3; kors-ABI 22/22. Parade korta mätningar och
begränsningar finns i OPT3 i `PLAN-fas2.md`. G5/G7/O2-målet fortsatt öppna.

### Status 2026-11-11: G5/G7, onåbar kod och signed tvåpotenser

Delat linjärt record-pass tar bort säkert onåbara suffix; x86 har nu
register-/effektkontrakt för nästa kopie-/livenesssteg. Signed konstant
tvåpotensdivision/-rest delar ett recept mellan alla bakändar; wasm
behåller kompakt div/rem där clang också gör det. Separate regelfiler
och måladaptrar. Båda byggsätten klarar 96 fokuserade kontroller;
egensviter x86 135/135 och arm64 140/140. Syntetisk kompilator-CPU-kostnad
och kvarvarande osäkerhet redovisas vid OPT8 i `PLAN-fas2.md`.
G5/G7 och hela O2-målet är fortsatt öppna.

### Status 2026-11-11: G5/G7, delad grenstädning

Arm64 och x86 använder nu `emit/rec/fallthrough.c`; arm64:s extraktion
är byte-identisk för 131 befintliga testfiler med O1 och -g. Wasm
utelämnar redundanta while-grenar med separat nåbarhetsbevis. Båda
byggsätten klarar 64 fokuserade kod-/körkombinationer; egensviter x86
133/133 och arm64 138/138. Mätdata och kvarvarande CPU-osäkerhet finns
vid OPT2 i `PLAN-fas2.md`. G5/G7 och O2-målet är fortsatt öppna.

### Status 2026-11-11: G5/G7, delade heltalsidentiteter

`emit/opt/identity.c` delar nu typkorrekta heltalsidentiteter mellan alla
bakändar; tvåpotensregeln ligger separat i `divpow2.c`. Unity och
fil-för-fil klarar 32 kod-/körkontroller vid O0–O3. Egna sviter: x86
131/131, arm64 136/136. Effekter och smala/höga registerbitar täcks av
regressioner. Mätbrus hindrar slutsats om små kompilator-CPU-skillnader;
se OPT1 i `PLAN-fas2.md`. G5/G7 och hela O2-målet är fortsatt öppna.

### Status 2026-11-11: G5/G7, första gemensamma regel implementerad

Unsigned konstant tvåpotensdivision/-rest sänks nu genom samma hjälpare
för x86, arm64 och wasm32/64. Alla 16 mål-/nivåkombinationer klarar
kodkrav och literalbaserad körkontroll, även fil-för-fil. Egna sviter:
x86 130/130, arm64 135/135. Wasm-testvägen använder vcc:s egna objekt
och länkare plus systemets Node 24/25, utan Emscripten; full
runtimeersättning är W9 i `PLAN-wasm.md`. Korta före/efter-mätningar
och begränsningarna för hårdvaruräknare är dokumenterade vid OPT1
i `PLAN-fas2.md`. Detta är delprogress; G5/G7 är fortsatt öppna.

### Status 2026-11-11: G5/G7, gemensam implementationsplan

`PLAN-fas2.md` **Gemensam optimerare utan SSA** äger nu OPT0–OPT8 efter
kodgranskning av alla tre generatorer. C-regler/strategier delas av x86,
arm64 och wasm32/wasm64; arm64:s befintliga postoptimerare extraheras för
registermålen. X4 och wasm-planen äger inkoppling och målsänkning.
Planen anger beroenden, effekt-/ABI-/minneskontrakt, begränsad CPU-kostnad,
tester och separata mätningar av kompilator och genererad kod. Nästa
leverans är en ny bas och en delad heltalsregel med tre konsumenter.
Ingen optimering implementerad; G5/G7 är fortsatt öppna och G5:s X saknas.

### Status 2026-11-11: G5, audit av x86-kodkvalitet

`PLAN-x86.md` X4 har en prioriterad audit mot clang 23.1.6 och GCC 15.2.0
på Ryzen 7 9700X: lokala optimeringar med låg kompilatorkostnad utan
IR/SSA. Utökad med 29 reproducerbara prober vid O0–O3, baseline och
native: 24/24 körningar ger samma checksumma samt klarar gränsvärden och
kodkontroller för befintliga egenskaper. Flaggor, passloggar och kod sparas
av `bench/vcc-optaudit.py`; vcc O1/O2/O3 ger samma assembler här.
Inga optimeringar implementerade eller uppmätt
körtidsmål; G5 är fortsatt öppet och benchmarksvitens X är inte fastställt.

### Status 2026-11-11: objektrepresentationer och packade boolvektorer
`__builtin_bit_cast` har körning och bytebaserad konstanttolkning med okänd
padding; packade boolvektorer har logiska element, bitlagring och verifierad
Linux/Wasm-ABI. Isolerad unity/filvis validering är 164/164 egna x86-prov,
169/169 ARM-prov och 32/32 ABI-prov per mål. TSan och skyddssidesprov är
gröna; se `TEST.plan` S2 för gränser och validering. Detta flyttar G1/G2 men
stänger dem inte; separat `nullptr_t`-identitet återstår.

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

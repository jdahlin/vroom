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

### Status 2026-11-11: G5/G7, wasm frame-cell forwarding

Wasm32/64 använder nu den gemensamma cell-/värdekärnan för exakta
4/8-byte frame-celler och tar bort överskrivna lagringar. Typade
emissionshändelser bevarar adress-/RHS-effekter och operandstack; inga
SSA-grafer eller assemblertextmönster. Positivt aggregatprov går från sex
till två minnesoperationer. Blandat elvafilersprov ger +0,599 % värd-Ir,
kontroller +0,18–0,30 %, positivt prov +7,03 %. Detta är avgränsad data,
inte hela G7. 16 fokuserade kör-/kodkrav, sanitiserare, oberoende review,
6120 identiska native-resultat och 149 egna wasm32-PASS verifierar steget.
Lokal wasm64-referenskrasch är baslinjereproducerad och synlig; se
PLAN-wasm.md. X86:s adapter är fortsatt experimentell på grund av kostnad;
smala wasm-celler, generell unread-DSE, full O2 och G5/G7 återstår.

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
för denna milstolpe. Riktade Wasm32/64-prover går 10/10 med ett kört XFAIL
för `object_size_inline`: samma test ger byteidentisk assemblerkod och
samma runtimefel med tidigare `main`; Clang-referensen passerar. Testet
behöver Wasm-inlining/argumentbounds och är inte överhoppat. G1/G4 är inte färdiga.

### Status 2026-11-11: G5/G7, kopior och direkt utvidgningsbevis

Delad native kopiekärna återanvänder arm64:s analys; x86 gör endast
kandidatsökning i sin befintliga tvåadressvandring, utan egna allokeringar
eller livenesspass. Separat extensions.c utelämnar bevisat redundant
unsigned32-normalisering från föregående skrivning. 148 fokuserade
körkombinationer gröna efter integration med main9d022c3, inklusive
volatile/high-bit/signed-regressioner; båda byggsätt och sanitiserade
kontrakt verifierade. 500-funktions v3-instruktionsprober mot14379fe ger
+0,23% loop och −0,74% kontroll för kombinationen. Profilens räckvidd
är begränsad; vbench/native helkorpus och hela G5/G7/O2-målet kvarstår.

### Status 2026-11-11: G1, padding i BitInt-uttryck

Sammansatta tilldelningar normaliserar nu gamla `_BitInt`-värdet innan
skift, division och rest på både Linux x86-64 och arm64. Det oförändrade
Kefir-fallet `bitint_assign_shift1` passerar alla fyra Clang/vcc-parningar
på båda målen. Regressionen provar 9–128 bitar, smutsig padding, tecken och
engångsevaluering av operander. Unity och separata filer samt kärnans
sema 887/887 individuellt och i batch är verifierade för den isolerade
fixen. Den kombinerade milstolpen `04209a5` passerar 18 oförändrade Kefir-par
med fyra kompilatorkombinationer per Linux-mål (144 körningar), båda
byggsätten, diagnostik 220/220, ABI 32/32 per mål och ASan 19/19 per mål.
Egna ARM-prov: 189/189; x86: 183/183 plus ett separat godkänt `branch_far`
med PR37:s granskade 300 s-kontrakt (ursprunglig 60 s-timeout bevaras i
loggen). Fem övriga x86-referensundantag återstår. Wasm64 går 148/148 med
31 körda XFAIL och tio referensundantag; fokuserad Wasm32 5/5 med sju XFAIL.
Padding-testets Wasm-lucka är bevisad med båda tidigare baslinjerna.
Manifest och slutgrind: `work/validate-kefir-7eca264/validation.json` och
`final-manifest.json`. G1 är fortsatt öppen.

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

### Status 2026-11-11: G5/G7, delad stackcellanalys

Nästa del delar nu värde-/storefakta i `emit/rec/slot-values.c`. 6108
byte-identiska arm64-resultat och separata sanitiserade kontrakt verifierar
flytten. X86-/wasm-anslutning och kombinerad kostnadsgrind pågår; G5/G7
är fortsatt öppna.

Arm64 använder nu en gemensam analys av exakta allokeringsceller,
överlapp och adressflykt i `emit/rec/slots.c`. 6069 byte-identiska
objekt-/textjämförelser, 177 egna arm64-körtester och ett sanitiserat
intervallkontrakt verifierar flytten. Två begränsade instruktionsprofiler
visar +0,655/+0,303 % kompilatorarbete; metod och baser i OPT5 i
`PLAN-fas2.md`. X86-/wasm-anslutning och delade omskrivningar återstår;
G5/G7 och full O2-täckning är inte klara.

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

### Status 2026-11-11: atomiska helobjekt på Linux
Större atomiska record, heltal och komplexa värden använder nu libatomic-ABI
när den inbyggda 1/2/4/8-bytevägen inte räcker. Jämför-och-byt-loopar behåller
hela objektet och rätt förväntat värde även vid padding och konkurrens.
Pthread-prov samt atomikregressioner klarar Linux x86 och ARM i unity,
filvis bygge och ASan/UBSan; kernel-sema är 887/887 i båda körsätten.
Den nya snabba värdriggen körs i normal och ASan-CI. Atomiska vektortyper
återstår i frontend; G1/G2 är fortsatt öppna.

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

### Status 2026-11-11: G5/G7, balanserad switchdispatch

OPT6:s första del använder gemensamt strategival/sortering och balanserade
jämförelseträd på x86, arm64 och wasm; täta wasm-tabeller behålls. Kombinationen
med b63c269 bygger unity/fil-för-fil och passerar 164 riktade kör-/kodkrav,
219 arm64- och 214 x86-regressioner. På fded15a-basen är 500-funktionskontroller
byte-identiska och extra Callgrind-kompileringsarbete högst +0,180 %; ett
riktat x86-switchprov minskar exekverade instruktioner 48,39 %. Avgränsning,
rådata och kvarvarande OPT6-delar står i PLAN-fas2. G5/G7 och full O2 är öppna.

### Status 2026-11-11: G5/G7, unsigned32 reciprokdivision

OPT8:s u32-del använder gemensamma reciproker för native kvot/rest och
begränsade wasm-kvoter. 180 riktade kör-/kodkrav, sanitiserade matematik- och
tryckkontrakt, unity/fil-för-fil samt 220 arm64-/215 x86-regressioner passerar.
Mot exakt 781ca69 ligger extra arbete i kontroller under +0,442 % och en
konfigurerad 10–12-filers mix mellan −0,017 % och +0,018 %. En mättad
arm64-divisionskorpus kostar däremot +19,54 % att kompilera; avvägningen och
avgränsade x86/wasm-körprov står i PLAN-fas2. Detta stänger inte G5/G7.
Signed/u64-reciproker och full O2-täckning återstår.

Efter integrering med main54580f7: samma kompilatorkod, 180/180 riktade fall,
221/221 arm64-regressioner (ett befintligt xfail), 216/216 x86-regressioner
(sex referensskips) samt sanitiserade reciprok-/tryckkontrakt passerar.

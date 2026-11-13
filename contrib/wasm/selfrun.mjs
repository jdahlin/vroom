// bench/vcc-self.py's runner: one vcc.wasm (DIR/vcc.js), many compiles.
//   node selfrun.mjs DIR LIST   LIST: lines "TARGET\tINPUT.i"; prints one JSON
//   line per input: {i, rc, out} with out the -S text (or the diagnostics).
// The module is compiled once and instantiated afresh for every input
// (EXIT_RUNTIME: main runs once per instance).
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
const [dir, list] = process.argv.slice(2);
const { default: VccModule } = await import(pathToFileURL(path.resolve(dir, 'vcc.js')).href);
const wasm = await WebAssembly.compile(fs.readFileSync(path.resolve(dir, 'vcc.wasm')));
for (const line of fs.readFileSync(list, 'utf8').split('\n')) {
	if (!line) continue;
	const [target, input] = line.split('\t');
	let err = '';
	const m = await VccModule({
		print: () => {}, printErr: s => { err += s + '\n'; },
		instantiateWasm: (imports, ok) => { WebAssembly.instantiate(wasm, imports).then(i => ok(i)); return {}; },
	});
	m.FS.writeFile('/t.c', fs.readFileSync(input));
	let rc;
	try {
		rc = m.callMain([`--target=${target}`, '-O2', '-w', '-S', '/t.c', '-o', '/t.s']);
	} catch (e) {
		rc = 'trap: ' + e.message;
	}
	let out = '';
	try { out = m.FS.readFile('/t.s', { encoding: 'utf8' }); } catch { }
	console.log(JSON.stringify({ i: input, target, rc, out: rc === 0 ? out : err }));
}

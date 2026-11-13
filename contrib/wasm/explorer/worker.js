/*
 * The compiler in a worker: every request runs vcc in a new instance of the
 * module (vcc keeps process-wide caches and never frees its reservations),
 * from a WebAssembly.Module compiled once.
 */
importScripts('vcc.js');

let modP = null;

function wasmModule() {
	if (!modP)
		modP = WebAssembly.compileStreaming
			? WebAssembly.compileStreaming(fetch('vcc.wasm')).catch(() =>
				fetch('vcc.wasm').then(r => r.arrayBuffer()).then(b => WebAssembly.compile(b)))
			: fetch('vcc.wasm').then(r => r.arrayBuffer()).then(b => WebAssembly.compile(b));
	return modP;
}

async function run(req) {
	const mod = await wasmModule();
	const out = [], err = [];
	const t0 = performance.now();
	const inst = await VccModule({
		print: s => out.push(s),
		printErr: s => err.push(s),
		instantiateWasm(imports, done) {
			WebAssembly.instantiate(mod, imports).then(i => done(i, mod));
			return {};
		},
	});
	const t1 = performance.now();
	inst.FS.mkdir('/src');
	inst.FS.writeFile('/src/' + req.name, req.source);
	let rc;
	try {
		rc = inst.callMain(req.args.concat(['/src/' + req.name, '-o', '/out']));
	} catch (e) {
		if (e && e.name === 'ExitStatus') rc = e.status;
		else { err.push('vcc crashed: ' + (e && e.message || e)); rc = -1; }
	}
	let text = '';
	try { text = inst.FS.readFile('/out', { encoding: 'utf8' }); } catch (e) { /* no output */ }
	const t2 = performance.now();
	return { id: req.id, rc, text, stdout: out.join('\n'), stderr: err.join('\n'),
		msInit: t1 - t0, msRun: t2 - t1 };
}

onmessage = async ev => {
	try {
		postMessage(await run(ev.data));
	} catch (e) {
		postMessage({ id: ev.data.id, rc: -1, text: '', stdout: '', stderr: 'worker: ' + (e && e.message || e), msInit: 0, msRun: 0 });
	}
};
wasmModule().then(() => postMessage({ ready: true }), e => postMessage({ ready: false, error: String(e) }));

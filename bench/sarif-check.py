#!/usr/bin/env python3
"""
Check a SARIF 2.1.0 log as vcc writes it (PLAN-diag.md D3.2).

Always: the structure SARIF consumers rely on (version, one run, the tool,
results with a message and rule index into the rules, 1-based regions that
do not end before they start, related location ids unique and every
relationship's target present, fix-it replacements with a region and
inserted text).  With the official schema vendored as
bench/sarif-schema-2.1.0.json (`bench/sarif-check.py fetch`), also a JSON
Schema check (a subset validator: $ref, type, properties, required,
additionalProperties, items, enum, const, minimum, minItems, uniqueItems,
anyOf/oneOf/allOf; formats and patterns are not checked).

  bench/sarif-check.py FILE...      exit 1 on the first problem in each
  bench/sarif-check.py fetch        download the schema (once)
  import: check(doc) -> [problem, ...]
"""
import json, os, subprocess, sys

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCHEMA = f'{R}/bench/sarif-schema-2.1.0.json'
URL = 'https://raw.githubusercontent.com/oasis-tcs/sarif-spec/main/sarif-2.1/schema/sarif-schema-2.1.0.json'


def structure(doc):
    p = []
    if doc.get('version') != '2.1.0':
        p.append('version is not 2.1.0')
    runs = doc.get('runs')
    if not isinstance(runs, list) or len(runs) != 1:
        return p + ['not exactly one run']
    run = runs[0]
    drv = run.get('tool', {}).get('driver', {})
    if not drv.get('name'):
        p.append('tool.driver.name missing')
    rules = drv.get('rules', [])
    ids = [r.get('id') for r in rules]
    if len(set(ids)) != len(ids):
        p.append('duplicate rule ids')

    def region(where, r):
        for k in ('startLine', 'startColumn', 'endLine', 'endColumn'):
            if not isinstance(r.get(k), int) or r[k] < 1:
                p.append(f'{where}: region.{k} missing or < 1')
                return
        if (r['endLine'], r['endColumn']) < (r['startLine'], r['startColumn']):
            p.append(f'{where}: region ends before it starts')

    def phys(where, l):
        pl = l.get('physicalLocation')
        if pl is None:
            return
        if not pl.get('artifactLocation', {}).get('uri'):
            p.append(f'{where}: artifactLocation.uri missing')
        if 'region' in pl:
            region(where, pl['region'])

    for i, res in enumerate(run.get('results', [])):
        w = f'results[{i}]'
        if not isinstance(res.get('message', {}).get('text'), str):
            p.append(f'{w}: message.text missing')
        if res.get('level') not in ('error', 'warning', 'note', 'none'):
            p.append(f'{w}: bad level {res.get("level")!r}')
        ri = res.get('ruleIndex')
        if ri is not None and (ri >= len(rules) or rules[ri].get('id') != res.get('ruleId')):
            p.append(f'{w}: ruleIndex {ri} does not name rule {res.get("ruleId")!r}')
        lids = set()
        for k in ('locations', 'relatedLocations'):
            for j, l in enumerate(res.get(k, [])):
                phys(f'{w}.{k}[{j}]', l)
                if 'id' in l:
                    if l['id'] in lids:
                        p.append(f'{w}: location id {l["id"]} twice')
                    lids.add(l['id'])
        for k in ('locations', 'relatedLocations'):
            for j, l in enumerate(res.get(k, [])):
                for rel in l.get('relationships', []):
                    if rel.get('target') not in lids:
                        p.append(f'{w}.{k}[{j}]: relationship target {rel.get("target")} is no location')
        for j, f in enumerate(res.get('fixes', [])):
            for c in f.get('artifactChanges', []):
                if not c.get('artifactLocation', {}).get('uri'):
                    p.append(f'{w}.fixes[{j}]: artifactLocation.uri missing')
                for rp in c.get('replacements', []):
                    region(f'{w}.fixes[{j}]', rp.get('deletedRegion', {}))
                    if 'insertedContent' in rp and not isinstance(rp['insertedContent'].get('text'), str):
                        p.append(f'{w}.fixes[{j}]: insertedContent.text missing')
    return p


def validate(inst, sch, root, path='$'):
    """JSON Schema (draft-07 subset) problems of inst"""
    out = []
    if '$ref' in sch:
        ref = sch['$ref']
        if ref.startswith('#/'):
            node = root
            for part in ref[2:].split('/'):
                node = node[part]
            return validate(inst, node, root, path)
        return out
    t = sch.get('type')
    tm = {'object': dict, 'array': list, 'string': str, 'boolean': bool, 'null': type(None)}
    if t:
        ts = t if isinstance(t, list) else [t]
        ok = False
        for x in ts:
            if x == 'integer':
                ok |= isinstance(inst, int) and not isinstance(inst, bool)
            elif x == 'number':
                ok |= isinstance(inst, (int, float)) and not isinstance(inst, bool)
            else:
                ok |= isinstance(inst, tm[x])
        if not ok:
            return [f'{path}: not {t}']
    if 'enum' in sch and inst not in sch['enum']:
        out.append(f'{path}: {inst!r} not in {sch["enum"]}')
    if 'const' in sch and inst != sch['const']:
        out.append(f'{path}: not {sch["const"]!r}')
    if 'minimum' in sch and isinstance(inst, (int, float)) and inst < sch['minimum']:
        out.append(f'{path}: {inst} < {sch["minimum"]}')
    if isinstance(inst, dict):
        props = sch.get('properties', {})
        for k in sch.get('required', []):
            if k not in inst:
                out.append(f'{path}: required {k!r} missing')
        for k, v in inst.items():
            if k in props:
                out += validate(v, props[k], root, f'{path}.{k}')
            elif sch.get('additionalProperties') is False:
                out.append(f'{path}: unexpected property {k!r}')
            elif isinstance(sch.get('additionalProperties'), dict):
                out += validate(v, sch['additionalProperties'], root, f'{path}.{k}')
    if isinstance(inst, list):
        if 'minItems' in sch and len(inst) < sch['minItems']:
            out.append(f'{path}: fewer than {sch["minItems"]} items')
        if sch.get('uniqueItems') and len(set(json.dumps(x, sort_keys=True) for x in inst)) != len(inst):
            out.append(f'{path}: items not unique')
        if isinstance(sch.get('items'), dict):
            for i, x in enumerate(inst):
                out += validate(x, sch['items'], root, f'{path}[{i}]')
    for k in ('allOf',):
        for s in sch.get(k, []):
            out += validate(inst, s, root, path)
    for k in ('anyOf', 'oneOf'):
        if k in sch:
            n = sum(1 for s in sch[k] if not validate(inst, s, root, path))
            if n == 0 or (k == 'oneOf' and n > 1):
                out.append(f'{path}: {k} matches {n}')
    return out


def check(doc):
    p = structure(doc)
    if os.path.exists(SCHEMA):
        sch = json.load(open(SCHEMA))
        p += validate(doc, sch, sch)
    return p


def main():
    a = sys.argv[1:]
    if a == ['fetch']:
        subprocess.run(['curl', '-fsSL', '-o', SCHEMA, URL], check=True)
        print(f'{SCHEMA}')
        return 0
    bad = 0
    for f in a:
        p = check(json.load(open(f)))
        if p:
            bad += 1
            print(f'{f}: {p[0]}' + (f' (+{len(p) - 1} more)' if len(p) > 1 else ''))
    print(f'{len(a) - bad}/{len(a)} ok' + ('' if os.path.exists(SCHEMA) else ' (structure only: no vendored schema)'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())

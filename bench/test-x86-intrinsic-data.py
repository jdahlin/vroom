#!/usr/bin/env python3
"""Offline invariants for generated X9 metadata and its nontrivial type grammar."""
import importlib.util
import json
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('x9gen',Path(__file__).with_name('gen-x86-intrinsics.py'))
gen=importlib.util.module_from_spec(spec);spec.loader.exec_module(gen)

class DataTests(unittest.TestCase):
    def test_pointer_to_vector(self):
        self.assertEqual(gen.encode_type('_Vector<64, char const *>'),('PKV64c',0))
        self.assertEqual(gen.signature('_Vector<64, char>(_Vector<64, char const *>, _Vector<64, char>, unsigned long long int)'),('V64c PKV64c V64c Q',64))
    def test_multiple_constants(self):
        self.assertEqual(gen.signature('int(_Constant int, unsigned _Constant char, _Constant int)'),('i !i !C !i',0))
    def test_char_identity(self):
        self.assertEqual([gen.encode_type(t)[0] for t in ['char','signed char','unsigned char']],['c','a','C'])
    def test_large_width(self):
        self.assertEqual(gen.encode_type('_Vector<256, int>'),('V256i',1024))
    def test_inventory(self):
        data=json.loads((gen.DATA/'builtins.json').read_text());rows=data['builtins']
        self.assertEqual(len(rows),2029)
        self.assertEqual(sum(bool(r['headers']) for r in rows),2002)
        self.assertEqual([r['name'] for r in rows],sorted(set(r['name'] for r in rows)))
        for row in rows:
            sig,width=gen.signature(row['prototype'])
            self.assertEqual(sig.count('!'),row['prototype'].count('_Constant'))
            argc=len(sig.split())-1
            for c in row['ranges']:
                self.assertTrue(0<=c['argument']<argc)
                self.assertLessEqual(c['min'],c['max'])
    def test_supported_vs_recognized(self):
        rows={r['name']:r for r in json.loads((gen.DATA/'builtins.json').read_text())['builtins']}
        mappings=json.loads((gen.DATA/'lowerings.json').read_text())
        self.assertEqual(len(mappings),119)
        for name,row in mappings.items():
            self.assertFalse(rows[name]['special_validation'])
            self.assertTrue(row['evidence'])
            self.assertIn(row['class'],['XI_DIRECT','XI_TIED','XI_SHIFT','XI_XGETBV','XI_LANE','XI_LOAD','XI_MASK','XI_MASKZ','XI_EXTRACT'])
        self.assertEqual(mappings['__builtin_ia32_xgetbv']['class'],'XI_XGETBV')
        self.assertEqual(mappings['__builtin_ia32_vec_ext_v2di']['class'],'XI_LANE')
    def test_integer_overloads(self):
        rows=json.loads((gen.DATA/'elementwise.json').read_text())
        self.assertEqual(len(rows),162)
        self.assertEqual(len({(r['op'],r['sig']) for r in rows}),len(rows))
        self.assertEqual(sum(r['class']=='XI_REDUCE' for r in rows),33)
        for row in rows:
            self.assertNotIn(row['sig'].split()[0][-1],['f','d','h','j'])
            self.assertIn(row['width'],[16,32,64])
            if row['op']=='POPCOUNT':self.assertIn('avx512',row['features'])
    def test_fresh_builtin_table(self):
        self.assertEqual(gen.OUT.read_text(),gen.generate())

if __name__=='__main__':unittest.main()

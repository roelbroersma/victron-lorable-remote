from pathlib import Path
import argparse
import importlib.util
import json
import struct
import zlib
import unittest

ROOT=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('bundle',ROOT/'tools/update-bundle.py')
bundle=importlib.util.module_from_spec(spec);spec.loader.exec_module(bundle)
IMAGE=ROOT/json.loads((ROOT/'firmware/manifest.json').read_text())['firmware']['path']

class Bundles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data=IMAGE.read_bytes()
        cls.version,cls.stm,cls.esp=bundle.unpack(cls.data)
    def test_roundtrip(self):
        self.assertEqual(bundle.pack(self.stm,self.esp,self.version),self.data)
    def test_corruption(self):
        for off in [0,8,12,20,48,52,56,60,64,96,128,132,251,252,256,1024,len(self.data)-1]:
            data=bytearray(self.data);data[off]^=1
            with self.assertRaises(ValueError,msg=str(off)):bundle.unpack(data)
    def test_truncation_extra(self):
        for data in [self.data[:255],self.data[:-1],self.data+b'\0']:
            with self.assertRaises(ValueError):bundle.unpack(data)
    def test_merged_and_invalid_vectors(self):
        for vectors in [(0xffffffff,0xffffffff),(0x2000fc00,0x08000001),(0x2000fc00,0x08006100)]:
            stm=bytearray(self.stm);struct.pack_into('<II',stm,0,*vectors)
            with self.assertRaises(ValueError):bundle.pack(stm,self.esp,self.version)
    def test_wrong_component_version(self):
        with self.assertRaises(ValueError):bundle.pack(self.stm,self.esp,self.version+'-wrong')

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--image',type=Path,default=IMAGE)
    args,remaining=parser.parse_known_args()
    IMAGE=args.image
    unittest.main(argv=[__file__]+remaining)

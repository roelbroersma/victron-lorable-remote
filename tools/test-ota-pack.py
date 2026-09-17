import argparse, hashlib, importlib.util
from pathlib import Path
spec=importlib.util.spec_from_file_location('packer',Path(__file__).with_name('pack-esp-ota.py'))
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser();parser.add_argument('--image',type=Path);args=parser.parse_args()
stock=args.image or next((root/'research/stock-3.3').rglob('esp-at.bin.xz.packed'))
raw=p.unpack(stock.read_bytes())
if not args.image:
    original=next(f for f in (root/'research/stock-3.3').rglob('esp-at.bin') if f.read_bytes()==raw)
assert p.unpack(p.pack(raw))==raw
for bad in [b'',stock.read_bytes()[:-1],stock.read_bytes()+b'bad']:
    try:p.unpack(bad)
    except ValueError:pass
    else:raise AssertionError('Invalid length accepted')
for index in [0,4,5,40,76,84,100,-1]:
    bad=bytearray(stock.read_bytes());bad[index]^=1
    try:p.unpack(bad)
    except ValueError:pass
    else:raise AssertionError('Corruption accepted')
print('PASS packed image roundtrip; truncated/extended/header/body corruption rejected')
print('Application SHA256',hashlib.sha256(raw).hexdigest())

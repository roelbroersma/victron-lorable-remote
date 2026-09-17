"""A temporary read-only server, bound only to the laptop's ESP WiFi address."""
from http.server import HTTPServer, BaseHTTPRequestHandler
import argparse
import importlib.util
import hashlib
from pathlib import Path
spec=importlib.util.spec_from_file_location('packer',Path(__file__).with_name('pack-esp-ota.py'))
packer=importlib.util.module_from_spec(spec);spec.loader.exec_module(packer)
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('image',type=Path);parser.add_argument('--sha256',required=True)
args=parser.parse_args();image=args.image.read_bytes()
if hashlib.sha256(image).hexdigest()!=args.sha256:raise SystemExit('Digest mismatch; not serving')
packer.unpack(image)
class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path=='/health': data=b'LORABLE_OTA_READY'
        elif self.path=='/esp.packed':data=image
        else:self.send_error(404);return
        self.send_response(200);self.send_header('Content-Type','application/octet-stream');self.send_header('Content-Length',str(len(data)));self.send_header('Connection','close');self.end_headers()
        self.wfile.write(data);self.wfile.flush()
        print(f'Completed {self.path}: {len(data)} bytes',flush=True)
print(f'Serving only verified image SHA256 {args.sha256} at 192.168.4.2:8765',flush=True)
HTTPServer(('192.168.4.2',8765),Handler).serve_forever()

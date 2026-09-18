"""One LoRaBLE .bin container; never pass this container to a raw chip flasher."""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import re
import struct
import zlib

spec = importlib.util.spec_from_file_location('esp_pack', Path(__file__).with_name('pack-esp-ota.py'))
esp_pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(esp_pack)
HEADER_SIZE = 256
STM_MAX = 0x31000
ESP_APP_MAX = 0xF0000

def check_stm(stm):
    if not 256 <= len(stm) <= STM_MAX or len(stm) % 8:
        raise ValueError('Application length does not fit RAK11162')
    stack, entry = struct.unpack_from('<II', stm)
    if not 0x20000000 <= stack <= 0x20010000 or stack % 8 or not entry & 1 or not 0x08006000 <= entry < 0x08006000 + len(stm):
        raise ValueError('Invalid RAK application vector; no bootloader or merged images')

def unpack(data):
    if len(data) < HEADER_SIZE:
        raise ValueError('Truncated bundle')
    h = data[:HEADER_SIZE]
    if h[:8] != b'LBRUPD1\0' or struct.unpack_from('<II', h, 8) != (1, 11162):
        raise ValueError('Wrong firmware target/format')
    version = h[16:48].split(b'\0')[0]
    if not re.fullmatch(rb'[0-9][0-9a-z.-]{0,30}', version) or h[16+len(version):48] != bytes(32-len(version)):
        raise ValueError('Invalid version')
    stm_len, esp_len, stm_crc, flags = struct.unpack_from('<IIII', h, 48)
    if flags or h[164:252] != bytes(88) or zlib.crc32(h[:252]) != struct.unpack_from('<I', h, 252)[0]:
        raise ValueError('Invalid header/checksum')
    if len(data) != HEADER_SIZE + stm_len + esp_len:
        raise ValueError('Bundle length mismatch')
    stm, esp = data[HEADER_SIZE:HEADER_SIZE+stm_len], data[HEADER_SIZE+stm_len:]
    check_stm(stm)
    if zlib.crc32(stm) != stm_crc or hashlib.sha256(stm).digest() != h[64:96] or hashlib.sha256(esp).digest() != h[96:128]:
        raise ValueError('Firmware checksum mismatch')
    if esp_len > 0xA5000: raise ValueError('Connectivity image leaves no update metadata space')
    raw = esp_pack.unpack(esp)
    if len(raw) > ESP_APP_MAX or len(raw) != struct.unpack_from('<I', h, 128)[0]:
        raise ValueError('WiFi firmware overlaps internal update staging area')
    if hashlib.sha256(raw).digest() != h[132:164]: raise ValueError('Connectivity application hash mismatch')
    # ESP app descriptor is inside segment 1. Require the exact same release.
    if raw[48:80].split(b'\0')[0] != version:
        raise ValueError('Component version mismatch')
    return version.decode(), stm, esp

def pack(stm, esp, version):
    check_stm(stm)
    raw = esp_pack.unpack(esp)
    h = bytearray(HEADER_SIZE)
    struct.pack_into('<8sII32sIIII', h, 0, b'LBRUPD1\0', 1, 11162, version.encode(), len(stm), len(esp), zlib.crc32(stm), 0)
    h[64:96], h[96:128] = hashlib.sha256(stm).digest(), hashlib.sha256(esp).digest()
    struct.pack_into('<I', h, 128, len(raw))
    h[132:164] = hashlib.sha256(raw).digest()
    struct.pack_into('<I', h, 252, zlib.crc32(h[:252]))
    result = bytes(h) + stm + esp
    unpack(result)
    return result

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stm', type=Path); p.add_argument('--esp', type=Path)
    p.add_argument('--version'); p.add_argument('--output', type=Path)
    p.add_argument('--verify', type=Path)
    a=p.parse_args()
    if a.verify:
        version, stm, esp = unpack(a.verify.read_bytes())
        print(f'Valid LoRaBLE {version}: control={len(stm)}, connectivity={len(esp)} bytes')
    else:
        if not all([a.stm, a.esp, a.version, a.output]): p.error('Provide --stm --esp --version --output')
        data=pack(a.stm.read_bytes(), a.esp.read_bytes(), a.version)
        with a.output.open('xb') as out: out.write(data)
        print(f'Created {a.output.name}: {len(data)} bytes; SHA256={hashlib.sha256(data).hexdigest()}')

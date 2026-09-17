"""Pack an application for the RETAINED Espressif C2 2MB compressed bootloader.

No deletion, no flashing, no partition-table or bootloader generation. Format
matches Espressif esp-bootloader-plus custom_ota_gen.py v2 (Apache-2.0).
"""
import argparse
import hashlib
import lzma
from pathlib import Path
import struct
import zlib

HEADER = struct.Struct('<4sBBBB32sI32sIII')
STORAGE_SIZE = 0xA6000
APP_SIZE = 0x130000

def unpack(data):
    if len(data) < HEADER.size:
        raise ValueError('Truncated OTA header')
    magic, version, compression, encryption, reserved, name, size, digest, base_len, base_crc, crc = HEADER.unpack_from(data)
    if (magic, version, compression, encryption, reserved) != (b'ESP\0', 2, 1, 0, 0):
        raise ValueError('Not an unencrypted compressed v2 ESP image')
    if base_len or base_crc:
        raise ValueError('Delta OTA is not supported')
    if len(data) != HEADER.size + size or len(data) > STORAGE_SIZE:
        raise ValueError('OTA length does not fit the installed storage partition')
    if crc != zlib.crc32(data[:84]):
        raise ValueError('Header CRC mismatch')
    compressed = data[88:]
    if digest != hashlib.md5(compressed).digest() + bytes(16):
        raise ValueError('Compressed data MD5 mismatch')
    # Bound decompression, including hostile/incompatible update files.
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_XZ, memlimit=2*1024*1024)
    raw = decoder.decompress(compressed, max_length=APP_SIZE + 1)
    if len(raw) > APP_SIZE or not decoder.eof or decoder.unused_data:
        raise ValueError('Application size / compressed stream invalid')
    if decoder.check != lzma.CHECK_CRC32:
        raise ValueError('Bootloader requires XZ CRC32')
    if len(raw) < 288 or raw[0] != 0xE9 or struct.unpack_from('<H', raw, 12)[0] != 12:
        raise ValueError('Expected an ESP32-C2 application, not another chip or a merged image')
    return raw

def pack(raw, version='LoRaBLE-C2-26M-v4.10'):
    if not version.startswith('LoRaBLE-C2-26M-') or len(version.encode()) > 31:
        raise ValueError('Explicit C2/26MHz application marker required')
    compressed = lzma.compress(raw, format=lzma.FORMAT_XZ, check=lzma.CHECK_CRC32,
        filters=[{'id':lzma.FILTER_LZMA2, 'preset':6, 'dict_size':65536}])
    header = HEADER.pack(b'ESP\0',2,1,0,0,version.encode(),len(compressed),hashlib.md5(compressed).digest(),0,0,0)
    result = header[:84] + struct.pack('<I',zlib.crc32(header[:84])) + compressed
    if unpack(result) != raw:
        raise ValueError('Roundtrip failed')
    return result

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--version', default='LoRaBLE-C2-26M-v4.10')
    args = parser.parse_args()
    data = args.input.read_bytes()
    if args.verify:
        raw = unpack(data)
        print(f'Valid stock-layout OTA: packed={len(data)} app={len(raw)} sha256={hashlib.sha256(raw).hexdigest()}')
    else:
        if not args.output or args.output.exists():
            parser.error('--output must name a new file; existing files are never overwritten')
        packed = pack(data, args.version)
        args.output.write_bytes(packed)
        print(f'Packed {len(data)} -> {len(packed)} bytes; sha256={hashlib.sha256(packed).hexdigest()}')

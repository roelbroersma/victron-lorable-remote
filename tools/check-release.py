"""Check publication structure, documents, firmware and installer resources."""
import argparse
import base64
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
from urllib.parse import unquote
sys.dont_write_bytecode = True


def check(root):
    root = root.resolve()
    manifest = json.loads((root / 'firmware/manifest.json').read_text(encoding='utf-8'))
    version = manifest['version']
    assert manifest['target'] == 'RAK11162' and manifest['format'] == 1
    item = manifest['firmware']
    assert item['path'] == f'firmware/LoRaBLE-Remote-{version}.bin'
    image = (root / item['path']).read_bytes()
    assert len(image) == item['bytes'] and hashlib.sha256(image).hexdigest() == item['sha256']
    spec = importlib.util.spec_from_file_location('release_bundle', root / 'tools/update-bundle.py')
    bundle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bundle)
    found, control, packed = bundle.unpack(image)
    assert found == version
    raw = bundle.esp_pack.unpack(packed)
    header = (root / 'stm32/portal_asset.h').read_text(encoding='utf-8')
    asset = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', header))
    html = (root / 'web/index.html').read_bytes()
    assert gzip.decompress(asset) == html, 'Generated web asset differs from its source'
    assert asset in raw, 'Firmware does not contain the current web interface'
    assert b'id="firmware_badge"' in html and b'v4.10.0' not in html, 'Stale interface version'
    helper_path = manifest['installer_helper']['path']
    assert helper_path == 'installer/bootstrap-image.json'
    helper_json = (root / helper_path).read_bytes()
    assert hashlib.sha256(helper_json).hexdigest() == manifest['installer_helper']['sha256']
    helper = json.loads(helper_json)
    helper_raw = base64.b64decode(helper['image_base64'], validate=True)
    assert helper['target'] == 'RAK11162' and helper['protocol'] == 1 and helper['bsp'] == '4.2.4'
    assert hashlib.sha256(helper_raw).hexdigest() == helper['sha256'] == manifest['installer_helper']['image_sha256']
    sp, entry = struct.unpack_from('<II', helper_raw)
    assert 256 <= len(helper_raw) <= 0x31000 and len(helper_raw) % 8 == 0
    assert 0x20000000 <= sp <= 0x20010000 and sp % 8 == 0
    assert entry & 1 and 0x08006000 <= entry < 0x08006000 + len(helper_raw)
    expected_firmware = {'manifest.json', Path(item['path']).name}
    assert {p.name for p in (root / 'firmware').iterdir()} == expected_firmware, 'Unexpected old firmware assets'
    for required in ['README.md', 'README.en.md', 'Install.cmd', 'LICENSE', 'THIRD-PARTY-NOTICES.md',
                     'docs/INSTALL.md', 'docs/NETWORKS.md', 'docs/EXAMPLES.md', 'docs/DEVELOPMENT.md', 'docs/RELEASE.md',
                     'installer/START-HERE.md', 'updater/ram-loader.c', 'updater/ram-loader.ld']:
        assert (root / required).is_file(), 'Missing file: ' + required
    assert not (root / 'stm32/settings.local.h').exists(), 'Private settings in publication'
    for pattern in ['VALIDATION*.md', 'POWER-BUDGET.md', 'BATTERYPROTECT-RESEARCH.md', 'SHA256SUMS']:
        assert not list(root.glob(pattern)), 'Development record in publication: ' + pattern
    assert not list((root / 'docs').glob('RELEASE-v*.md')), 'Superseded release notes'
    docs = list(root.glob('*.md')) + list((root / 'docs').rglob('*.md')) + [root / 'installer/START-HERE.md']
    links = 0
    for doc in docs:
        content = doc.read_text(encoding='utf-8')
        assert not re.search(r'\b(?:un)?tested\b|\b(?:on)?getest\b|testrelease|VALIDATION-v', content, re.I), 'Stale release narrative: ' + str(doc)
        for target in re.findall(r'!?\[[^\]\n]*\]\(([^)\n]+)\)', content):
            target = target.split(' "', 1)[0].strip('<>')
            if re.match(r'^[a-z][a-z0-9+.-]*:', target, re.I) or target.startswith('#'):
                continue
            relative = unquote(target.split('#', 1)[0])
            resolved = (doc.parent / relative).resolve()
            resolved.relative_to(root)
            assert resolved.exists(), f'Broken link in {doc.name}: {target}'
            links += 1
    notices = root / 'third_party_notices'
    for sdk in ['RAK-RUI-4.2.4', 'ESP-IDF-5.5.5']:
        assert (notices / sdk).is_dir(), 'Missing SDK terms'
    for p in notices.rglob('*'):
        assert not p.is_symlink(), 'Unexpected notice symlink'
        assert p.suffix.lower() not in {'.c', '.cpp', '.h', '.hpp'}, 'SDK source in license collection'
    print(f'PASS: release {version}; complete firmware, helper, embedded UI, {len(docs)} documents and {links} local links')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    check(parser.parse_args().root)

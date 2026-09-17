"""Build a checked release ZIP, excluding unrelated/historical repository files."""
import argparse
import hashlib
from pathlib import Path
import re
import zipfile


def release_zip(root, output):
    root = root.resolve()
    entries = {'SHA256SUMS': root / 'SHA256SUMS'}
    for line in entries['SHA256SUMS'].read_text(encoding='utf-8').splitlines():
        digest, relative = line.split('  ', 1)
        if not re.fullmatch('[0-9a-f]{64}', digest):
            raise ValueError('Invalid checksum')
        target = (root / relative).resolve()
        target.relative_to(root)  # Reject a path outside the release root.
        if Path(relative).is_absolute() or '..' in Path(relative).parts:
            raise ValueError('Invalid release path')
        if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
            raise ValueError('Checksum mismatch: ' + relative)
        entries[relative] = target
    for target in (root / 'third_party_notices').rglob('*'):
        if target.is_symlink():
            raise ValueError('Symlinks are not release notice files')
        if target.is_file():
            entries[target.relative_to(root).as_posix()] = target
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for relative, target in sorted(entries.items()):
            archive.write(target, 'LoRaBLE-Remote/' + relative)
    print(f'Checked release ZIP: {len(entries)} files, {output.stat().st_size} bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    parser.add_argument('--output', type=Path, required=True)
    arguments = parser.parse_args()
    release_zip(arguments.root, arguments.output)

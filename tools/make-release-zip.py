"""Build the minimal Windows installer and checksums from a clean release tree."""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import zipfile

def release_zip(root, output):
    root = root.resolve()
    spec = importlib.util.spec_from_file_location('release_check', root / 'tools/check-release.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    manifest = module.check(root)
    entries = {}
    for relative in ['Install.cmd', 'LICENSE', 'THIRD-PARTY-NOTICES.md',
                     'installer/First-Install.ps1', 'installer/FirstInstall.Core.ps1',
                     'installer/FirstInstall.Windows.cs', 'installer/Start-First-Install.cmd',
                     'installer/bootstrap-image.json', 'installer/Flash-USB.ps1',
                     'installer/Usb-Portal.ps1', 'installer/Bundle.ps1', 'installer/Start-USB-Flash.cmd',
                     'firmware/manifest.json', manifest['firmware']['path']]:
        entries[relative] = (root / relative).read_bytes()
    entries['START-HERE.md'] = (root / 'installer/START-HERE.md').read_bytes()
    for target in (root / 'third_party_notices').rglob('*'):
        if target.is_symlink():
            raise ValueError('Symlinks are not release notices')
        if target.is_file():
            entries[target.relative_to(root).as_posix()] = target.read_bytes()
    entries['SHA256SUMS'] = ''.join(hashlib.sha256(data).hexdigest() + '  ' + name + '\n'
                                  for name, data in sorted(entries.items())).encode('utf-8')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for relative, data in sorted(entries.items()):
            info = zipfile.ZipInfo(relative, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        assert archive.read(manifest['firmware']['path']) == (root / manifest['firmware']['path']).read_bytes()
        assert 'Install.cmd' in archive.namelist()
    # Fresh download checksums cover actual release assets, not repository files.
    checksum_file = output.parent / 'SHA256SUMS'
    checksum_text = hashlib.sha256((root / manifest['firmware']['path']).read_bytes()).hexdigest() + '  ' + Path(manifest['firmware']['path']).name + '\n'
    checksum_text += hashlib.sha256(output.read_bytes()).hexdigest() + '  ' + output.name + '\n'
    with checksum_file.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(checksum_text)
    print(f'Windows package: {len(entries)} files, {output.stat().st_size} bytes; {checksum_file}')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    release_zip(args.root, args.output)

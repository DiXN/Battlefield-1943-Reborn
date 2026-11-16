# The official build manifest of a server package (the main backend's build check, plasma.py ServerCheck, 16 Nov 2026):
# every program file of the package (bin, server, python) with its SHA-256, and their digest, as server/gmhost.py
# reports them when it registers. Run at a release on the unpacked package folder; the manifest goes in the official
# server set's data folder, attest\ (the main backend reads it again when it changes).
#   python -I tools/build_manifest.py <unpacked package folder> <out .json> [--platform windows|linux]
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'server'))
import gmhost  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('package')
    ap.add_argument('out')
    ap.add_argument('--platform', choices=['windows', 'linux'], default='')
    a = ap.parse_args()
    root = os.path.abspath(a.package)
    with open(os.path.join(root, 'version.txt'), encoding='utf-8') as f:
        version = (f.read().strip().splitlines() or [''])[0].strip()
    platform = a.platform or ('windows' if os.path.exists(os.path.join(root, 'bin', 'bf1943.exe')) else 'linux')
    files = gmhost.build_files(root)
    if not version or not files:
        sys.exit('no version.txt or no program files in %s' % root)
    manifest = {'version': version, 'platform': platform, 'digest': gmhost.build_digest(files), 'files': files}
    with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    print('%s %s: %d files, digest %s -> %s' % (version, platform, len(files), manifest['digest'], a.out))


if __name__ == '__main__':
    main()

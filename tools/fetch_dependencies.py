#!/usr/bin/env python3
"""Populate an isolated, hash-verified source cache. Never runs at library runtime."""
import hashlib, json, pathlib, tarfile, urllib.request
ROOT = pathlib.Path(__file__).resolve().parents[1]
for dep in json.loads((ROOT / 'cmake/dependencies.json').read_text()):
    archive = ROOT / '.deps' / (dep['name'] + '.archive')
    archive.parent.mkdir(exist_ok=True)
    if not archive.exists():
        with urllib.request.urlopen(dep['url'], timeout=60) as response:
            archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != dep['sha256']:
        raise SystemExit('Dependency hash mismatch: ' + dep['name'])
    with tarfile.open(archive) as source:
        for member in source:
            parts = pathlib.PurePosixPath(member.name).parts[1:]
            if not parts:
                continue
            if '..' in parts or member.issym() or member.islnk():
                raise SystemExit('Unsupported archive member')
            if member.isfile():
                path = ROOT / '.deps' / dep['name'] / pathlib.Path(*parts)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(source.extractfile(member).read())
    print('Verified', dep['name'], dep['sha256'])

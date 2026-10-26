#!/usr/bin/env python3
"""Local publication gate. Reports paths/reasons only, never matched secret values."""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
# Construct patterns so the checker itself is not a private-reference false positive.
PATTERNS = {
    'private key': re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?' + r'PRIVATE KEY-----'),
    'credential': re.compile(r'gh[pousr]_' + r'[A-Za-z0-9]{30,}|github_pat_' + r'[A-Za-z0-9_]{30,}|AKIA' + r'[0-9A-Z]{16}'),
    'personal path': re.compile(r'/(?:Users|home)/' + r'[A-Za-z0-9_.-]+/'),
    'private service': re.compile(r'localhost:' + r'8929|' + 'bird' + 'watch' + r'/astroattitude', re.I),
    'private project': re.compile('Bird' + 'Watch|stellar-pose' + '2-2026|calibration-stellar' + '-2026', re.I),
}
MEDIA = {'.dng','.jpg','.jpeg','.png','.heic','.tif','.tiff','.mp4','.mov','.pdf','.zip','.tar','.gz','.a','.so','.dylib','.apk','.aar','.class','.jks','.keystore'}
LINK = re.compile(r'!?\[[^\]\n]*\]\(([^)\n]+)\)')

def anchors(text):
    result=set();seen={}
    for line in text.splitlines():
        if re.match(r'^#{1,6} ',line):
            title=re.sub(r'[`*_]','',line.lstrip('#').strip()).lower()
            key=re.sub(r'[^\w\- ]','',title).replace(' ','-')
            count=seen.get(key,0);seen[key]=count+1
            result.add(key+('-'+str(count) if count else ''))
    return result

def check(root=ROOT):
    errors=[]
    inventory=json.loads((root/'publication-files.json').read_text())
    entries=inventory['files']
    files=set(subprocess.check_output(['git','ls-files','--cached','--others','--exclude-standard','-z'],cwd=root).decode().split('\0'))-{''}
    expected=set(entries)|{'publication-files.json'}
    for p in sorted(files-expected):errors.append((p,'not in reviewed inventory'))
    for p in sorted(expected-files):errors.append((p,'inventory entry missing'))
    for name in sorted(files):
        p=root/name
        if p.is_symlink() or not p.resolve().is_relative_to(root.resolve()):
            errors.append((name,'symlink or escaping path'));continue
        if not p.is_file():errors.append((name,'not a regular file'));continue
        data=p.read_bytes();entry=entries.get(name,{})
        if len(data)>2_000_000:errors.append((name,'oversized source file'))
        if entry.get('binary_sha256'):
            if name!='bindings/android/gradle/wrapper/gradle-wrapper.jar':errors.append((name,'unapproved binary path'))
            if hashlib.sha256(data).hexdigest()!=entry['binary_sha256']:errors.append((name,'binary hash changed'))
            continue
        if p.suffix.lower() in MEDIA:errors.append((name,'unapproved media/archive/binary'))
        try:text=data.decode('utf-8')
        except UnicodeDecodeError:errors.append((name,'unapproved non-text data'));continue
        if '\x00' in text:errors.append((name,'unapproved binary content'))
        for category,pattern in PATTERNS.items():
            if pattern.search(text):errors.append((name,category))
        if p.suffix=='.md':
            for raw in LINK.findall(text):
                dest=raw.strip().split(' "')[0].strip('<>');u=urlsplit(dest)
                if u.scheme or u.netloc:
                    if u.scheme not in ('http','https','mailto'):errors.append((name,'nonportable link scheme'))
                    continue
                path=(p.parent/unquote(u.path)).resolve() if u.path else p.resolve()
                if not path.is_relative_to(root.resolve()) or not path.exists():errors.append((name,'missing or escaping relative link'))
                elif u.fragment and path.suffix=='.md' and unquote(u.fragment) not in anchors(path.read_text()):errors.append((name,'missing Markdown anchor'))
    synthetic=inventory.get('synthetic_provenance',{})
    for name in synthetic.get('generators',[]):
        if name not in entries:errors.append((name,'synthetic generator absent'))
    if synthetic.get('captured_inputs') is not False:errors.append(('publication-files.json','synthetic provenance missing'))
    if (root/'LICENSE').exists() and (root/'bindings/python/LICENSE').exists():
        if (root/'LICENSE').read_bytes()!=(root/'bindings/python/LICENSE').read_bytes():errors.append(('bindings/python/LICENSE','license differs from root'))
    return errors

if __name__=='__main__':
    issues=check()
    for path,reason in issues:print(f'{path}: {reason}')
    print(f'Publication gate: {len(issues)} issue(s)')
    sys.exit(bool(issues))

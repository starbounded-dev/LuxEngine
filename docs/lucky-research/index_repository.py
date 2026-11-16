"""Read-only indexing of the supplied Lucky snapshot; writes research artifacts only."""
import collections
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path('M:/LuckyEngine/MAC-Source')
OUT = Path('C:/Git/luxengine/docs/lucky-research')
OUT.mkdir(parents=True, exist_ok=True)

def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args]).decode('utf-8', 'replace')

raw = git('log', '--all', '--topo-order', '--format=%H%x1f%aI%x1f%cI%x1f%P%x1f%B%x1e')
commits = []
for record in raw.split('\x1e'):
    record = record.strip()
    if not record:
        continue
    sha, authored, committed, parents, message = record.split('\x1f', 4)
    commits.append(dict(sha=sha, authoredAt=authored, committedAt=committed,
                        parents=parents.split(), subject=message.split('\n')[0], message=message.strip()))
baseline = set(git('rev-list', 'pr-base').split())
known = {c['sha'] for c in commits}
for c in commits:
    c['inHazel2024Baseline'] = c['sha'] in baseline
missing = sorted({p for c in commits for p in c['parents'] if p not in known})
ledger = dict(head=git('rev-parse', 'HEAD').strip(), baseline=git('rev-parse', 'pr-base').strip(),
              count=len(commits), baselineCount=len(baseline), missingParents=missing, commits=commits)
(OUT / 'Lucky-Commit-History.json').write_text(json.dumps(ledger, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')

staged = git('ls-files', '--stage', '-z').split('\0')
files = []
code = {'.cpp', '.h', '.hpp', '.c', '.cc', '.cxx', '.mm', '.m', '.cs', '.glsl', '.glslh', '.hlsl', '.hlsli', '.py', '.lua', '.cmake', '.sh', '.ps1', '.yml', '.yaml', '.md', '.toml', '.json', '.proto', '.html', '.js', '.ts'}
for entry in staged:
    if not entry:
        continue
    header, path = entry.split('\t', 1)
    mode, blob, stage = header.split()
    item = dict(path=path, mode=mode, blob=blob, stage=stage)
    parts = Path(path).parts
    item['area'] = '/'.join(parts[:4]) if path.startswith('Hazel/src/Hazel/') else parts[0]
    item['vendored'] = 'vendor' in [p.lower() for p in parts]
    item['category'] = 'submodule' if mode == '160000' else ('source-or-text' if Path(path).suffix.lower() in code else 'asset-or-other')
    physical = ROOT / path
    item['present'] = physical.exists()
    if item['category'] == 'source-or-text' and not item['vendored'] and physical.is_file():
        data = physical.read_bytes()
        item['bytes'] = len(data)
        item['lines'] = len(data.splitlines())
        item['workingSha256'] = hashlib.sha256(data).hexdigest()
        # Some tracked blobs themselves use CRLF. Accept either exact bytes or
        # LF-normalized working text; do not misreport those blobs as modified.
        raw_hash = hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
        normalized = data.replace(b'\r\n', b'\n')
        h = hashlib.sha1(b'blob '+str(len(normalized)).encode()+b'\0'+normalized).hexdigest()
        item['matchesIndexedBlobRawOrAfterLFNormalization'] = raw_hash == blob or h == blob
    files.append(item)
inventory = dict(head=ledger['head'], trackedEntries=len(files),
                 sourceTextFiles=sum('lines' in f for f in files),
                 sourceTextLines=sum(f.get('lines', 0) for f in files),
                 submodules=[f for f in files if f['category']=='submodule'],
                 textMismatches=[f['path'] for f in files if f.get('matchesIndexedBlobRawOrAfterLFNormalization') is False],
                 files=files)
(OUT / 'Lucky-File-Inventory.json').write_text(json.dumps(inventory, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')
groups = collections.defaultdict(lambda: [0, 0])
for f in files:
    if 'lines' in f:
        groups[f['area']][0] += 1
        groups[f['area']][1] += f['lines']
years = collections.Counter(c['committedAt'][:4] for c in commits)
summary = dict(historyCount=len(commits), baselineCount=len(baseline), afterBaseline=len(commits)-len(baseline),
               missingParents=missing, years=dict(sorted(years.items())), trackedEntries=len(files),
               sourceTextFiles=inventory['sourceTextFiles'], sourceTextLines=inventory['sourceTextLines'],
               submoduleCount=len(inventory['submodules']), textMismatches=inventory['textMismatches'],
               areas=dict(sorted(groups.items())))
(OUT / 'inventory-summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
print(json.dumps(summary, indent=2))

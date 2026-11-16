"""Assemble the investigated chapters into the user's existing comparison file."""
from pathlib import Path
import json
import re

HERE = Path(__file__).resolve().parent
TARGET = HERE.parent / 'LuckyEngine-Comparison.md'

def chapter(filename, title):
    text = (HERE / filename).read_text(encoding='utf-8')
    lines = text.splitlines()
    if lines and lines[0].startswith('# '):
        lines = lines[1:]
    # The report has one shared source/methodology preface. Standalone chapter
    # metadata remains in the research files without repeating it in the report.
    first_section = next((i for i, line in enumerate(lines) if line.startswith('## ')), 0)
    lines = lines[first_section:]
    # Chapter headings nest under the consolidated report's second level.
    lines = [('#' + line if line.startswith('#') else line) for line in lines]
    return '## ' + title + '\n\n' + '\n'.join(lines).strip() + '\n'

parts = [(HERE / 'overview.md').read_text(encoding='utf-8').strip()]
parts.append(chapter('rendering.md', 'Rendering implementation comparison'))
parts.append(chapter('content-editor-build.md', 'Content, editor, build, and user documentation'))
parts.append(chapter('runtime-simulation.md', 'Runtime, simulation, ownership, and automation'))
parts.append(chapter('reflection-crash-web.md', 'Reflection, crash recovery, services, and browser code'))
parts.append((HERE / 'decisions.md').read_text(encoding='utf-8').strip())
document = '\n\n'.join(parts) + '\n'
TARGET.write_text(document, encoding='utf-8', newline='\n')

errors = []
local_links = []
for match in re.finditer(r'\[[^\]\n]*\]\(([^)\n]+)\)', document):
    target = match.group(1).strip('<>')
    if not re.match(r'^[A-Za-z]:[/\\]', target):
        continue
    path_text, line = target, None
    line_match = re.match(r'^(.*):(\d+)$', target)
    if line_match:
        path_text, line = line_match[1], int(line_match[2])
    path = Path(path_text)
    local_links.append(target)
    if not path.is_file():
        errors.append('Missing file: ' + target)
    elif line:
        count = len(path.read_bytes().splitlines())
        if not 1 <= line <= count:
            errors.append(f'Line outside file ({count}): {target}')

headings = re.findall(r'^#{1,6} (.+)$', document, re.M)
anchors = {re.sub(r'[^\w\- ]', '', h.lower()).replace(' ', '-') for h in headings}
for fragment in re.findall(r'\]\(#([^)]*)\)', document):
    if fragment not in anchors:
        errors.append('Missing heading anchor: ' + fragment)

ledger = json.loads((HERE / 'Lucky-Commit-History.json').read_text(encoding='utf-8'))
commits = {c['sha'] for c in ledger['commits']}
missing_parents = {p for c in ledger['commits'] for p in c['parents'] if p not in commits}
if missing_parents:
    errors.append('Incomplete Lucky parent closure')
inventory = json.loads((HERE / 'Lucky-File-Inventory.json').read_text(encoding='utf-8'))
if inventory['textMismatches']:
    errors.append('Lucky indexed text mismatch')

result = dict(path=str(TARGET), lines=len(document.splitlines()), words=len(document.split()),
              characters=len(document), localLinks=len(local_links),
              uniqueLocalLinks=len(set(local_links)), headings=len(headings),
              luckyCommits=len(commits), trackedEntries=inventory['trackedEntries'], errors=errors)
(HERE / 'report-validation.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
print(json.dumps(result, indent=2))
if errors:
    raise SystemExit(1)

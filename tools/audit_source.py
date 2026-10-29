"""Check public-source file policy. Does not inspect credentials or user files."""
from pathlib import Path
import re, subprocess, sys
root=Path(__file__).resolve().parent.parent
try:
    names=subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0')
    files=[root/name for name in names if name]
except (OSError,subprocess.CalledProcessError):
    files=[]
if not files:
    files=[p for p in root.rglob('*') if p.is_file() and not any(x in {'.git','build','app','dist','__pycache__'} for x in p.relative_to(root).parts)]
forbidden={'.exe','.dll','.xpl','.pdb','.obj','.spv','.zip','.rar','.log','.prf'}
patterns=[re.compile(r'gh[pousr]_[A-Za-z0-9]{30,}'),re.compile(r'github_pat_[A-Za-z0-9_]{40,}'),re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')]
problems=[]
for path in files:
    relative=path.relative_to(root)
    if path.suffix.lower() in forbidden or path.name in {'settings.ini','launcher.lock'}:problems.append(f'Excluded runtime/user artifact: {relative}')
    if path.suffix.lower() in {'.ico','.png'}:continue
    data=path.read_text(encoding='utf-8',errors='replace')
    if any(p.search(data) for p in patterns):problems.append(f'Possible secret: {relative}')
    if re.search(r'[A-Z]:\\Users\\[^\\\s]+\\(?:OneDrive|Documents)\\',data):problems.append(f'Local development path: {relative}')
if problems:
    print('\n'.join(problems));sys.exit(1)
print(f'PASS: {len(files)} public-source files; no runtime/user artifacts, local development paths or matched secret patterns.')

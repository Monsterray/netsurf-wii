#!/usr/bin/env python3
"""The revision gate rejects old and modified SDKs without changing a checkout."""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    source = path / 'checkout'
    source.mkdir()
    def git(*args):
        return subprocess.check_output(['git', '-C', str(source), *args], stderr=subprocess.PIPE).decode().strip()
    git('init', '-q')
    (source / 'sdk').mkdir()
    header = source / 'sdk/hbc_agent.h'
    header.write_text('original\n')
    git('add', '.')
    git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture')
    revision = git('rev-parse', 'HEAD')
    shutil.copy(ROOT / 'check-hbc.sh', path)
    lock = path / 'hbc-reborn.env'
    lock.write_text(f'HBC_REBORN_COMMIT={revision}\nHBC_REBORN_VERSION=test\n')
    def check(expected):
        result = subprocess.run(['bash', str(path / 'check-hbc.sh'), str(source)], capture_output=True)
        assert (result.returncode == 0) == expected, result.stderr.decode()
    check(True)
    (source / 'unrelated-build-artifact').write_text('ignored by gate\n')
    check(True)
    header.write_text('modified\n')
    check(False)
    git('add', 'sdk')
    check(False)
    git('restore', '--staged', 'sdk')
    git('restore', 'sdk')
    lock.write_text('HBC_REBORN_COMMIT=old\nHBC_REBORN_VERSION=test\n')
    check(False)
print('PASS: reviewed HBC revision, modified/staged SDK rejection, unrelated artifacts allowed')

# The leased-job preflight must refuse an old HBC or any running app.
import json
import sys
script = (ROOT / 'hardware-test.sh').read_text()
preflight = script.split("<<'PY_CHECK'\n", 1)[1].split("\nPY_CHECK", 1)[0]
with tempfile.TemporaryDirectory() as directory:
    status = Path(directory) / 'status.json'
    for version, agent, success in [('1.10.2', False, True),
                                    ('1.10.0', False, False),
                                    ('1.10.2', True, False)]:
        status.write_text(json.dumps({'version': version, 'agent': agent}))
        result = subprocess.run([sys.executable, '-c', preflight, str(status), '1.10.2'], capture_output=True)
        assert (result.returncode == 0) == success, result.stderr.decode()
print('PASS: leased preflight rejects older HBC and running apps before staging')

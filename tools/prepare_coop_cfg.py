"""Assemble the legacy co-op CFG overlay without creating a patched ROM."""
import argparse
import os
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
ENGINE = Path(os.environ.get('SNESRECOMP_ROOT', ROOT / 'snesrecomp'))
sys.path.insert(0, str(ENGINE / 'recompiler'))
from v2.atomic_output import AtomicOutputDir

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cfg-dir', type=Path, required=True)
args = parser.parse_args()
if args.cfg_dir.resolve() != ROOT / '.build/recomp-coop':
    parser.error('unexpected co-op CFG output directory')
transaction = AtomicOutputDir(args.cfg_dir, seed_existing=False)
try:
    for source in sorted((ROOT / 'recomp').glob('bank*.cfg')):
        text = source.read_text(encoding='utf-8')
        text = '\n'.join(line for line in text.splitlines()
                         if line != 'name 0086df GameMode14_InLevel_0086DF') + '\n'
        (transaction.staging / source.name).write_text(text, encoding='utf-8')
    for source in sorted((ROOT / 'recomp/coop').glob('bank*.cfg')):
        target = transaction.staging / source.name
        text = source.read_text(encoding='utf-8')
        if target.exists():
            text = '\n'.join(line for line in text.splitlines()
                             if not line.startswith(('bank ', 'bank=', 'includes ', 'comment ')))
            text = target.read_text(encoding='utf-8') + '\n' + text
        target.write_text(text + '\n', encoding='utf-8')
    transaction.publish()
finally:
    transaction.cleanup()

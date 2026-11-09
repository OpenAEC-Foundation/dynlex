#!/usr/bin/env python3
"""Export a selected dynlex-ui checkout and update the site's dependency lock."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from source_package import SourcePackage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ui', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    exporter = args.ui.resolve() / 'scripts/export_sdk.py'
    if not exporter.is_file():
        parser.error(f'The selected checkout has no SDK exporter: {exporter}')
    project_path = root / 'site/project.json'
    project = json.loads(project_path.read_text())
    archive = root / 'site' / project['dependency']['archive']
    subprocess.run([sys.executable, str(exporter), '--dynlex', str(root), '--output', str(archive)], check=True)
    package = SourcePackage(archive)
    if package.manifest['package'] != 'dynlex-ui':
        raise ValueError('The site requires a dynlex-ui source package')
    project['dependency']['contentHash'] = package.manifest['contentHash']
    temporary = project_path.with_suffix('.json.tmp')
    temporary.write_text(json.dumps(project, indent=2) + '\n')
    temporary.replace(project_path)
    print(f"Pinned dynlex-ui: {project['dependency']['contentHash']}")


if __name__ == '__main__':
    main()

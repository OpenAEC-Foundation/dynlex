#!/usr/bin/env python3
"""Removing embedded files must rebuild the bundle without changing its command."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='dynlex-embedded-files-') as temporary:
    root = Path(temporary)
    library = root / 'lib'
    library.mkdir()
    (library / 'first.dl').write_text('first')
    removed = library / 'removed.dl'
    removed.write_text('removed')
    packer = root / 'pack.py'
    packer.write_text('''import json,sys
from pathlib import Path
root=Path(sys.argv[1])
Path(sys.argv[2]).write_text(json.dumps({p.name:p.read_text() for p in sorted(root.glob('*'))}))
''')
    (root / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(embedded_files NONE)
include("{ROOT / 'cmake/EmbeddedDirectory.cmake'}")
dynlex_embedded_directory_dependencies("${{CMAKE_CURRENT_SOURCE_DIR}}/lib" "${{CMAKE_CURRENT_BINARY_DIR}}/embedded-files.txt" inputs)
add_custom_command(OUTPUT "${{CMAKE_CURRENT_BINARY_DIR}}/bundle.json"
 COMMAND "{sys.executable}" "${{CMAKE_CURRENT_SOURCE_DIR}}/pack.py" "${{CMAKE_CURRENT_SOURCE_DIR}}/lib" "${{CMAKE_CURRENT_BINARY_DIR}}/bundle.json"
 DEPENDS ${{inputs}} VERBATIM)
add_custom_target(bundle ALL DEPENDS "${{CMAKE_CURRENT_BINARY_DIR}}/bundle.json")
''')
    build = root / 'build'
    subprocess.run(['cmake','-S',str(root),'-B',str(build),'-G','Ninja'],check=True,capture_output=True)
    def contents():
        subprocess.run(['cmake','--build',str(build)],check=True,capture_output=True)
        return json.loads((build / 'bundle.json').read_text())
    assert contents() == {'first.dl':'first','removed.dl':'removed'}
    removed.unlink()
    assert contents() == {'first.dl':'first'}, 'Removed source remained embedded'
    (library / 'first.dl').write_text('edited')
    assert contents() == {'first.dl':'edited'}
    (library / 'added.dl').write_text('added')
    assert contents() == {'added.dl':'added','first.dl':'edited'}
print('Embedded directory bundles rebuild on deletion, edits and additions.')

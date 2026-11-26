"""Resolve project aliases and transitive exports from any working directory."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import queue
import threading

compiler = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='dynlex-package-imports-') as temporary:
    root = Path(temporary)
    package = root / '.dynlex/packages/example'
    (package / 'api').mkdir(parents=True)
    (package / 'api/main.dl').write_text('import helper/main.dl\nimport ./helper.dl\nanswer means: helper\n')
    (package / 'api/helper.dl').write_text('helper means: shared answer\n')
    (package / 'dynlex.json').write_text('{"schema":1,"dependencies":{}}')
    helper = root / '.dynlex/packages/helper'
    (helper / 'api').mkdir(parents=True)
    (helper / 'api/main.dl').write_text('shared answer means: 42\n')
    (root / 'ui').mkdir()
    (root / 'ui/main.dl').write_text('answer means: 0\n')
    (root / 'main.dl').write_text('import lib/std.dl\nimport ui/main.dl\nprint line answer\n')
    source = {'path': '../example'}
    manifest = {'schema': 1, 'dependencies': {'ui': source}}
    lock = {'schema': 1, 'requests': {'ui': source}, 'dependencies': {'ui': 'example'}, 'packages': {'example': {
        'root': '.dynlex/packages/example', 'request': source, 'exports': {'*': 'api/*'}, 'dependencies': {'helper': 'helper'}},
        'helper': {'root': '.dynlex/packages/helper', 'exports': {'*': 'api/*'}, 'dependencies': {}}}}
    (root / 'dynlex.json').write_text(json.dumps(manifest))
    (root / 'dynlex.lock').write_text(json.dumps(lock))
    output = root / 'program.out'
    options = ['--cache-dir', str(root / 'compiler-cache')]
    unrelated = Path(temporary).anchor
    subprocess.run([compiler, str(root / 'main.dl'), *options, '-o', str(output)], cwd=unrelated, check=True)
    assert subprocess.check_output([str(output)], text=True) == '42\n'
    # The language service/dependency inspector uses exactly the same resolver.
    graph = json.loads(subprocess.check_output([compiler, str(root / 'main.dl'), '--dump-dependencies'], cwd=unrelated, text=True))
    assert graph['complete']
    assert (package / 'api/helper.dl').resolve() in {Path(name).resolve() for name in graph['sources']}
    (package / 'next').mkdir()
    (package / 'next/main.dl').write_text('answer means: 84\n')
    lock['packages']['example']['exports'] = {'*': 'next/*'}
    (root / 'dynlex.lock').write_text(json.dumps(lock))
    subprocess.run([compiler, str(root / 'main.dl'), *options, '-o', str(output)], cwd=unrelated, check=True)
    assert subprocess.check_output([str(output)], text=True) == '84\n', 'Changing locked exports reused stale compiler results'
    lock['packages']['example']['exports'] = {'public.dl': 'api/main.dl'}
    (root / 'dynlex.lock').write_text(json.dumps(lock))
    failed = subprocess.run([compiler, str(root / 'main.dl'), *options, '-o', str(output)], cwd=unrelated, capture_output=True, text=True)
    assert failed.returncode != 0 and 'does not export' in failed.stderr + failed.stdout
    (root / 'dynlex.lock').unlink()
    failed = subprocess.run([compiler, str(root / 'main.dl'), *options, '-o', str(output)], cwd=unrelated, capture_output=True, text=True)
    assert failed.returncode != 0 and 'lockfile' in failed.stderr + failed.stdout
    (root / 'dynlex.lock').write_text(json.dumps(lock))
    server = subprocess.Popen([compiler, '--stdio'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
    def send(message):
        data = json.dumps({'jsonrpc': '2.0', **message}).encode()
        server.stdin.write(f'Content-Length: {len(data)}\r\n\r\n'.encode() + data)
        server.stdin.flush()
    messages = queue.Queue()
    def read_messages():
        try:
            while True:
                headers = {}
                while (line := server.stdout.readline().decode().strip()):
                    key, value = line.split(':', 1)
                    headers[key.lower()] = value.strip()
                if not headers:
                    return
                length = int(headers['content-length'])
                data = bytearray()
                while len(data) < length:
                    chunk = server.stdout.read(length - len(data))
                    assert chunk, 'The language server closed an incomplete message'
                    data.extend(chunk)
                messages.put(json.loads(data))
        except BaseException as error:
            messages.put(error)
    reader = threading.Thread(target=read_messages, daemon=True)
    reader.start()
    def receive():
        result = messages.get(timeout=10)
        if isinstance(result, BaseException):
            raise result
        return result
    try:
        send({'id': 1, 'method': 'initialize', 'params': {'processId': None, 'rootUri': root.as_uri(), 'capabilities': {}}})
        while receive().get('id') != 1:
            pass
        send({'method': 'initialized', 'params': {}})
        uri = (root / 'missing.dl').as_uri()
        send({'method': 'textDocument/didOpen', 'params': {'textDocument': {'uri': uri, 'languageId': 'dynlex', 'version': 1, 'text': 'import widget/main.dl\n'}}})
        while True:
            message = receive()
            if message.get('method') == 'textDocument/publishDiagnostics' and message['params']['uri'] == uri:
                diagnostics = message['params']['diagnostics']
                if diagnostics:
                    break
        send({'id': 2, 'method': 'textDocument/codeAction', 'params': {'textDocument': {'uri': uri},
              'range': diagnostics[0]['range'], 'context': {'diagnostics': diagnostics}}})
        while (reply := receive()).get('id') != 2:
            pass
        assert any(action.get('command', {}).get('command') == 'dynlex.addLibrary' and action['command']['arguments'] == ['widget'] for action in reply['result'])
    finally:
        server.terminate()
        server.wait(timeout=10)
        reader.join(timeout=2)
        server.stdin.close()
        server.stdout.close()
        server.stderr.close()
print('Project libraries resolve aliases, sibling files and exports independently of the working directory.')

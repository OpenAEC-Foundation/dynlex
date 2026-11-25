#!/usr/bin/env python3
"""Exercise the actual debug adapter with isolated, library-free programs."""
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time


class Session:
    def __init__(self, compiler, directory, environment=None):
        self.process = subprocess.Popen(
            [str(compiler), '--dap'], cwd=directory, env=environment,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        self.messages = queue.Queue()
        self.received = []
        self.unmatched = []
        self.sequence = 0
        self.reader = threading.Thread(target=self.read_messages, daemon=True)
        self.reader.start()

    def read_messages(self):
        try:
            while True:
                headers = {}
                while True:
                    line = self.process.stdout.readline()
                    if not line:
                        return
                    if line == b'\r\n':
                        break
                    key, value = line.decode().split(':', 1)
                    headers[key.lower()] = value.strip()
                size = int(headers['content-length'])
                body = bytearray()
                while len(body) < size:
                    chunk = self.process.stdout.read(size - len(body))
                    if not chunk:
                        raise RuntimeError('Debug adapter closed an incomplete message')
                    body.extend(chunk)
                self.messages.put(json.loads(body))
        except Exception as error:
            self.messages.put(error)

    def send(self, command, arguments=None):
        self.sequence += 1
        body = json.dumps({'seq': self.sequence, 'type': 'request',
                           'command': command, 'arguments': arguments or {}}).encode()
        self.process.stdin.write(f'Content-Length: {len(body)}\r\n\r\n'.encode() + body)
        self.process.stdin.flush()
        return self.sequence

    def until(self, predicate):
        for index, message in enumerate(self.unmatched):
            if predicate(message):
                return self.unmatched.pop(index)
        deadline = time.monotonic() + 15
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError(f'Debug adapter timed out: {self.received}')
            message = self.messages.get(timeout=remaining)
            if isinstance(message, Exception):
                raise message
            self.received.append(message)
            if predicate(message):
                return message
            self.unmatched.append(message)

    def request(self, command, arguments=None):
        sequence = self.send(command, arguments)
        response = self.until(lambda message: message.get('request_seq') == sequence)
        assert response['success'], response
        return response

    def start(self, source):
        self.request('initialize', {'adapterID': 'dynlex'})
        self.request('launch', {'program': str(source), 'cwd': str(source.parent)})
        self.request('configurationDone')

    def close(self):
        try:
            self.request('disconnect')
            self.process.wait(timeout=5)
        finally:
            if self.process.poll() is None:
                self.process.kill()
                self.process.wait()
            self.reader.join(timeout=2)
            self.log = self.process.stderr.read().decode()
            self.process.stdin.close()
            self.process.stdout.close()
            self.process.stderr.close()
        assert self.process.returncode == 0, self.log

    def output(self, category):
        return ''.join(message['body']['output'] for message in self.received
                       if message.get('event') == 'output'
                       and message['body']['category'] == category)


def write_source(path, output):
    literal = json.dumps(output, ensure_ascii=False)
    path.write_text('@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", '
                    '@intrinsic("type", "int", 32), 1, "%s", ' + literal + '))\n')


def check_output(compiler, directory):
    # No newline, empty lines, protocol-like text, Unicode and multiple pipe buffers.
    expected = 'test\n\n~"console"\n@"target"\n^done\n(gdb)\n' + 'é🙂' * 12000 + 'end'
    source = directory / 'output.dl'
    write_source(source, expected)
    session = Session(compiler, directory)
    try:
        session.start(source)
        session.until(lambda message: message.get('event') == 'terminated')
        assert session.output('stdout') == expected, 'Program output was lost, changed or mixed with GDB messages'
        assert session.output('console') == '', session.output('console')
        assert session.output('stderr') == '', session.output('stderr')
    finally:
        session.close()
    assert not Path(str(source) + '.debug_bin').exists(), 'Debug executable was not removed'


def check_stop(compiler, directory):
    source = directory / 'stopped.dl'
    write_source(source, 'test')
    session = Session(compiler, directory)
    try:
        session.request('initialize', {'adapterID': 'dynlex'})
        session.request('launch', {'program': str(source), 'stopOnEntry': True})
        session.request('configurationDone')
        session.until(lambda message: message.get('event') == 'stopped')
        assert session.output('stdout') == '', 'Program ran past its entry breakpoint'
        session.request('next', {'threadId': 1})
        session.until(lambda message: message.get('event') in ('stopped', 'terminated'))
        if session.received[-1].get('event') != 'terminated':
            session.request('continue', {'threadId': 1})
            session.until(lambda message: message.get('event') == 'terminated')
        assert session.output('stdout') == 'test', session.received
        assert session.output('console') == '', session.output('console')
    finally:
        session.close()


def check_signal(compiler, directory):
    source = directory / 'signal.dl'
    source.write_text('@intrinsic("discard", @intrinsic("call", "libc", "raise", '
                      '@intrinsic("type", "int", 32), 11))\n')
    session = Session(compiler, directory)
    try:
        session.start(source)
        stopped = session.until(lambda message: message.get('event') == 'stopped')
        assert stopped['body']['reason'] == 'exception', stopped
        assert 'SIGSEGV' in session.output('stderr'), session.received
        assert session.output('console') == '', session.output('console')
    finally:
        session.close()


def check_pause(compiler, directory):
    source = directory / 'pause.dl'
    source.write_text('@intrinsic("discard", @intrinsic("call", "libc", "sleep", '
                      '@intrinsic("type", "int", 32), 3))\n')
    session = Session(compiler, directory)
    try:
        session.start(source)
        time.sleep(0.1)
        before = time.monotonic()
        session.request('pause', {'threadId': 1})
        stopped = session.until(lambda message: message.get('event') in ('stopped', 'terminated'))
        assert stopped.get('event') == 'stopped' and stopped['body']['reason'] == 'pause', stopped
        assert time.monotonic() - before < 2, 'Pause waited for the program to finish'
        assert session.output('stderr') == '', session.received
    finally:
        session.close()


def check_hover(compiler, directory):
    source = directory / 'hover.dl'
    source.write_text('function set variable to value:\n'
                      '    replacement:\n'
                      '        @intrinsic("store", variable, value)\n'
                      'set {test count} to @intrinsic("add", @intrinsic("command line argument count"), 1)\n'
                      '@intrinsic("discard", @intrinsic("variadic call", "libc", "printf", '
                      '@intrinsic("type", "int", 32), 1, "%d", test count))\n')
    session = Session(compiler, directory)
    try:
        initialized = session.request('initialize', {'adapterID': 'dynlex'})
        assert initialized['body']['supportsEvaluateForHovers']
        session.request('launch', {'program': str(source)})
        response = session.request('setBreakpoints', {'source': {'path': str(source)}, 'breakpoints': [{'line': 5}]})
        assert response['body']['breakpoints'][0]['verified'], response
        session.request('configurationDone')
        session.until(lambda message: message.get('event') == 'stopped')
        hovered = session.request('evaluate', {'expression': 'test count', 'frameId': 0, 'context': 'hover'})
        assert hovered['body']['result'] == '1', hovered
        scopes = session.request('scopes', {'frameId': 0})['body']['scopes']
        variables = session.request('variables', {'variablesReference': scopes[0]['variablesReference']})
        assert any(variable['name'] == 'test count' and variable['value'] == '1'
                   for variable in variables['body']['variables']), variables
        session.request('continue', {'threadId': 1})
        session.until(lambda message: message.get('event') == 'terminated')
        assert session.output('stdout') == '1', session.received
    finally:
        session.close()


def main():
    if os.name == 'nt':
        print('DAP output checks require the POSIX debugger backend.')
        return
    compiler = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='dynlex dap output ') as temporary:
        directory = Path(temporary)
        check_output(compiler, directory)
        check_stop(compiler, directory)
        check_signal(compiler, directory)
        check_hover(compiler, directory)
        check_pause(compiler, directory)
    print('Debug sessions preserve program output, hide GDB chatter, step, and report crashes.')


if __name__ == '__main__':
    main()

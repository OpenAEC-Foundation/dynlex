#!/usr/bin/env python3
"""Hover code spans preserve delimiters, source escapes and Unicode values."""
import json
from pathlib import Path

from lsp_tokens import LspSession, default_server_path, initialize_session, to_file_uri


def main():
    root = Path(__file__).resolve().parent.parent
    document = root / 'tests/lsp/hover_markup/main.dl'
    source = document.read_text()
    assert 'import ' not in source
    session = LspSession(default_server_path(root), root, False, False)
    try:
        initialize_session(session, root)
        uri = to_file_uri(document)
        session.notify('textDocument/didOpen', {'textDocument': {
            'uri': uri, 'languageId': 'dynlex', 'version': 1, 'text': source,
        }})
        for item in json.loads(document.with_name('expected.json').read_text()):
            result = session.request('textDocument/hover', {
                'textDocument': {'uri': uri},
                'position': {'line': item['line'], 'character': item['character']},
            })
            assert isinstance(result, dict), (item, result)
            assert result['contents'] == {'kind': 'markdown', 'value': item['value']}, result
    finally:
        session.close()
    print('Hover markup preserves literal backticks, quotes, escapes, Unicode and numeric values.')


if __name__ == '__main__':
    main()

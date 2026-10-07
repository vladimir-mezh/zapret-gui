"""Exercise the shipped executable through stdio, using isolated test settings.
Fixture winws.exe is an empty file for import validation; it is never executed.
"""
import json
import subprocess
import tempfile
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
exe = root / 'dist/mcp/ZapretMCP.exe'
scratch = (root / '.tools').resolve()
scratch.mkdir(exist_ok=True)

class Client:
    def __init__(self, config):
        self.process = subprocess.Popen([str(exe), '--config', str(config)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
        self.id = 0
    def send(self, method, params=None, notify=False):
        self.id += 1
        message = {'jsonrpc': '2.0', 'method': method}
        if not notify:
            message['id'] = self.id
        if params is not None:
            message['params'] = params
        self.process.stdin.write((json.dumps(message, ensure_ascii=False) + '\n').encode('utf-8'))
        self.process.stdin.flush()
        if notify:
            return
        result = json.loads(self.process.stdout.readline())
        assert result['id'] == self.id
        return result
    def initialize(self):
        result = self.send('initialize', {'protocolVersion': '2025-11-25', 'capabilities': {},
            'clientInfo': {'name': 'integration-test', 'version': '1'}})
        assert result['result']['protocolVersion'] == '2025-11-25'
        assert result['result']['serverInfo']['version'] == '0.2.0'
        self.send('notifications/initialized', notify=True)
        assert self.send('ping')['result'] == {}  # Wait until initialization notification was processed.
    def call(self, name, arguments=None):
        return self.send('tools/call', {'name': name, 'arguments': arguments or {}})['result']
    def close(self):
        self.process.stdin.close()
        assert self.process.wait(timeout=10) == 0
        assert not self.process.stderr.read(), 'unexpected server stderr'

with tempfile.TemporaryDirectory(prefix='mcp-test-', dir=scratch) as temp:
    base = Path(temp).resolve()
    assert base.is_relative_to(scratch), 'cleanup target must stay under test workspace'
    config = base / 'settings.json'
    client = Client(config)
    def connections():
        return int(subprocess.check_output([str(root / 'dist/manager_tests.exe'), '--connections', str(base / 'mcp')],
            creationflags=subprocess.CREATE_NO_WINDOW))
    try:
        assert client.send('tools/list')['error']['code'] == -32000
        assert client.send('server/discover')['error']['code'] == -32601
        client.initialize()
        assert (base / 'mcp/sessions' / f'{client.process.pid}.json').is_file()
        assert connections() == 1
        available = client.send('tools/list')['result']['tools']
        assert len(available) == 4
        assert all(tool['inputSchema']['additionalProperties'] is False for tool in available)
        state = client.call('zapret_get_state')['structuredContent']
        assert state['revision'] == 0 and state['available_versions'] == []
        preview = client.call('zapret_set_profile', {'changes': {'game_filter': True}, 'expected_revision': 0})
        assert preview['structuredContent']['dry_run'] is True
        assert client.call('zapret_get_state')['structuredContent']['profile']['game_filter'] is False
        update = client.call('zapret_set_profile', {'changes': {'game_filter': True}, 'expected_revision': 0, 'dry_run': False})
        assert update['structuredContent']['revision'] == 1
        assert json.loads(config.read_text())['profile']['game_filter'] is True
        for arguments in [
            {'changes': {'command': 'cmd.exe'}, 'expected_revision': 1, 'dry_run': False},
            {'changes': {'game_filter': 'yes'}, 'expected_revision': 1},
            {'changes': {'game_filter': False}, 'expected_revision': -1},
            {'changes': {'game_filter': False}, 'expected_revision': True},
            {'changes': {'game_filter': False}, 'expected_revision': 0, 'dry_run': False},
            {'changes': {'strategy': 'missing.bat'}, 'expected_revision': 1},
        ]:
            assert client.call('zapret_set_profile', arguments)['isError'] is True
        assert client.call('zapret_get_state')['structuredContent']['revision'] == 1
        installation = base / 'тестовая версия'
        (installation / 'bin').mkdir(parents=True)
        (installation / 'lists').mkdir()
        (installation / 'bin/winws.exe').write_bytes(b'')
        (installation / 'general.bat').write_text('@echo off\n', encoding='utf-8')
        imported = client.call('zapret_import_version', {'path': str(installation), 'expected_revision': 1})
        state = imported['structuredContent']
        assert state['revision'] == 2 and state['available_versions'][0]['strategies'] == ['general.bat']
        assert str(base) not in json.dumps(state, ensure_ascii=False)
        assert client.call('zapret_import_version', {'path': str(installation), 'expected_revision': 2})['structuredContent']['revision'] == 2
        assert client.call('zapret_check_files')['structuredContent']['checks'][0]['winws_exists'] is True
        second = Client(config)
        try:
            second.initialize()
            assert connections() == 2
            result = second.call('zapret_set_profile', {'changes': {'ipset_mode': 'none'}, 'expected_revision': 2, 'dry_run': False})
            assert result['structuredContent']['revision'] == 3
            assert client.call('zapret_set_profile', {'changes': {'game_filter': False}, 'expected_revision': 2, 'dry_run': False})['isError'] is True
            assert client.call('zapret_get_state')['structuredContent']['profile']['ipset_mode'] == 'none'
        finally:
            second.close()
        assert connections() == 1
        # A corrupt configuration must be preserved, rather than overwritten.
        config.write_text('{invalid', encoding='utf-8')
        assert client.call('zapret_get_state')['isError'] is True
        assert config.read_text() == '{invalid'
    finally:
        client.close()
        assert not (base / 'mcp/sessions' / f'{client.process.pid}.json').exists()
        assert connections() == 0

generated = subprocess.check_output([str(exe), '--client-config'], creationflags=subprocess.CREATE_NO_WINDOW)
assert json.loads(generated)['mcpServers']['zapret-gui']['command'] == str(exe)
with tempfile.TemporaryDirectory(prefix='mcp-launcher-', dir=scratch) as temp:
    base = Path(temp).resolve()
    assert base.is_relative_to(scratch)
    package = base / 'mcp'
    version = package / 'versions/0.2.0'
    version.mkdir(parents=True)
    shutil.copyfile(exe, version / 'ZapretMCP.exe')
    shutil.copyfile(root / 'dist/mcp/ZapretMcpLauncher.exe', package / 'ZapretMcpLauncher.exe')
    (package / 'manager.json').write_text(json.dumps({'active_version': '0.2.0'}))
    exe = package / 'ZapretMcpLauncher.exe'
    client = Client(base / 'settings.json')
    try:
        client.initialize()
        assert client.call('zapret_get_state')['structuredContent']['revision'] == 0
        result = client.call('zapret_set_profile', {'changes': {'game_filter': True}, 'expected_revision': 0, 'dry_run': False})
        assert result['structuredContent']['revision'] == 1
    finally:
        client.close()
    (package / 'manager.json').write_text(json.dumps({'active_version': '../escape'}))
    assert subprocess.run([str(exe)], creationflags=subprocess.CREATE_NO_WINDOW).returncode != 0
print('PASS: stable launcher stdio, profile persistence, exit and invalid version rejection')
print('PASS: stdio lifecycle, tools, preview/commit, persistence, import, UTF-8, two-client conflicts, corrupt-file preservation')

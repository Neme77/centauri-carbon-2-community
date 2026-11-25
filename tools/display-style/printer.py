"""Manual macOS/Linux SSH launcher; no password storage or automatic reboot."""
from pathlib import Path
import argparse
import datetime
import hashlib
import ipaddress
import json
import re
import shlex
import subprocess
import uuid

HOME = Path(__file__).resolve().parent
BUNDLE = '034f420bfff43632367d2b5ca7cc919f730486675f9bd7bc8611ecc87d19f697'
INSTALLER = 'de1eff32d25c7739d6e77d378910ad30447b12c1c2be6ba1acefe110f4597d16'


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def save_record(path, record):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def command(program, arguments, log):
    # SSH keeps its native password/host-key prompts on the terminal; secrets
    # never pass through Python or the report.
    with log.open('a', encoding='utf-8') as report:
        process = subprocess.Popen([program, *arguments], stdout=subprocess.PIPE,
                                   stderr=None, text=True, errors='replace')
        for line in process.stdout:
            print(line, end='', flush=True)
            report.write(line)
        code = process.wait()
        report.write('REMOTE_COMMAND_EXIT=' + str(code) + '\n')
    require(code == 0, 'Command failed; retain report and stage. Do not retry automatically.')


def run(action, home=HOME):
    archive = home / 'display-kit.tar.gz'
    require(hashlib.sha256(archive.read_bytes()).hexdigest() == BUNDLE,
            'Kit archive differs; preserve files and stop.')
    log = home / (action + '-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '.log')
    receipt = home / 'printer-stage.json'
    print('REPORT: ' + str(log), flush=True)
    prior = json.loads(receipt.read_text(encoding='utf-8-sig')) if receipt.exists() else None
    if action == 'Check':
        host = str(ipaddress.IPv4Address(input('Printer IPv4: ').strip()))
        if prior:
            require(prior['ip'] == host and prior['bundle_sha256'] == BUNDLE,
                    'Stage belongs to another printer or kit; retain the record.')
            stage = prior['stage']
        else:
            stage = '/opt/usr/cc2-stock-style-' + uuid.uuid4().hex
            remote_archive = stage + '.tar.gz'
            command('scp', ['-O', str(archive), 'root@' + host + ':' + remote_archive], log)
            prior = dict(ip=host, stage=stage, bundle_sha256=BUNDLE, read_only_passed=False)
            save_record(receipt, prior)
            bootstrap = ("set -eu; umask 077; [ ! -L '{a}' ]; chmod 600 '{a}'; "
                         "printf '%s  %s\\n' '{h}' '{a}' | sha256sum -c -; "
                         "[ ! -e '{s}' ] && [ ! -L '{s}' ]; mkdir -m 700 '{s}'; "
                         "tar -xzf '{a}' -C '{s}'; printf 'STAGE=%s\\n' '{s}'").format(
                             a=remote_archive, h=BUNDLE, s=stage)
            command('ssh', ['root@' + host, bootstrap], log)
    else:
        require(prior and prior.get('read_only_passed') is True and prior.get('bundle_sha256') == BUNDLE,
                'Run Check first; verified admission is missing.')
        host = str(ipaddress.IPv4Address(prior['ip']))
        stage = prior['stage']
    require(re.fullmatch(r'/opt/usr/cc2-stock-style-[0-9a-f]{32}', stage), 'Invalid stage record.')
    mode = dict(Check='--check', Install='--apply', Restore='--restore', Status='--status')[action]
    remote = ("set -eu; [ ! -L '{s}' ] && [ -d '{s}' ] && [ ! -L '{s}/install.sh' ]; "
              "printf '%s  %s\\n' '{h}' '{s}/install.sh' | sha256sum -c -; "
              "sh '{s}/install.sh' '{m}'").format(s=stage, h=INSTALLER, m=mode)
    print('Private staging only.' if action == 'Check' else
          'Cold idle printer, zero heater targets. No automatic reboot.', flush=True)
    command('ssh', ['root@' + host, remote], log)
    if action == 'Check':
        prior['read_only_passed'] = True
        save_record(receipt, prior)
        print('VERIFIED_STAGE=' + stage)
        print('STOCK_STYLE_DISPLAY_READ_ONLY_PASSED. Review report before Install.')
    elif action in ('Install', 'Restore'):
        print(action.upper() + '_COMPLETE. Review report before one manual cold-idle reboot.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['Check', 'Install', 'Restore', 'Status'])
    try:
        run(parser.parse_args().action)
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        parser.exit(125, 'STOP: ' + str(error) + '\nKeep kit, backup and reports.\n')

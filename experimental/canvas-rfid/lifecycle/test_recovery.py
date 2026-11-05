"""Actual boot recovery CLI; dummy native process, PTY and RAM Canvas only.

The read-only stock input is data for the protocol simulator, never executed.
No printer path or real serial port is used.
"""
from pathlib import Path
import errno
import fcntl
import hashlib
import json
import os
import pty
import select
import signal
import struct
import subprocess
import tempfile
import threading
import time

HERE = Path(__file__).resolve().parent
STOCK = Path('/stock-canvas.bin').read_bytes()
assert len(STOCK) == 51472
assert hashlib.sha256(STOCK).hexdigest() == 'f1267ee3219dcad73efffa8dc38e30ff0f17c02c0a1674cbead48a99bdff3228'
CASES = ['check', 'recover-filter', 'recover-stock', 'from-application',
         'bad-crc', 'bad-offset', 'lost-erase-ack', 'cancel', 'commit-failed',
         'missing-journal', 'corrupt-journal', 'public-journal', 'backup-bad',
         'backup-symlink', 'profile-changed', 'profile-missing', 'guard-changed',
         'role-duplicate', 'role-missing', 'public-base', 'locked',
         'native-live', 'port-owned', 'active-not-recover', 'no-challenge', 'boot-policies', 'orphan-journal']
sha = lambda data: hashlib.sha256(data).hexdigest()

def seed(folder, phase, profile):
    payload = bytearray(128)
    magic = b'CC2-CANVAS-LC1'
    payload[:len(magic)] = magic
    payload[16] = phase
    struct.pack_into('<Q', payload, 24, 7)
    payload[32:48] = bytes(range(1, 17))
    # Simulate an earlier Linux boot: recovery must not depend on its monotonic
    # timestamp, PID, capability record or a running native/MQTT controller.
    payload[48:84] = b'00000000-0000-0000-0000-000000000001'
    payload[84:116] = hashlib.sha256(profile).digest()
    path = folder / 'lifecycle'
    path.write_bytes(payload + sha(payload).encode())
    path.chmod(0o600)

def crc(data):
    value = 0xffff
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0x8408 if value & 1 else 0)
    return value

def packet(command, data):
    return b'\xa5\x5a' + bytes([command]) + len(data).to_bytes(2, 'big') + data + crc(data).to_bytes(2, 'big')

def run():
    count = 0
    with tempfile.TemporaryDirectory(prefix='cc2-boot-recover-') as temporary:
        root = Path(temporary)
        dummy = root / 'elegoo_printer'
        (root / 'dummy.c').write_text('#include <unistd.h>\nint main(void){for(;;)pause();}\n')
        subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', root / 'dummy.c', '-o', dummy], check=True)
        flags = ['-O2', '-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                 '-UNDEBUG', '-D_POSIX_C_SOURCE=200809L', '-I' + str(HERE.parent / 'host')]
        for arch, compiler, extra, runner in [
            ('host', 'gcc', [], []),
            ('arm', 'arm-linux-gnueabihf-gcc', ['-static'], ['qemu-arm']),
        ]:
            for case in CASES:
                folder = root / f'{arch}-{case}'
                folder.mkdir(mode=0o700)
                component = folder / 'component'
                component.mkdir(mode=0o700)
                backup = component / 'original-stock-44.bin'
                backup.write_bytes(STOCK)
                backup.chmod(0o600)
                master, slave = pty.openpty()
                device = os.ttyname(slave)
                if case != 'port-owned':
                    os.close(slave)
                    slave = -1
                binary = folder / 'recover'
                fault_sources = []
                if case == 'commit-failed':
                    fault = folder/'commit-fault.c'
                    fault.write_text('#include <errno.h>\n#include <string.h>\nint __real_renameat(int,const char*,int,const char*);\nint __wrap_renameat(int a,const char*b,int c,const char*d){if(!strcmp(d,"lifecycle")){errno=EIO;return -1;}return __real_renameat(a,b,c,d);}\n')
                    fault_sources = [fault, '-Wl,--wrap=renameat']
                subprocess.run([compiler, *flags, *extra,
                                '-DCL_BASE=' + json.dumps(str(component)),
                                '-DCL_TTY=' + json.dumps(device),
                                HERE / 'recover_worker.c', *fault_sources, '-o', binary], check=True)
                roles = []
                for role in ['bootstrap', 'native', 'guard', 'entry', 'recover', 'marker']:
                    target = binary if role == 'recover' else folder / role
                    if target != binary:
                        target.write_bytes(('FAKE ROLE: ' + role).encode())
                        target.chmod(0o600)
                    roles.append(f'{role} {sha(target.read_bytes())} {target}\n')
                if case == 'role-duplicate':
                    roles.append(roles[0])
                if case == 'role-missing':
                    roles.pop(2)
                profile = ''.join(roles).encode()
                profile_path = component / 'recovery-profile'
                profile_path.write_bytes(profile)
                profile_path.chmod(0o600)
                phase = 6 if case == 'recover-stock' else 5 if case == 'active-not-recover' else 3
                seed(component, phase, profile)
                journal = component / 'lifecycle'
                sentinel = folder / 'outside'
                sentinel.write_bytes(b'KEEP')
                sentinel.chmod(0o600)
                if case == 'missing-journal': journal.unlink()
                if case == 'corrupt-journal':
                    content = bytearray(journal.read_bytes()); content[32] ^= 1; journal.write_bytes(content)
                if case == 'public-journal': journal.chmod(0o644)
                if case == 'backup-bad': backup.write_bytes(STOCK[:-4] + b'BAD!')
                if case == 'backup-symlink': backup.unlink(); backup.symlink_to(sentinel)
                if case == 'profile-changed': profile_path.write_bytes(profile + b'changed\n')
                if case == 'profile-missing': profile_path.unlink()
                if case == 'guard-changed': (folder / 'guard').write_bytes(b'changed')
                if case == 'public-base': component.chmod(0o755)
                if case in ['commit-failed','orphan-journal']:
                    (component / 'lifecycle.next').write_bytes(b'KEEP-PENDING')
                held = None
                if case == 'locked':
                    held = os.open(component / 'lifecycle.lock', os.O_RDWR | os.O_CREAT, 0o600)
                    fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
                native = subprocess.Popen([str(dummy)]) if case == 'native-live' else None
                frames, jumps, errors = [], [], []
                memory = bytearray(b'\x69' * 0x4000 + b'\xff' * 53248)
                stop = threading.Event()
                active = None

                def peer():
                    data = b''
                    entered = case != 'from-application'
                    offset = 0
                    end = time.monotonic() + 25
                    magic = b' \x1c Request Serial Bootloader!! ~'
                    while not stop.is_set() and time.monotonic() < end:
                        if not select.select([master], [], [], .02)[0]: continue
                        try: chunk = os.read(master, 4096)
                        except OSError as error:
                            if error.errno == errno.EIO:
                                time.sleep(.01); continue
                            errors.append(str(error)); return
                        data += chunk
                        while data:
                            if data[:1] == magic[:1]:
                                if len(data) < len(magic): break
                                if not data.startswith(magic): errors.append('bad entry bytes'); return
                                data = data[len(magic):]; entered = True; continue
                            if len(data) < 5: break
                            if data[:2] != b'\xa5\x5a': errors.append('bad header'); return
                            n = int.from_bytes(data[3:5], 'big')
                            if n > 1036: errors.append('out of bounds'); return
                            if len(data) < n + 7: break
                            frame, data = data[:n+7], data[n+7:]
                            command, payload = frame[2], frame[5:5+n]
                            if crc(payload) != int.from_bytes(frame[-2:], 'big'):
                                errors.append('request CRC'); return
                            frames.append((command, payload))
                            if command == 3:
                                if entered and case != 'no-challenge': os.write(master, packet(3, payload))
                            elif command == 0:
                                if payload != struct.pack('<I', len(STOCK)): errors.append('erase bounds'); return
                                offset = 0; memory[0x4000:] = b'\xff' * 53248
                                if case != 'lost-erase-ack': os.write(master, packet(0, struct.pack('<I', 53248)))
                            elif command == 1:
                                off, length, total = struct.unpack_from('<III', payload)
                                if off != offset or total != len(STOCK) or not 4 <= length <= 1024 or length % 4 or n != 12+length or off+length > len(STOCK):
                                    errors.append('write bounds'); return
                                memory[0x4000+off:0x4000+off+length] = payload[12:]
                                offset += length
                                ack = struct.pack('<II', length, offset-4 if case == 'bad-offset' and offset == 10240 else offset)
                                reply = packet(1, ack)
                                if case == 'bad-crc' and offset == 10240: reply = reply[:-1] + bytes([reply[-1] ^ 1])
                                if case == 'cancel' and offset == 10240:
                                    while active is None: time.sleep(.001)
                                    os.kill(active.pid, signal.SIGTERM)
                                os.write(master, reply)
                            elif command == 2:
                                if offset != len(STOCK): errors.append('jump incomplete'); return
                                jumps.append(bytes(memory[0x4000:0x4000+len(STOCK)]))
                            else: errors.append('unknown command'); return

                thread = threading.Thread(target=peer, daemon=True)
                thread.start()
                try:
                    if case == 'boot-policies':
                        for p in range(11):
                            seed(component, p, profile)
                            result = subprocess.run([*runner, binary, '--boot-policy'], capture_output=True, text=True, timeout=5)
                            expected = 20 if p in [3, 6] else 10 if p in [1, 2, 4, 5, 7] else 0
                            assert result.returncode == expected, (arch, case, p, result.stdout, result.stderr)
                        assert not frames
                    else:
                        action = '--check' if case == 'check' else '--recover-original'
                        active = subprocess.Popen([*runner, str(binary), action], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                        stdout, stderr = active.communicate(timeout=20)
                        success = case in ['check', 'recover-filter', 'recover-stock', 'from-application', 'orphan-journal']
                        assert (active.returncode == 0) == success, (arch, case, active.returncode, stdout, stderr, errors)
                        if case in ['recover-filter', 'recover-stock', 'from-application', 'orphan-journal']:
                            assert jumps == [STOCK], (arch, case, stdout, stderr)
                            assert journal.read_bytes()[16] == 7
                            assert journal.read_bytes()[48:84].decode() == Path('/proc/sys/kernel/random/boot_id').read_text().strip()
                        else:
                            assert not jumps
                        if case not in ['recover-filter', 'recover-stock', 'from-application', 'bad-crc', 'bad-offset', 'lost-erase-ack', 'cancel', 'commit-failed', 'no-challenge', 'orphan-journal']:
                            assert not frames, (arch, case, frames)
                        if case in ['bad-crc', 'bad-offset', 'lost-erase-ack', 'cancel', 'commit-failed', 'no-challenge']:
                            assert journal.read_bytes()[16] == 3
                        if case in ['commit-failed','orphan-journal']: assert (component / 'lifecycle.next').read_bytes() == b'KEEP-PENDING'
                    assert not errors, (arch, case, errors)
                    assert memory[:0x4000] == b'\x69' * 0x4000
                    assert sentinel.read_bytes() == b'KEEP'
                    if native: assert native.poll() is None, 'worker must not stop a native process'
                    print('PASS:', arch, case, 'actual recovery CLI; dummy process/PTY/RAM only', flush=True)
                    count += 1
                finally:
                    if active and active.poll() is None: active.kill(); active.wait(timeout=3)
                    stop.set(); thread.join(timeout=2); os.close(master)
                    if slave >= 0: os.close(slave)
                    if held is not None: os.close(held)
                    if native: native.kill(); native.wait(timeout=3)
    assert count == 2 * len(CASES)
    print(f'PASS: {count} host/ARM actual boot-recovery cases; no native process stopped, real UART or printer accessed.', flush=True)
    return count

if __name__ == '__main__': run()

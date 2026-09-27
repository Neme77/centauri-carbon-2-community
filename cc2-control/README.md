# CC2 Control

CC2 Control is the local control service and web interface included with the
Centauri Carbon 2 Community Firmware.

## Directories

- `src/`: C backend and Panda compatibility bridge;
- `web/`: browser interface;
- `scripts/`: procd startup and runtime scripts;
- `config/`: example configuration;
- `defaults/`: default material presets;
- `tests/`: native, integration and static UI tests;
- `docs/`: protocol and API notes.

## Native development build

```sh
make clean all CROSS= CC=gcc
python3 -m unittest discover -s tests -p 'test_*.py'
```

The native build is for host-side validation. Use the ARM cross toolchain for a
printer executable.

## Runtime paths

```text
/opt/inst/cc2-control     immutable application files
/opt/usr/cc2-control      persistent settings and material presets
/etc/init.d/cc2-control   procd service definition
```

The LAN access code is local printer configuration and must not be committed.

If the code is changed on the printer, open **Settings → Connection**, enter the
new value, and select **Change / Revalidate**. CC2 Control stores the replacement
atomically and restarts only its own service; printer services and active print
state are left untouched.

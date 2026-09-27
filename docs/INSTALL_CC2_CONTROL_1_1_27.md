# Installing CC2 Control 1.1.27

This updater is intended for printers already running **Community Firmware V4.1** and updates CC2 Control without reflashing the full firmware.

Package:

```text
CC2-Control-1.1.27-Multiplatform-Update.zip
```

SHA-256:

```text
17012fc53eaca3bd3ab1a1829172c9d12e8ed56dcf4b135d9e17a8acfc6fdccc
```

## Requirements

- printer and computer on the same local network
- printer IP address
- SSH access
- printer root password
- `ssh` and `scp`

## Windows 10 / 11

Extract the ZIP and open PowerShell in that directory.

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\Install-CC2-Control-1.1.27.ps1
```

Or specify the printer IP:

```powershell
.\Install-CC2-Control-1.1.27.ps1 -PrinterIp 192.168.1.103
```

On first SSH connection, accept the host key when prompted. The password is not shown while typing.

## Linux / macOS

```sh
chmod +x install-cc2-control-1.1.27.sh
./install-cc2-control-1.1.27.sh
```

Or:

```sh
./install-cc2-control-1.1.27.sh 192.168.1.103
```

## Rollback

The updater preserves the previous persistent installation in:

```text
/opt/usr/cc2-control-rollback-1.1.26
```

Existing LAN code, material presets and UI preferences are preserved.

## Mandatory power cycle

> [!IMPORTANT]
> After the updater completes successfully, **do not use the printer immediately**.
>
> **Switch the printer completely off, then power it on again before doing anything else.**
>
> This full power cycle is required to correctly realign **Canvas** and the related background services.

After powering it back on, wait approximately **30–60 seconds** before normal use.

## Verify

Open:

```text
http://PRINTER-IP:8081
```

or over SSH:

```sh
wget -qO- http://127.0.0.1:8081/api/health
```

The reported version must be:

```text
1.1.27
```

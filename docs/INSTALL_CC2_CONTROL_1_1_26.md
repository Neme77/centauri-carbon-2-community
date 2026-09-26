# Installing CC2 Control 1.1.26

CC2 Control **1.1.26** is a targeted dashboard and workflow hotfix for **Community Firmware V4.1**.

It does **not** require reflashing the printer firmware. The updater replaces only CC2 Control and its web interface, preserves the persistent user configuration, verifies the new service, and automatically restores the previous installation if the health check fails.

## Download

Release package:

```text
CC2-Control-1.1.26-Multiplatform-Hotfix.zip
```

SHA-256:

```text
3b73afaed88752d42240442e1ed3bcd9417e95583eaa77878548a8de2b93c15e
```

## Before you start

- The printer must already be running **Community Firmware V4.1**.
- The printer and computer must be connected to the same local network.
- Note the printer IP address, for example `192.168.1.103`.
- Extract the ZIP archive on the computer before running the installer.
- The installer requires `ssh` and `scp`.
- The default printer root password is:

```text
MTY4ODE2
```

If you previously changed the root password, use your own password instead.

> [!NOTE]
> The password is normally requested **twice**: once while the payload is copied to the printer and once when the remote installation starts.

## Windows 10 / 11

### 1. Extract the ZIP

Right-click `CC2-Control-1.1.26-Multiplatform-Hotfix.zip` and choose **Extract All**.

Open the extracted folder.

### 2. Open PowerShell in that folder

An easy method is to click the folder address bar, type:

```text
powershell
```

and press Enter.

### 3. Start the installer

Run:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\Install-CC2-Control-1.1.26.ps1
```

The installer asks for the printer IP address.

You can also supply it directly:

```powershell
.\Install-CC2-Control-1.1.26.ps1 -PrinterIp 192.168.1.103
```

Replace `192.168.1.103` with the actual printer IP.

### 4. SSH confirmation

On the first connection, Windows may ask whether you trust the printer SSH host key.

Type:

```text
yes
```

and press Enter.

When prompted for the root password, enter:

```text
MTY4ODE2
```

The password is not displayed while typing. This is normal.

### 5. Wait for completion

A successful installation ends with a message similar to:

```text
CC2 Control 1.1.26 installed: http://PRINTER-IP:8081
```

Open that address in a browser and verify that CC2 Control reports version **1.1.26**.

### Windows: ssh or scp not found

Enable the Windows **OpenSSH Client** optional feature, then reopen PowerShell and run the installer again.

## Linux

Extract the archive, open a terminal in the extracted directory, and run:

```sh
chmod +x install-cc2-control-1.1.26.sh
./install-cc2-control-1.1.26.sh
```

Enter the printer IP when requested.

Or provide it directly:

```sh
./install-cc2-control-1.1.26.sh 192.168.1.103
```

If the computer does not have `ssh` / `scp`, install the OpenSSH client package supplied by your Linux distribution.

On the first SSH connection, confirm the host key with `yes`, then enter the printer root password.

Default root password:

```text
MTY4ODE2
```

## macOS

The same shell installer is used on macOS.

Extract the ZIP, open **Terminal**, move to the extracted directory, then run:

```sh
chmod +x install-cc2-control-1.1.26.sh
./install-cc2-control-1.1.26.sh
```

Or:

```sh
./install-cc2-control-1.1.26.sh 192.168.1.103
```

Accept the SSH host key if asked, then enter the printer root password.

Default root password:

```text
MTY4ODE2
```

## What the updater changes

The updater installs:

- the CC2 Control 1.1.26 executable;
- the updated CC2 Control web dashboard;
- the service launch scripts;
- the CC2 Control init script.

It preserves the existing persistent files when present:

- `cc2-control.conf`
- `material-presets.json`
- `ui-preferences.json`

This preserves the LAN code, material presets and UI preferences.

## Automatic backup and rollback

Before replacing the current installation, the updater creates:

```text
/opt/usr/cc2-control-rollback-1.1.25
```

After installation it starts CC2 Control and checks:

```text
http://127.0.0.1:8081/api/health
```

The health check must report version **1.1.26**. The updater waits for up to approximately 90 seconds.

If the health check does not pass, the updater automatically restores the previous installation and restarts CC2 Control.

## Verify the installation

Open:

```text
http://PRINTER-IP:8081
```

You can also verify over SSH:

```sh
wget -qO- http://127.0.0.1:8081/api/health
```

The response should include:

```json
"version":"1.1.26"
```

## Important

This package is an update for **CC2 Control**, not a complete printer firmware image. Do not copy this ZIP to the printer USB update drive and do not try to install it from the ELEGOO firmware update screen.

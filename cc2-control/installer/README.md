# CC2 Control updater launchers

Place these launchers beside cc2-control-1.1.31-payload.tar.gz from the matching release. The payload is not stored in Git.

Windows:
    .\Install-CC2-Control.ps1 -PrinterIp 192.0.2.10

For an expected SSH host-key change:
    .\Install-CC2-Control.ps1 -PrinterIp 192.0.2.10 -ResetHostKey

Linux/macOS:
    sh ./install-cc2-control.sh 192.0.2.10

For an expected SSH host-key change:
    sh ./install-cc2-control.sh --reset-host-key 192.0.2.10

Recovery requires typing the exact printer address. It removes only that address from the default user known_hosts file with ssh-keygen -R. OpenSSH then performs normal verification: verify the new fingerprint through a trusted channel before accepting it. Printer keys are not regenerated.

Custom known_hosts files and HostKeyAlias settings require manual recovery.

Packaging: include both launchers, this README and the matching release payload in the updater ZIP. These sources replace the launchers in the external release builder; they do not build or modify the payload. Do not mix releases.

Tests from the repository root:
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File cc2-control/tests/test_installer_ssh.ps1
    python3 cc2-control/tests/test_installer_ssh.py

Tests simulate SSH commands and never connect to a printer.

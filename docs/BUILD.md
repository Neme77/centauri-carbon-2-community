# Builder e avviatori

I sorgenti conservano il builder v3.7 collaudato. Questa esportazione non include immagini firmware, chiavi private/AES, toolchain compilata o binari SSH/daemon. Non basta quindi clonarla per costruire un firmware.

Per l'ambiente completo del manutentore, mantenere `C:\CC2_BUILD` con:

- `cc2_builder_v3_7/` completo, inclusi `components/ssh/sshd`, `dualtrust/dual_verify.bin` e gli altri componenti del bundle v3.7;
- `original_firmware/cc2_eeb001_02.01.00.00_20260707170825.zip.sig`;
- `keys/cc2_aes_key_v1.bin`;
- `keys/cc2_stock_private.pem` oppure `keys/cc2_community_release_private.pem`;
- `tools/squashfs-tools-4.6.1/bin/mksquashfs` e `unsquashfs`;
- i due `.ps1` e `launch_helpers/` alla radice.

Le chiavi di firma sono del manutentore. Un utente finale installa un pacchetto firmato e non deve ricevere le chiavi private. Una nuova coppia community non è automaticamente compatibile con il Dual Trust già distribuito.

Prerequisiti: Ubuntu/WSL, Python 3, OpenSSL, GNU cpio, SquashFS Tools 4.6.1 con gli hash previsti. Il builder usa root per ripristinare i proprietari dei file. Il launcher verifica anche l'hash del sorgente v3.7: eventuali modifiche richiedono una nuova versione coordinata.

```powershell
cd C:\CC2_BUILD
.\build_stock.ps1 -CheckKey
.\build_community.ps1 -CheckKey
.\build_stock.ps1 -Preflight
.\build_stock.ps1
# Per un successivo pacchetto community:
.\build_community.ps1
```

Output in `output/`, log in `logs/`. I comandi non installano sulla stampante. La parte Python dei launcher è stata verificata; il passaggio PowerShell/WSL dei nuovi avviatori non ha ancora un esito riferito dal manutentore. La build completa v3.7 via comando WSL esplicito è invece riuscita.

Non caricare la propria cartella CC2_BUILD su GitHub: contiene segreti e materiale non destinato al repository.

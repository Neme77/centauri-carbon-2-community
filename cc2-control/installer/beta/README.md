# CC2 Control — Beta @BUILD_ID@, test combinato PR 46 e 51–56

Aggiornamento di CC2 Control su Community Firmware con CC2 Control già installato.
La versione API resta 1.1.31; `beta-build.json` identifica esattamente sorgenti,
PR, compilatore e hash. Questa revisione include il fix della precisione Z e la PR56. Upload, web,
Canvas, esclusione oggetti e viti sono gia stati provati nella beta precedente;
restano da verificare Z offset aggiornato, calibrazione A/B e singola mesh adattiva.
Il refresh automatico della mesh e la coerenza 2D/3D restano da sistemare: questo
pacchetto non dichiara quel problema risolto.

## Installazione

Estrarre tutto lo ZIP. Con stampante connessa e **Idle**, su Windows avviare
`Install-Windows.cmd`. Oppure PowerShell dalla cartella estratta:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install-CC2-Control.ps1 -PrinterIp 192.168.1.103
```

Linux/macOS:

```sh
sh ./install-cc2-control.sh 192.168.1.103
```

Usare l'IP della propria stampante. Serve OpenSSH e la password SSH root.
I launcher includono il recupero esplicito della chiave SSH solo se richiesto
con `-ResetHostKey` o `--reset-host-key`; verificare la fingerprint.

L'installer verifica i checksum, richiede stato Idle recente e registrazione
MQTT, crea un backup completo e aggiorna insieme binario, dashboard, quattro
lingue e script di avvio. Conserva configurazione LAN, preset e preferenze.
Verifica l'hash del processo avviato e tenta il rollback se l'installazione
fallisce dopo l'arresto del servizio. Conservare l'output del terminale.

Dopo `BETA INSTALLATION PASSED`, aprire `http://IP:8081` e ricaricare tutti i
client con Ctrl+F5. Non occorre riavviare la stampante.

## Ripristino

Il terminale mostra il percorso effettivo del backup e il comando da eseguire
via SSH, a stampante Idle:

```sh
sh /opt/usr/cc2-control-backup-beta-DATA-ORA-PID/restore.sh
```

Sostituire il percorso con quello stampato dall'installer. Il ripristino
conserva le preferenze/configurazioni correnti e mantiene l'installazione
sostituita in una directory separata. Se l'API non è raggiungibile, conservare
il backup e contattare il manutentore per il ripristino assistito.

## Test delle PR

1. **46 — Upload:** file da 70–120 MiB, da file manager e Orca; oltre 128 MiB
   rifiutato. Ripetere upload-only durante stampa: nessun avvio del secondo
   file e nessuna sovrascrittura. Verificare anche le miniature.
2. **51 — UI:** copia console da HTTP, errore upload senza notifiche ripetute,
   campi temperatura vuoti rifiutati, selezioni file aggiornate, Canvas,
   grafici e comandi protetti durante stampa. Controllare desktop con barra
   aperta/chiusa, smartphone portrait e tablet.
3. **52 — HTTP:** dashboard LAN e Orca continuano a funzionare; se usato,
   verificare anche il proprio accesso remoto/DNS. CLI di sola lettura invariata.
   Le richieste di modifica da CLI richiedono `X-CC2-Request: 1`.
4. **53 — Misure:** scegliere il lato A/B montato prima della calibrazione;
   verificare risultati dello screw leveling solo per la procedura appena
   conclusa, anche dopo interruzione. La selezione mesh per la visualizzazione
   è distinta dalla calibrazione e non cambia la mesh applicata.
5. **54 — Z offset:** annotare il riferimento reale ricevuto dalla stampante;
   fare passi ±0,01/0,02 mm, verificare valore visibile e readback, ricaricare la pagina e premere
   Annulla. Deve tornare al riferimento originale, anche se diverso da zero.
   Verificare da un secondo client. Non scrive SAVE_CONFIG. I passi con MOVE=1
   muovono Z: osservare il movimento e mantenere adeguata distanza dal piatto.
6. **55 — Esclusione:** durante una stampa multioggetto, provare nomi UTF-8 e
   parentesi; deve essere escluso solo l'oggetto scelto.
7. **56 — Avvio adattivo:** con lo stesso G-code Benchy verificare una sola
   calibrazione 4×4 entro i limiti dell'oggetto, senza 11×11 preliminare.
   Verificare anche avvio con mesh salvata A/B e calibrazione completa esplicita.
8. **Mesh A/B e adattiva:** visualizzare A/B, confrontare i valori con il
   firmware. ADAPTIVE è disponibile solo se esposta dalla stampante. Un profilo
   salvato non dimostra che appartenga al modello corrente o che sarà
   riutilizzato in una ristampa. Non vengono caricate o salvate mesh in automatico.
9. **Regressioni:** velocità/flow e reset 100%, Pause/Resume, webcam, chamber,
   Canvas, stampa completa, target riscaldatori zero a fine stampa e UDS stabile.

Per confrontare le risorse, mantenere stessa fase di stampa e stessi client.
Raccogliere due campioni a 60 secondi di distanza:

```sh
CC2_PID=$(pidof cc2-control)
cat /proc/uptime
cat /proc/$CC2_PID/stat
grep -E 'VmSize|VmRSS|VmStk|Threads' /proc/$CC2_PID/status
wget -qO- http://127.0.0.1:8081/api/health
wget -qO- http://127.0.0.1:8081/api/uds
wget -qO- http://127.0.0.1:8081/api/printer
```

La memoria disponibile e il load average riguardano tutta la stampante;
non misurano da soli il consumo di CC2 Control. Riportare errori riproducibili,
browser/client, modalità LAN/WAN e output dell'installer.

## English quick start

Combined experimental updater for PR46 and PR51–56, preserving the approved
UI. Extract the ZIP and run the Windows launcher or the shell installer with
your printer address. Install only while connected and Idle. The updater
checks hashes, creates a complete backup, preserves settings and verifies
the running binary. Refresh all clients. Printer validation remains pending;
use the printed restore command while Idle to revert.

# CC2 Camera Recovery R2 — pacchetto sperimentale per tester

Questa prova unisce due interventi: gestione dei thread dei client video, per evitare l'accumulo rilevato, e ripristino della comunicazione video dopo il riavvio della sola webcam. Mantiene l'abilitazione LAN una tantum già provata. Usa gli stessi binari della prova riuscita: due riavvii consecutivi della sola camera hanno ripristinato sia video locale sia Matrix.

È una beta temporanea per un piccolo gruppo di tester. Durata prolungata, funzioni AI e sessioni cloud scadute non sono ancora completamente verificate. Non è un watchdog: non riavvia automaticamente la camera se si blocca. Non modifica firmware, elegoo_printer, CC2 Control o avvio di sistema. L'attivazione termina al riavvio completo della stampante.

## Compatibilità e preparazione

- Community Firmware V4.4/V4.4.1, base ELEGOO 02.01.01.07, con webcam originale dal checksum esatto accettato dallo script. Non applicare a firmware diversi.
- Stampante inattiva, SSH disponibile, CC2 Control raggiungibile sulla porta 8081 con telemetria aggiornata.
- Prima della prova verifica che video locale e Matrix funzionino dal normale avvio. Se Matrix è indisponibile già prima, segnala il problema cloud e non attribuirlo alla patch.
- Se avevi Recovery R1 attiva, riavvia normalmente la stampante e verifica i due video. Il controllo R2 rifiuta una R1 ancora caricata.
- Assegna a ogni tester un identificativo T1, T2 o T3. Nessuna password va scritta nei report.

## 1. Copia dal computer

Estrai completamente lo ZIP. Su Windows puoi aprire `Copy-to-printer.cmd`: chiede l'IPv4 della stampante, verifica i due file da trasferire e li copia in `/tmp/`. Può chiedere conferma della chiave SSH e la normale password di root. Il programma effettua soltanto la copia, senza avviare la patch.

In alternativa, dal terminale del computer nella cartella estratta, sostituisci `PRINTER_IP` con l'IP reale:

```sh
scp -O cc2-camera-recovery-r2.tar.gz cc2-camera-tester-report.sh root@PRINTER_IP:/tmp/
```

Non caricare questo archivio dall'updater di CC2 Control: è un pacchetto diagnostico separato.

## 2. Preparazione via SSH — un comando alla volta

```sh
mkdir -p /tmp/cc2-camera-recovery-r2
```

```sh
tar -xzf /tmp/cc2-camera-recovery-r2.tar.gz -C /tmp/cc2-camera-recovery-r2
```

```sh
sh /tmp/cc2-camera-recovery-r2/trial.sh check
```

Deve terminare con `CHECK PASS`. Non riavvia processi. Se si ferma, conserva l'output e non procedere. Non aggirare i checksum o gli altri controlli.

Raccogli lo stato iniziale, sostituendo T1 con il tuo identificativo:

```sh
sh /tmp/cc2-camera-tester-report.sh T1 baseline
```

## 3. Prima attivazione — sempre con stampante inattiva

```sh
sh /tmp/cc2-camera-recovery-r2/trial.sh start
```

Riavvia esclusivamente ai_camera e carica entrambe le librerie. Il video si interrompe durante il riavvio. Attendi circa 15 secondi e verifica il video locale e Matrix; se necessario chiudi e riapri una volta la visualizzazione. Usa un solo client remoto per distinguere il recupero da eventuale latenza con più spettatori.

```sh
sh /tmp/cc2-camera-recovery-r2/trial.sh status
```

```sh
sh /tmp/cc2-camera-tester-report.sh T1 after-start
```

Annota separatamente se locale e Matrix sono tornati. Le librerie caricate e i contatori non bastano a dimostrare il video. Nella prova riuscita abbiamo visto `ready=1`, `initial_calls=1`, `initial_returns=1`, `connections=2`, `announced=2`, `failed=0`; i valori possono dipendere dai collegamenti presenti.

## 4. Ripetibilità — solo dopo il primo esito positivo

Sempre con stampante inattiva, esegui una sola seconda prova:

```sh
sh /tmp/cc2-camera-recovery-r2/trial.sh restart
```

Ricontrolla entrambi i video, poi:

```sh
sh /tmp/cc2-camera-tester-report.sh T1 after-restart
```

Se una prova fallisce, non ripeterla automaticamente. Raccogli il report con fase `issue`, descrivi cosa è successo e ripristina il normale avvio a stampante inattiva.

## 5. Osservazione normale e risultati

Dopo due esiti positivi, lasciala attiva durante l'uso normale. Raccogli un report dopo 30 minuti e un altro alla fine dell'osservazione, indicando la durata in `TESTER-REPORT.md`. Non eseguire loop di connessione o prove di stress. Non riavviare la camera durante una stampa. Una normale stampa prolungata è una verifica successiva, dopo i controlli da inattiva.

```sh
sh /tmp/cc2-camera-tester-report.sh T1 after-30m
```

Il report viene mostrato e salvato in un piccolo file `/tmp/cc2-camera-report-...txt`. Raccoglie PID, memoria, contatori e numero di connessioni di controllo; non invia richieste HTTP e non modifica servizi. I contatori `spawned=0`/`reaped=0` non dimostrano un errore: quel percorso potrebbe non essere stato esercitato. Per l'AI indica la funzione effettivamente osservata, altrimenti scrivi `NOT VERIFIED`.

Compila `TESTER-REPORT.md` e invialo insieme ai piccoli report testuali. Non pubblicare `ai_camera-trial.log` o i log completi: possono contenere dati di sessione. Il ritardo WAN e i problemi Wi-Fi restano una verifica separata.

## Ripristino

Con stampante inattiva, un normale riavvio completo elimina questa attivazione in memoria:

```sh
reboot
```

Al termine verifica di nuovo i due video. I file in `/tmp/` scompaiono normalmente al riavvio; il pacchetto non modifica la configurazione di avvio già presente e non riarma vecchie prove one-shot. Se avevi altre modifiche persistenti, queste restano quelle precedenti.

`SHA256SUMS.txt` verifica l'integrità dei file, non è una firma di autenticità. In `validation/` trovi provenienza dei binari e risultati delle due prove reali; `sources/` contiene i sorgenti originali delle patch. Nessun firmware vendor, chiave privata o credenziale è incluso.

Grazie a **tharje** per la diagnosi e le catture, e ai beta tester per le verifiche reali locali e remote.

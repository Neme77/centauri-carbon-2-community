# Retrazione Canvas estesa — prova R1

Pacchetto sperimentale autonomo per Community Firmware **V4.4.2 / CC2 Control 1.2.21**. Riproduce la modifica di **Noahwhite2014** (#172 / #202): alza il limite accettato da 350 a 1000 mm. Non modifica automaticamente la distanza, non espelle filamento, non invia movimenti né riavvia la stampante. Non modifica la webcam o CC2 Control.

**Prima prova controllata: questo installer non è ancora validato sulla stampante.** Il controllo delle impronte non dimostra che una distanza sia adatta al proprio percorso filamento. Il contributor ha riportato una prova a 560 mm; scaricamento e successivo caricamento a 1000 mm non sono validati.

## Requisiti

- PC Windows, Linux o macOS con Python 3.10+ e client OpenSSH (`ssh`). Stampante raggiungibile via SSH; password inserita nel terminale, mai salvata dal pacchetto.
- Stampante connessa, inattiva, senza stampa sospesa, temperature non superiori a 50 °C e riscaldatori spenti; almeno 128 MiB liberi oltre al caricamento del pacchetto temporaneo.
- Programma/runtime esattamente compatibili: ogni impronta è verificata. Configurazioni con include o valori ambigui vengono rifiutate. Non aggirare un rifiuto.

## Prima prova

1. Estrarre tutto lo ZIP sul PC.
2. Windows: aprire `Start-Windows.cmd`. Linux/macOS: `sh Start-Linux-macOS.sh` nel terminale.
3. Scegliere **1 — Verifica**, inserire IP e credenziali SSH; inviare al maintainer l'esito prima di installare.
4. Dopo aver concordato la prova, scegliere **3 — Installa** e digitare `INSTALL PULLBACK`. Non chiudere il terminale durante la transazione.
5. Solo dopo `INSTALLATION PASSED`, riavviare normalmente la stampante inattiva. Il programma in memoria mantiene il limite precedente fino al riavvio.
6. Scegliere **2 — Stato** e verificare programma esteso, Canvas, webcam locale/Matrix e caricamento ordinario. La distanza configurata rimane quella precedente. Concordare separatamente la prova fisica; nessuna sequenza automatica è inclusa.

Da terminale è anche possibile usare `python manage.py check --host IP` (Linux/macOS: `python3`). Azioni: `check`, `status`, `install`, `remove`.

## Rimozione e aggiornamenti

**Prima di aggiornare app o firmware, rimuovere questa mod e riavviare.** L'installer ordinario controlla il programma originale e rifiuta quello esteso. Un normale cambio canale non rimuove la mod.

Scegliere **4 — Rimuovi**, confermare `REMOVE PULLBACK`, attendere `REMOVAL PASSED` e riavviare mentre la stampante è inattiva. Prima del ripristino del programma originale, gli eventuali `unload_fila_dist` superiori a 350 vengono riportati a 350 in `printer_dsp.cfg`, `autosave.cfg` e, se presente, `autosave_backup.cfg`. Altri parametri, mesh e impostazioni non vengono riscritti. I backup vengono conservati in `/opt/usr/cc2-canvas-pullback-1000.removed-*`.

Non cambiare la distanza tra rimozione e riavvio: il vecchio processo esteso è ancora in memoria. Verificare **Stato** dopo il riavvio prima dell'aggiornamento.

Se una transazione viene interrotta, conservare l'output e usare **Stato**; non riavviare alla cieca. La rimozione può ripristinare una transazione parziale solo se tutti i file sono ancora riconoscibili e i backup sono integri. File estranei vengono rifiutati.

## Contenuto e crediti

Sorgenti e script, senza eseguibili vendor, credenziali o chiavi. La trasformazione viene effettuata sul PC su una copia letta dalla propria stampante; vengono verificati SHA-256 prima e dopo. Patch originale e idea: **Noahwhite2014**. Packaging e controlli di installazione/ripristino: progetto CC2 Community. Non è il progetto abbandonato di espulsione completa del filamento.

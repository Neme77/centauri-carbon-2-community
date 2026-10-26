# Retrazione Canvas estesa — prova R4

Pacchetto sperimentale autonomo per Community Firmware **V4.4.1 oppure V4.4.2 / CC2 Control 1.2.21 o 1.2.22**. Riproduce la modifica di **Noahwhite2014** (#172 / #202): alza il limite accettato da 350 a 1000 mm. Non modifica automaticamente la distanza, non espelle filamento, non invia movimenti né riavvia la stampante. Non modifica la webcam o CC2 Control.

**Prova controllata.** R1 è stata provata su V4.4.2 con scaricamento e ricaricamento a 500 mm. R2 ha completato il ciclo installazione/stato/rimozione sulla V4.4.1 di Noahwhite2014: override a 560 mm persistente, ritorno a 350 mm alla rimozione e webcam/CC2 Control funzionanti. In quel ciclo non sono stati inviati movimenti. Il controllo delle impronte non dimostra che una distanza sia adatta al proprio percorso filamento; 1000 mm non sono qualificati per ogni stampante.

## Correzioni R3

Il rifiuto dell’installazione indica ora il percorso esatto del file e il valore unload_fila_dist superiore a 350 mm, anche in autosave_backup.cfg e nelle sezioni SAVE_CONFIG. La rimozione conserva i backup in /opt/usr/cc2-canvas-pullback-1000.removed-<token>, senza il prefisso della cartella temporanea. Menu e archivio identificano ora coerentemente R4. Patch del programma, impronte, protezioni, distanza e timeout di ricaricamento non cambiano.



## Requisiti

- PC Windows, Linux o macOS con Python 3.10+ e client OpenSSH (`ssh`). Stampante raggiungibile via SSH; password inserita nel terminale, mai salvata dal pacchetto.
- Stampante connessa, inattiva, senza stampa sospesa, temperature non superiori a 50 °C e riscaldatori spenti; almeno 128 MiB liberi oltre al caricamento del pacchetto temporaneo.
- Programma/runtime esattamente compatibili: ogni impronta è verificata. Configurazioni con include o valori ambigui vengono rifiutate. Non aggirare un rifiuto.

## Prima prova

1. Estrarre tutto lo ZIP sul PC.
2. Windows: aprire `Start-Windows.cmd`. Linux/macOS: `sh Start-Linux-macOS.sh` nel terminale.
3. Scegliere **1 — Verifica**, inserire IP e credenziali SSH; proseguire solo dopo CHECK PASS. In caso di rifiuto conservare l’output e verificarne la causa.
4. Dopo una verifica riuscita, scegliere **3 — Installa** e digitare `INSTALL PULLBACK`. Non chiudere il terminale durante la transazione.
5. Solo dopo `INSTALLATION PASSED`, riavviare normalmente la stampante inattiva. Il programma in memoria mantiene il limite precedente fino al riavvio.
6. Scegliere **2 — Stato** e verificare programma esteso, Canvas, webcam locale/Matrix e caricamento ordinario. La distanza configurata rimane quella precedente. La verifica fisica della distanza va eseguita separatamente sul proprio percorso filamento; nessuna sequenza automatica è inclusa.

Da terminale è anche possibile usare `python manage.py check --host IP` (Linux/macOS: `python3`). Azioni: `check`, `status`, `install`, `remove`.

## Rimozione e aggiornamenti

**Prima di aggiornare app o firmware, rimuovere questa mod e riavviare.** L'installer ordinario controlla il programma originale e rifiuta quello esteso. Un normale cambio canale non rimuove la mod.

Scegliere **4 — Rimuovi**, confermare `REMOVE PULLBACK`, attendere `REMOVAL PASSED` e riavviare mentre la stampante è inattiva. Prima del ripristino del programma originale, gli eventuali `unload_fila_dist` superiori a 350 vengono riportati a 350 in `printer_dsp.cfg`, `autosave.cfg` e, se presente, `autosave_backup.cfg`. Altri parametri, mesh e impostazioni non vengono riscritti. I backup vengono conservati in `/opt/usr/cc2-canvas-pullback-1000.removed-*`.

Non cambiare la distanza tra rimozione e riavvio: il vecchio processo esteso è ancora in memoria. Verificare **Stato** dopo il riavvio prima dell'aggiornamento.

Se una transazione viene interrotta, conservare l'output e usare **Stato**; non riavviare alla cieca. La rimozione può ripristinare una transazione parziale solo se tutti i file sono ancora riconoscibili e i backup sono integri. File estranei vengono rifiutati.

## Contenuto e crediti

Sorgenti e script, senza eseguibili vendor, credenziali o chiavi. La trasformazione viene effettuata sul PC su una copia letta dalla propria stampante; vengono verificati SHA-256 prima e dopo. Patch originale e idea: **Noahwhite2014**. Packaging e controlli di installazione/ripristino: progetto CC2 Community. Non è il progetto abbandonato di espulsione completa del filamento.

## Profilo V4.4.1 in R2

Le impronte coincidono con il firmware V4.4.1 pubblicato. Installazione/stato/rimozione di R2 su questa versione sono stati provati da Noahwhite2014; prima di una nuova installazione occorre ripristinare i file originali da eventuali patch manuali. R1 è stata provata su V4.4.2 con scaricamento e ricaricamento a 500 mm. Il kit non modifica distanze o timeout di ricarica e non qualifica 1000 mm per ogni percorso fisico. Generazioni miste vengono rifiutate.


## Validazione R3 e compatibilità R4

Noahwhite2014 ha completato sulla V4.4.1 il test R3: rifiuto con file/valore e hash immutati, installazione, riavvio, stato esteso, salvataggio 560 senza movimento, rimozione con ritorno a 350 e ripristino esatto degli originali, nome backup corretto e webcam funzionante. R4 aggiunge il riconoscimento esplicito di CC2 Control 1.2.22, oltre alla 1.2.21, e aggiorna le istruzioni. La trasformazione del programma e le impronte dei runtime non cambiano. Il nuovo controllo versione è verificato automaticamente; la prova sulla stampante riportata è quella di R3. La V4.4 non ha un profilo runtime qualificato in questo componente.

I controlli Carica/Scarica della bobina esterna in CC2 Control sono indipendenti: questa mod riguarda la retrazione Canvas dopo il taglio, non la corsa della bobina esterna.

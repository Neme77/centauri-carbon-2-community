# Aggiornamento CC2 Control 1.2.26

Il pacchetto completo richiede il profilo verificato di **Community Firmware
V4.4.3**, base ELEGOO 02.01.01.07 e Canvas 00.00.00.44. Non aggiorna il display
originale o il firmware base. Profili sconosciuti, avvii modificati e operazioni
native incomplete vengono rifiutati. Per una base precedente aggiornare prima
alla V4.4.3. Il controllo usa impronte reali, non il solo numero di versione.

## Installazione

Stampante connessa, inattiva e fredda, target dei riscaldatori a zero, nessuna
stampa, scansione o calibrazione. Chiudere i video. Mantenere alimentazione e
connessione. Estrarre tutto lo ZIP: su Windows avviare `Install-Windows.cmd`;
su Linux/macOS `sh ./install-cc2-control.sh IP-STAMPANTE`. Servono OpenSSH,
utente root e password SSH, separata dal codice LAN. Gli utenti del pannello
possono usare l'aggiornamento firmato da Impostazioni → Aggiornamenti.

Un solo installer riconosce prima installazione, aggiornamento RFID supportato
e versione già corrente. Il payload è identico per PC e pannello. Impostazioni,
calibrazioni, materiali, preferenze, piani e bobine vengono conservati. Annotare
il percorso del backup e il comando di ripristino stampati dall'installer.

La preparazione usa il disco persistente `/opt/usr`, non `/tmp`: servono almeno
64 MiB di spazio libero e 24 MiB di RAM disponibile prima del trasferimento.
I dati del payload vengono ripuliti soltanto dopo il successo; log e backup
restano. Se fallisce, conservare la cartella indicata e il log.

## CC2 non Combo o Canvas non collegato

L'installer aggiorna normalmente app, EXHAUST, camera e Connessioni senza
pubblicare una richiesta di attivazione RFID. Il pannello non resta in attesa
di un lettore assente e non viene scritto alcun firmware Canvas. Se Canvas viene
aggiunto in seguito, basta rilanciare lo stesso installer con l'accessorio
collegato per preparare l'attivazione protetta. Un aggiornamento RFID già attivo
mantiene i suoi controlli indipendenti, senza richiedere una risposta Canvas
solo per aggiornare l'app.

## Prima attivazione RFID

Sono inclusi EXHAUST minimo, camera/ripristino e Connessioni. Il flusso RFID
salva e verifica l'originale Canvas già presente sulla stampante e prepara la
protezione host. Seguire i messaggi in Canvas → RFID: serve un riavvio completo
a stampante inattiva per caricare la protezione; dopo la sua verifica avviene
la modifica dell'applicazione Canvas per rimuovere il filtro produttore. Un
ulteriore riavvio completo verifica Canvas e protezione. I LED possono lampeggiare
rossi mentre il processo nativo viene fermato per il trasferimento. **Non togliere
alimentazione e non riavviare durante il trasferimento.**

Se RFID è già attivo e supportato, l'aggiornamento conserva Canvas, librerie native,
file di avvio e backup originale: sostituisce l'app e quattro file RFID collegati,
senza un altro flash. Se tutto è già aggiornato, verifica senza ripetere l'attivazione.

Per leggere una bobina, avviare la scansione e avvicinare il tag solo quando è
armata. Può attendere qualche secondo per acquisire dati freschi. Controllare
l'anteprima, selezionare lo slot e confermare. Materiale assegnato e presenza del
filamento sono distinti: inserire il filo nell'ingresso scelto per rilevarne la
presenza. Sono stati letti fisicamente un tag Anycubic PETG e una bobina Creality;
non significa compatibilità con ogni produttore o tag. Nessuna assegnazione
riscalda, muove o carica automaticamente il filamento.

## Ripristino

Per un aggiornamento della sola app usare il comando esatto del proprio backup:
ripristina insieme app precedente e file RFID collegati. Prima di rimuovere la
protezione host o cambiare firmware base, ripristinare dal pannello l'originale
Canvas e completare riavvio e verifica; solo dopo rimuovere la protezione e
completare il relativo riavvio. Non cancellare blocchi, backup o registri.

Se viene richiesto recupero, mantenere alimentazione e conservare log e backup.
Il protocollo conferma la ricezione del trasferimento, non una rilettura fisica
della flash. Le istruzioni del pannello distinguono trasferimento e verifica.

[Note della release](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.26-V4.4)
· [Guida completa in inglese](UPDATE_CC2.md)

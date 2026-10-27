# Update CC2 Control 1.2.23 — esclusivamente Community Firmware V4.4 / V4.4.1 / V4.4.2

[English](UPDATE_CC2.md) · [Homepage](../README.md)

La 1.2.23 comprende Dashboard e Controllo rinnovati, selezione materiali condivisa Canvas/bobina esterna e comandi nativi della bobina esterna, Spoolman facoltativo, salvataggio locale del codice LAN, avviso di fine stampa, cronologia bobine compatta e riscaldamento piatto temporizzato sperimentale. Preferenze e librerie vengono conservate.

**Riavvio completo solo alla prima attivazione del fix webcam.** È incluso dalla 1.2.19: se già attivo, questo aggiornamento non richiede un ulteriore riavvio. Il firmware completo V4.4.2 si riavvia già durante l'installazione. Se il vecchio downloader fallisce con errore HTTPS, scarica il `.cc2update` sul computer e caricalo dal pannello oppure usa l'installer per PC. Chiudi i video prima dell'aggiornamento.

**Solo Community Firmware V4.4, V4.4.1 o V4.4.2 già installato e configurato. Non installare sulla V4.3, sul firmware originale ELEGOO o su altri modelli.** Il solo numero base 02.01.01.07 non basta: prima di fermare il servizio, l’installer verifica binario community su disco, processo della stampante e daemon degli aggiornamenti. Impronte sconosciute vengono rifiutate.

È uno ZIP applicativo per aggiornare via SSH. Non installa l’immagine firmware o SSH e non sostituisce il modulo callback. Installa anche il componente di avvio webcam con verifiche di compatibilità descritto sopra. Canvas usa MQTT come impostazione iniziale; UDS e Quiet sono opzioni sperimentali indipendenti; fix LAN/WAN, luci e funzioni già validate restano inclusi.

## Scegliere il metodo di aggiornamento

- **La scheda Impostazioni → Aggiornamenti non è ancora presente:** seguire una volta l'installazione dello ZIP dal computer descritta sotto. Installa anche i componenti dell'updater.
- **Il pannello aggiornamenti è già installato:** aprire Impostazioni → Aggiornamenti, quindi Controlla aggiornamenti. Inserire il codice LAN, scegliere Scarica e verifica e, solo dopo la verifica, Installa con conferma separata. In alternativa caricare `CC2-Control-1.2.23-V4.4.cc2update` dalla release. Lo ZIP dell'installer non va caricato nel pannello.
- Se il pannello riporta già 1.2.23, non serve reinstallare. Controlla aggiornamenti aggiorna manualmente l'indice in cache.

Per entrambi i metodi valgono stampante inattiva/fredda e target zero. La sola verifica non installa nulla. Indice e applicazione sono firmati; basi incompatibili e versioni uguali/precedenti vengono rifiutate. Non interrompere l'alimentazione. Attendere l'esito, ricaricare e ricontrollare connessione, Canvas e impostazioni dopo un riavvio a stampante inattiva.

Da Dashboard → Mantieni attiva si può abilitare anche la **camera persistente**: attivare la preferenza e poi avviare esplicitamente il live. Lo stesso stream continua durante navigazione e schede nascoste/minimizzate. Pausa, disattivazione, chiusura/uscita dalla pagina e passaggio a un altro viewer lo interrompono. Ricaricare conserva la preferenza ma non avvia automaticamente il video.

## Perché viene richiesto il codice LAN

Il codice LAN conferma che sei autorizzato ad aggiornare questa stampante. **Controlla aggiornamenti** non lo richiede; **Scarica e verifica**, il caricamento di un file `.cc2update` e **Installa** lo richiedono. La firma del pacchetto verifica la provenienza dell'applicazione; il codice LAN conferma invece l'autorizzazione sulla stampante.

1. Sul display apri **Impostazioni → Solo LAN** e leggi il codice di accesso. Distingui con attenzione la lettera **O** dal numero **0** e rispetta maiuscole/minuscole.
2. In **Impostazioni → Aggiornamenti** inserisci quel codice nel campo **Codice di accesso LAN**, anche se CC2 Control è già configurato e connesso. Puoi inserirlo ogni volta oppure salvarlo facoltativamente in Impostazioni → Connessione: questo browser lo precompilerà mascherato dopo la verifica, mantenendo le conferme. Dimentica rimuove il valore dal browser.
3. Scarica/verifica il pacchetto, quindi premi **Installa** e conferma. Inserire il codice da solo non avvia alcuna installazione.

**Non è la password SSH/root e non è un codice GitHub.** Funziona anche nella modalità WAN ibrida supportata, usando lo stesso codice configurato in CC2 Control. Se lo leggi passando temporaneamente a Solo LAN, al ritorno nella modalità abituale verifica che CC2 Control risulti connesso prima di aggiornare.

Dopo un ripristino di fabbrica il codice può cambiare: inserisci prima il nuovo codice nella scheda **Connessione**, scegli **Cambia / Rivalida** e conferma. Il servizio salva il codice e si riavvia; verifica la connessione, poi inserisci il nuovo codice anche nella scheda **Aggiornamenti**. Il pannello confronta la conferma con il codice configurato nel servizio. Dopo cinque tentativi errati attende 60 secondi prima di accettare un nuovo tentativo. Non inviare il codice in commenti GitHub o screenshot pubblici.

## 1. Preparazione

1. Scaricare **CC2-Control-1.2.23-Multiplatform-Update.zip** e checksum dalla [release applicativa](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.23-V4.4).
2. Confrontare SHA-256 dello ZIP con il valore pubblicato:

~~~powershell
Get-FileHash -Algorithm SHA256 -LiteralPath '.\CC2-Control-1.2.23-Multiplatform-Update.zip'
~~~

3. Confermare V4.4 e **Stampante connessa** in CC2 Control.
4. Attendere **Inattiva**, senza stampa o calibrazioni, ugello e piatto al massimo a **50 °C**, target entrambi a **0 °C**. Chiudere camera live e altre pagine di controllo durante l’installazione.
5. Annotare l’IP. Il computer deve raggiungere la rete locale e disporre di OpenSSH (ssh e scp).
6. Estrarre tutto lo ZIP in una cartella nuova. Non avviare i launcher dallo ZIP compresso e non usare questo ZIP nel menu USB degli aggiornamenti firmware.

Codice LAN, materiali, preferenze, dati piatti/ugelli e inventario bobine vengono conservati. Il backup automatico registra applicazione precedente e launcher del servizio.

## 2. Installazione

### Windows

Aprire la cartella estratta, fare doppio clic su **Install-Windows.cmd** e inserire l’IP.
In alternativa, aprire PowerShell nella cartella:

~~~powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install-CC2-Control.ps1 -PrinterIp 192.168.1.50
~~~

Sostituire l’IP di esempio. Serve il client OpenSSH di Windows.

### Linux e macOS

Aprire un terminale nella cartella estratta:

~~~sh
sh ./install-cc2-control.sh 192.168.1.50
~~~

Sostituire l’IP di esempio.

### SSH e conclusione

Utente **root**, con la propria password SSH ([riferimento SSH](FIRST_RUN.it.md#riferimento-per-laccesso-ssh)). La digitazione non viene mostrata e la password può essere richiesta più volte. È diversa dal codice LAN.

Alla prima connessione verificare l’impronta della chiave della stampante. Solo per cambiamenti previsti seguire il [recupero della chiave SSH](../cc2-control/installer/README.md#ssh-host-key-changes).

L’installer verifica pacchetto, base reale e stato; crea il backup; ferma **solo CC2 Control**; installa applicazione ARM/UI e riavvia il servizio. Attendere fino a **240 secondi** per salute, checksum del binario in esecuzione, registrazione MQTT e snapshot.

Il successo mostra **INSTALLATION PASSED**, percorso del backup e comando esatto di ripristino. In caso di errore leggere lo stato del rollback e conservare il percorso. Non stampare finché il servizio precedente o aggiornato non funziona.

## 3. Verifica

1. Aprire http://IP-STAMPANTE:8081 e ricaricare; Ctrl+F5 se rimane la vecchia UI.
2. Verificare **1.2.23**, **Stampante connessa**, temperature e slot Canvas. Il codice LAN salvato non dovrebbe essere richiesto di nuovo.
3. Se desiderato, scegliere **Magyar** da **Dashboard → Mantieni attiva**. Le lingue diventano otto.
4. Controllare piatti/ugelli in **Livellamento piatto**. La calibrazione accetta temperatura **40–110 °C**, stabilizzazione **0–60 minuti** (predefinita **10**) e ugello. Lasciare il piatto libero prima di confermare.
5. Ogni piatto conserva misure per temperatura e ugello. Il dialogo di stampa utilizza la misura a temperatura più vicina o quella scelta, applicando la correzione Z dell’ugello selezionato.
6. Riavviare una volta mentre la stampante è inattiva, poi verificare avvio automatico, connessione e Canvas.

Non servono ripristino di fabbrica o sostituzione del codice LAN. Il display mantiene la base vendor 02.01.01.07; l’applicazione è 1.2.23.

## 4. Ripristino dell’applicazione precedente

A stampante connessa, inattiva e fredda con target dei riscaldatori a zero, usare il backup esatto indicato dal proprio installer:

~~~sh
ssh root@IP-STAMPANTE
sh /opt/usr/cc2-control-backup-PROPRIO-TIMESTAMP-PID/restore.sh
~~~

Sostituire IP e percorso segnaposto. Credenziali, materiali, preferenze, piatti/ugelli e bobine correnti vengono mantenuti. Se la precedente app era integrata, il launcher può tornare al binario in /opt/inst; il backup di /opt/usr può contenere solo dati persistenti.

Si ripristina l’**applicazione**, non un firmware ELEGOO precedente. Il blocco del downgrade firmware non cambia. Non usare il backup di un’altra stampante.


Libreria piatti: le vecchie app con minimo 40 °C non leggono nuove misure a 30–39 °C e possono scartare i profili non calibrati al salvataggio. Conservare una copia prima di tornare a una versione precedente.



## Codice LAN e integrazioni

Impostazioni → Connessione offre il salvataggio facoltativo del codice LAN verificato in questo browser: viene precompilato e mascherato nelle azioni protette, che mantengono le conferme. Dimentica rimuove il valore salvato dal browser. Le integrazioni sperimentali sono in Impostazioni → Integrazioni. Mantieni attiva si trova nell’intestazione della camera sulla Dashboard.

## Componente separato per la retrazione Canvas estesa

Il download sperimentale R4 aumenta fino a 1000 mm il massimo accettato per la retrazione Canvas sui runtime esattamente qualificati V4.4.1/V4.4.2 con CC2 Control 1.2.21 o 1.2.22. Non viene incluso o attivato dall’aggiornamento dell’app e non estende la corsa della bobina esterna. Seguire README e Verifica del pacchetto. Rimuoverlo e riavviare prima degli aggiornamenti ordinari dell’app/firmware: la rimozione riporta a 350 mm i valori superiori nei file supportati. Distanze fisiche e parametri di ricaricamento dipendono dal percorso del filamento.


## **FINALMENTE TPU SENZA SCOLLEGARE IL CANVAS**

La funzione si abilita in **Impostazioni → Integrazioni → Integrazioni sperimentali** e resta **sperimentale, facoltativa e inizialmente spenta**. Serve un percorso esterno diretto. **DamiBFryta ha completato il ciclo PLA → TPU e la stampa eSUN TPU 95A senza errori e senza scollegare il Canvas.** Materiali e percorsi diversi richiedono le proprie prove.

Inclusi i fix Console, fine stampa, indicazione delle sperimentali e testi ungheresi di pezso92, oltre a tutte le funzioni precedenti. Il riavvio completo serve solo alla prima attivazione del fix webcam, gia presente da 1.2.19 o firmware V4.4.2. Firmware e builder non cambiano.

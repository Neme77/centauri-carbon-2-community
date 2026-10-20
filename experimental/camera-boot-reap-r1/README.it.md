# Webcam: prova di un solo avvio — Boot R1 con libreria R2

**Sperimentale, per la sola stampante verificata. Non è un aggiornamento di CC2 Control né un rilascio stabile.**

Questa prova carica la stessa libreria R2 durante il normale avvio della webcam, senza il precedente riavvio a caldo e senza forzare il video LAN: `enable_lan_once` deve essere **0**. La prova serve a verificare video locale e Matrix/RTC insieme. AI e stampe lunghe restano verifiche separate.

Compatibilità stretta: Community V4.4/V4.4.1, base ELEGOO 02.01.01.07; webcam SHA256 `ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99`; `/etc/init.d/printer` SHA256 `bcabb1fa405b066b06c229c6d8cc6e474a0ccc57e31a1b5b387d3b55945be563`. Un'altra versione viene rifiutata. Il filesystem della tua stampante è stato verificato scrivibile; non forzare rimontaggi o controlli falliti.

## Cosa cambia

`stage` salva una copia esatta dello script di avvio in `/opt/usr/cc2-camera-reap-boot-r1/printer.original` e sostituisce solo la chiamata `ai_camera &` con un lanciatore nello stesso punto della sequenza. Non modifica eseguibili ELEGOO, callback, MQTT, movimento, riscaldamento o CC2 Control, e non ferma processi. Se il payload persistente manca, il nuovo punto di avvio richiama direttamente la webcam originale.

La prova è armata per **un solo avvio**. Il lanciatore consuma l'abilitazione prima di attivare R2. Al riavvio completo successivo, la webcam parte originale attraverso il lanciatore disarmato; lo script di avvio modificato e i file della prova rimangono finché non esegui rollback. Se un controllo preliminare del payload fallisce, il lanciatore tenta l'avvio originale. Un problema successivo all'attivazione non viene nascosto con riavvii automatici.

La libreria R2 recupera i thread delle connessioni video terminate e mantiene il limite di quattro client attivi. I test locali precedenti, incluso quello di tharje con 450 connessioni e memoria virtuale stabile, non convalidano questa nuova modalità di avvio o Matrix/RTC. Il riavvio della sola webcam originale ha già riprodotto il blocco del video Matrix; questo pacchetto non implementa il recupero di quella sessione a caldo.

## Copia e controllo

Stampante inattiva, senza stampa/calibrazione/aggiornamento in corso. Verificare prima il video Matrix originale. Estrarre lo ZIP sul PC e aprire PowerShell nella cartella contenente `cc2-camera-boot-r1.tar.gz`. Sostituire l'IP se necessario.

```powershell
scp -O .\cc2-camera-boot-r1.tar.gz root@192.168.1.103:/tmp/cc2-camera-boot-r1.tar.gz
```

Nella sessione SSH, separatamente:

```sh
mkdir -p /tmp/cc2-camera-boot-r1
```

```sh
tar -xzf /tmp/cc2-camera-boot-r1.tar.gz -C /tmp/cc2-camera-boot-r1
```

```sh
sh /tmp/cc2-camera-boot-r1/manage.sh check
```

Richiede **CHECK PASS**. Il controllo verifica checksum, librerie, processo originale e stato inattivo recente; scrive solo la diagnostica temporanea, senza installare nulla. In caso di STOP inviare l'output e fermarsi.

## Preparazione e riavvio manuale

Solo dopo il controllo:

```sh
sh /tmp/cc2-camera-boot-r1/manage.sh stage
```

Richiede **STAGED**. La webcam attuale rimane in esecuzione. Riavviare poi normalmente la stampante inattiva, senza usare l'arresto di emergenza. Non avviare una stampa durante questa prima prova.

Quando il pannello è nuovamente connesso:

```sh
sh /opt/usr/cc2-camera-reap-boot-r1/manage.sh status
```

Verificare `Boot R2 library mapped: yes` e una riga riferita al PID attuale con `patched=1 enable_lan_once=0`. La sola presenza del file marker o del processo non dimostra il video. Se la libreria non è caricata, la prova R2 non è attiva anche se il video originale funziona.

## Accettazione della prova

1. Verificare in Matrix separatamente stampante online e video funzionante.
2. Aprire il video locale da CC2 Control e tenere attivi entrambi per un minuto.
3. Chiudere Matrix e verificare che il locale continui; riaprire Matrix una volta.
4. Eseguire nuovamente status e inviare gli output, indicando fluidità e qualsiasi effetto inatteso.

Non servono altri stress test di connessione in questa fase. Non riavviare la sola webcam per questa prova: perderebbe di nuovo la normale inizializzazione che stiamo preservando. Non inviare log integrali della camera: possono contenere token RTC.

## Ritorno all'originale

Per tornare a una webcam originale senza R2 basta un altro **normale riavvio completo, a stampante inattiva**: l'abilitazione è già stata consumata. Questo lascia il punto di avvio modificato ma disarmato.

Per ripristinare anche lo script di avvio esatto, mentre la stampante è inattiva:

```sh
sh /opt/usr/cc2-camera-reap-boot-r1/manage.sh rollback
```

Attendere **ORIGINAL STARTUP RESTORED**, poi riavviare normalmente la stampante inattiva. Il rollback non termina il processo corrente: un'eventuale R2 già caricata rimane in memoria fino al suo termine. Backup e payload vengono conservati. Se lo script è stato modificato da un altro intervento o da un firmware successivo, il ripristino rifiuta di sovrascriverlo.

Non applicare questa prova durante un aggiornamento firmware e non usarla per attivazioni permanenti. Un risultato negativo va riportato e ripristinato, senza forzare controlli o ripetere automaticamente.

Crediti: **tharje** per l'analisi e la riproduzione del problema dei thread, **dwolfuk** per il confronto Matrix. Sorgenti della prova e della libreria inclusi; nessun eseguibile vendor, chiave privata o credenziale è distribuito.

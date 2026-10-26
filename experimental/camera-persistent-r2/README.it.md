# Camera R2 persistente — installazione personale controllata

Carica a ogni avvio le stesse due librerie già provate: gestione thread R2 e recupero connessione R2. Nessun binario nativo viene ricompilato o sostituito. La prova temporanea ha recuperato locale e Matrix per due riavvii consecutivi; è seguita osservazione normale con IA abilitata. Durata estesa e specifiche rilevazioni IA restano da verificare.

Questa installazione è preparata per lo stato esatto comunicato dalla tua stampante: `/etc/init.d/printer` SHA-256 `01924b3dc427972838e995ac6763b566b85754a162d60fd6728553a405cd00cc`, con il vecchio aggancio one-shot. Non è un installer generico per tutti i tester. Firmware e camera devono corrispondere ai checksum previsti.

## Preparazione e controllo

Con stampante inattiva, copia `cc2-camera-persistent-r2.tar.gz` dal computer in `/tmp/`. Questo pacchetto non va caricato nell'updater di CC2 Control.

Esegui separatamente via SSH:

```sh
mkdir -p /tmp/cc2-camera-persistent-r2
```

```sh
tar -xzf /tmp/cc2-camera-persistent-r2.tar.gz -C /tmp/cc2-camera-persistent-r2
```

```sh
sh /tmp/cc2-camera-persistent-r2/manage.sh check
```

Condividi il risultato prima dell'installazione. Nessun processo viene riavviato dal controllo.

## Installazione

Dopo il controllo positivo, con stampante inattiva:

```sh
sh /tmp/cc2-camera-persistent-r2/manage.sh install
```

Il comando salva una copia esatta dell'avvio attuale in `/opt/usr/cc2-camera-recovery-r2/printer.original` e sostituisce soltanto la riga del vecchio launcher webcam. Non modifica la patch callback, elegoo_printer, i servizi di stampa o il firmware camera. La camera attualmente attiva continua a funzionare fino al successivo avvio.

Per provare l'attivazione persistente, riavvia normalmente la stampante inattiva:

```sh
reboot
```

Attendi l'avvio completo e verifica video locale e Matrix. Poi:

```sh
sh /opt/usr/cc2-camera-recovery-r2/manage.sh status
```

Entrambe le librerie devono risultare caricate dalla cartella persistente e i marker devono riferirsi al PID attuale. Lo stato non prova il video: va osservato. Dopo il primo esito positivo, un secondo normale riavvio da inattiva può confermare la persistenza.

## Disattivazione o ripristino

Per disattivare il caricamento successivo, senza cambiare il processo attuale:

```sh
sh /opt/usr/cc2-camera-recovery-r2/manage.sh disable
```

Riavvia poi la stampante inattiva per tornare all'avvio della camera senza le nostre librerie. Per riabilitare, sempre da inattiva, usa `manage.sh enable` e un nuovo avvio. `manage.sh verify` verifica l'integrità dell'installazione.

Per ripristinare esattamente la configurazione di avvio precedente:

```sh
sh /opt/usr/cc2-camera-recovery-r2/manage.sh rollback
```

Il rollback rifiuta di sovrascrivere un avvio modificato successivamente da altri aggiornamenti. Mantiene il backup e non riavvia servizi. Dopo il normale riavvio da inattiva rientra in uso la configurazione precedente, compreso il vecchio launcher one-shot non riarmato.

## Comportamento in caso di incompatibilità

Se il caricatore rileva checksum diversi, payload mancante, dipendenze non disponibili o disattivazione, avvia la camera senza la patch. I controlli avvengono prima dell'attivazione; non sono un supervisore del processo dopo l'avvio. Non viene introdotto un watchdog o un riavvio automatico in caso di crash. Un futuro firmware che cambia la camera richiede nuova verifica, non un'applicazione forzata di queste librerie.

Non inviare log completi della camera: possono contenere dati di sessione. Il pacchetto contiene solo patch originali, helper e sorgenti; nessun firmware vendor, chiave privata o credenziale.

Grazie a tharje per la diagnosi e le catture, e ai beta tester per le verifiche reali.

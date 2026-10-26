# Recupero webcam R2 — prova temporanea

Questa prova conserva la libreria di recupero thread R2 già qualificata e la sua abilitazione LAN una tantum. La nuova libreria di riconnessione R2 copre anche l'inizializzazione diretta della webcam all'avvio: l'annuncio arriva dopo il ritorno dell'inizializzazione nativa, poi alle nuove connessioni di controllo, evitando duplicati. ELEGOO printer può quindi usare i parametri di sessione che conserva. Nessuna credenziale viene letta o copiata dalla patch.

Non è una correzione definitiva: video locale, Matrix, riavvio della sola camera e AI richiedono conferma sulla stampante. Le prove offline non dimostrano il recupero reale, né il rinnovo di token scaduti.

## Preparazione

Solo stampante inattiva, con video locale e Matrix funzionanti prima della prova. Dopo la precedente R1 serve il normale riavvio della stampante e la verifica dei due video. R2 rifiuta l'avvio finché la libreria di recupero R1 è ancora attiva. Non eseguire cicli di connessione o prove durante una stampa. Non cambiare rete o parametri Wi-Fi contemporaneamente.

Dal computer copia `cc2-camera-recovery-r2.tar.gz` in `/tmp/`. Sulla stampante esegui separatamente:

```sh
mkdir -p /tmp/cc2-camera-recovery-r2
```

```sh
tar -xzf /tmp/cc2-camera-recovery-r2.tar.gz -C /tmp/cc2-camera-recovery-r2
```

```sh
sh /tmp/cc2-camera-recovery-r2/trial.sh check
```

Condividi il risultato prima di attivare. Il controllo non riavvia processi e non invia notifiche.

## Prova assistita

Dopo il controllo positivo, `sh /tmp/cc2-camera-recovery-r2/trial.sh start` riavvia esclusivamente ai_camera con la libreria thread R2 e il recupero R2. Non riavvia elegoo_printer né modifica file di sistema. Verifica video locale, Matrix e poi `sh /tmp/cc2-camera-recovery-r2/trial.sh status`. I contatori distinguono ingresso nell'avvio, ritorno dall'inizializzazione e connessioni di controllo. `ready=1` o `announced` non dimostrano che il video funzioni.

Solo dopo una prima conferma positiva si potrà provare un secondo riavvio camera con `sh /tmp/cc2-camera-recovery-r2/trial.sh restart`. Non automatizzare ripetizioni in caso di errore.

## Ripristino

Un normale riavvio della stampante inattiva elimina questa attivazione in memoria e ripristina l'avvio già configurato. L'eventuale precedente prova one-shot non viene riarmata da questo pacchetto. Nessun file originale viene sostituito. Se la prova fallisce, conserva solo status e risultati; i log completi possono contenere dati di sessione e non vanno pubblicati.

Grazie a tharje per la diagnosi e le catture della perdita di stack, e ai beta tester per le verifiche locali e remote.

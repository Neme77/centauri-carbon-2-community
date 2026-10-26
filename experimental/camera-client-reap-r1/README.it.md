# Webcam: recupero thread client — prova R2

Prova sperimentale per la sola ai_camera della base ELEGOO 02.01.01.07 su Community Firmware V4.4/V4.4.1. Non aggiorna CC2 Control.

Eseguibile richiesto: SHA256 ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99. Qualsiasi altro eseguibile viene rifiutato.

Il problema riprodotto da **tharje** nella issue #160 è una crescita degli stack trattenuti dopo le connessioni MJPEG, seguita da std::system_error e SIGABRT. La prova sostituisce una sola chiamata di creazione dei thread client: recupera quelli realmente terminati con pthread_tryjoin_np e gestisce gli errori di creazione chiudendo/rimuovendo la nuova connessione. Mantiene il contenitore compatibile con il join/clear nativo durante lo stop. Nessun detach, thread reaper aggiuntivo o polling.

Il limite nativo di quattro connessioni attive resta invariato. Il contenitore della prova ammette al massimo otto thread ancora da recuperare, per coprire il breve intervallo tra rimozione del socket e ritorno del thread. Al raggiungimento di quel limite, la nuova connessione viene rifiutata.

**Riavvio manuale della sola camera:** il binario originale inizializza il live LAN in pausa. Per questa prova il launcher abilita esplicitamente un secondo piccolo hook che inizializza quel flag a true una sola volta per il nuovo oggetto. I successivi controlli pausa/ripresa originali restano operativi. Questa differenza di avvio viene dichiarata separatamente dalla correzione dei thread e va verificata sulla stampante.

## Prima fase

1. Stampante connessa e inattiva, nessuna stampa o aggiornamento in corso. Mettere in pausa il live e chiudere gli altri visualizzatori della camera.
2. Estrarre i file esclusivamente in /tmp/cc2-camera-reap-r2.
3. Eseguire: sh /tmp/cc2-camera-reap-r2/trial.sh check
4. Condividere l'esito prima dell'attivazione. Check non riavvia processi.

## Attivazione dopo il controllo

Eseguire: sh /tmp/cc2-camera-reap-r2/trial.sh start

Viene riavviato soltanto ai_camera. Il file /opt/bin/ai_camera, i servizi della stampante, la patch callback e le impostazioni Wi-Fi non vengono modificati. La libreria corregge due chiamate solo nella memoria del nuovo processo, dopo verifica dell'hash completo e delle istruzioni originali.

Un marker di attivazione non prova che video, Matrix/RTC o AI funzionino: aprire il live normalmente e riferire il risultato. Eseguire poi:

sh /tmp/cc2-camera-reap-r2/trial.sh status

Verificare spawned/reaped/retained e VmSize dopo poche normali aperture/chiusure; inizialmente nessun ciclo automatico di centinaia di connessioni. Non presentare questa prova come pronta per stampe lunghe o uso non sorvegliato.

## Ripristino

Con stampante inattiva: sh /tmp/cc2-camera-reap-r2/trial.sh rollback-reboot

Oppure riavviare normalmente la stampante inattiva. La prova non modifica l'avvio permanente e non si riattiva dopo il riavvio. Se l'avvio sperimentale fallisce, non ripeterlo automaticamente: conservare il log ai_camera-trial.log e riavviare la stampante inattiva.

Lo ZIP contiene solo sorgenti originali della correzione e componenti compilati da essi. Nessun firmware vendor, binario ai_camera, credenziale o chiave privata è incluso. I test host/ARM emulati non sostituiscono la prova hardware. Il Wi-Fi lento resta un'indagine separata.

## R2

La R2 corregge il punto di avvio della R1: una scrittura finale originale annullava la precedente abilitazione. L’hook opzionale sostituisce ora quella scrittura finale. L’esecuzione offline dell’intero costruttore riproduce il difetto R1 e conferma il flag abilitato con R2; video e SDK devono ancora essere provati sulla stampante.

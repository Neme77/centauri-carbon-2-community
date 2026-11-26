# Managed application updates

## English

Use this route for the signed CC2 Control 1.2.30 update on recognised Community
4.4.4, Universal 07 and 07 HU bases. It does not replace the complete firmware
migration required for 4.4.3, or install on an arbitrary stock/modified image.

1. Save a panel backup and retain existing recovery records. Let any RFID
   operation and its requested full reboots finish. Leave the printer idle and
   cold, heater targets at zero, with live video paused and stable power.
2. Open the bell or Settings → Updates. Verify the release identity and follow
   the signed package's prompts. A signature check alone does not confirm that
   the installed base is admitted; the installer checks that separately.
3. Wait for completion. Do not repeat installation during an active job or cut
   power. Perform the single manual reboot when the instructions request it.
4. Check the panel, original display/languages, Wi-Fi, Canvas and light. Retain
   the install log and backup. Application updating does not request RFID
   activation, restoration or a Canvas firmware transfer.

The initial bridge includes the qualified native display changes and panel
manager. Future compatible panel updates continue through notifications and
the managed panel path; native firmware changes require their own qualification.

Full rollback was validated using the package's controlled rollback procedure.
Keep its verified package/stage and records. If rollback is needed, use the
instructions for that exact release or contact support; do not guess a command,
remove archives or assume an arbitrary older package can be installed over it.
There is no claim here of a new public one-click rollback button.

## Italiano

Questo percorso riguarda il pacchetto firmato CC2 Control 1.2.30 sulle basi
Community 4.4.4, Universal 07 e 07 HU riconosciute. Per la 4.4.3 occorre prima
seguire la migrazione firmware documentata.

1. Salva il backup del pannello e conserva i dati di recupero. Completa eventuali
   operazioni RFID e i riavvii richiesti. Stampante fredda e inattiva, temperature
   impostate a zero, video in pausa e alimentazione stabile.
2. Apri la campanella o Impostazioni → Aggiornamenti e segui le istruzioni del
   pacchetto firmato. La firma valida non sostituisce la verifica della base.
3. Attendi il completamento senza ripetere l'operazione o togliere corrente.
   Riavvia manualmente quando richiesto.
4. Controlla pannello, display e lingue, Wi-Fi, Canvas e luce. Conserva rapporto
   e backup. Questo aggiornamento non richiede attivazione/ripristino RFID o
   trasferimenti del firmware Canvas.

Il rollback completo è stato provato con la procedura controllata del pacchetto.
Per usarlo conserva pacchetto e rapporti e segui le istruzioni di quello specifico
rilascio, oppure contattaci. Gli aggiornamenti futuri del solo pannello restano
notificati; le modifiche native richiedono una verifica dedicata.

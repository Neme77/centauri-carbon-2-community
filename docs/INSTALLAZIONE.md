# Installazione USB

Guida per la base 02.01.00.00 collaudata dal manutentore. I file di release non sono ancora allegati a questa preparazione.

1. Scegliere il pacchetto: **stock bootstrap** se il firmware attivo è originale; **community** se il Dual Trust del progetto è già attivo.
2. Verificare lo SHA-256 del file contro quello della specifica release, ad esempio in PowerShell: `Get-FileHash -Algorithm SHA256 "C:\percorso\firmware.zip.sig"`.
3. A stampante ferma, copiare il pacchetto `.zip.sig` sulla chiavetta senza decomprimerlo e avviare l'aggiornamento locale USB dall'interfaccia. Seguire le indicazioni mostrate dalla stampante e mantenere l'alimentazione durante l'aggiornamento.
4. Attendere il riavvio e verificare versione, accesso SSH e componenti come indicato in TEST.md. Usare le proprie credenziali; nessuna password universale è documentata qui.

Nelle prove del manutentore l'aggiornamento scrive lo slot opposto a quello attivo: rootfsA è mmcblk0p7, rootfsB è mmcblk0p8. Lo slot inattivo non è una garanzia di recupero automatico. Non scrivere manualmente le partizioni per seguire questa guida.

Il ritorno al firmware originale tramite aggiornamento locale è stato provato con successo; dopo il riavvio SSH non era disponibile. Non è un ripristino garantito per qualunque guasto o versione.

HTTP sulla porta 80 e SSH sono servizi della rete locale: configurare l'accesso per utenti fidati. Questa guida non richiede apertura di porte del router.

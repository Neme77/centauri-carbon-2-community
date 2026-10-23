# CC2 Control 1.2.21 — prova cumulativa correttiva

Per Community Firmware V4.4/V4.4.1 e candidato V4.4.2, base ELEGOO 02.01.01.07.

Corregge il download degli aggiornamenti quando le intestazioni di sicurezza
di GitHub superano 8 KiB, mantenendo il limite totale di 16 KiB e tutte le
verifiche TLS. Le versioni precedenti richiedono questo primo caricamento del
file firmato dal PC; la correzione si applica ai download successivi.

Riconosce e conserva una precedente cartella webcam R2 rimasta inattiva dopo
un ripristino o una reinstallazione del firmware, verificando file e impronte
prima di reinstallarla. Componenti sconosciuti o avvii modificati restano
rifiutati. Il fix webcam R2 incluso mantiene le librerie già qualificate.

Installare con stampante connessa, inattiva e fredda, target di riscaldamento zero.
Alla prima attivazione del fix webcam serve un riavvio completo della stampante;
il solo riavvio di CC2 Control non basta. Nessun riavvio viene eseguito in
automatico. Conservare il percorso di backup indicato dall'installer.

Include le novità stabili già provate di Canvas, bobine, mappa oggetti, gestione
file e interfaccia. Canvas UDS, Quiet e retrazione estesa restano esperimenti
separati. Impostazioni, bobine e librerie piatti vengono conservate.

Il builder del firmware completo è un pacchetto separato: il firmware V4.4.2
va generato sul PC e provato sulla stampante prima della pubblicazione.

Grazie a tharje per la diagnosi webcam, a DamiBFryta, il nostro beta tester
definitivo, per prove e suggerimenti, e a Surfoo, Nick Plateringen-Paul e tutti
i contributor già accreditati nella cronologia del progetto.


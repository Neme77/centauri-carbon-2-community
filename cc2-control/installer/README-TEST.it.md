# Prova cumulativa CC2 Control — PR 208 e 225, R1

Pacchetto ricompilato dai sorgenti di develop e delle PR #208 e #225. Mantiene la versione 1.2.21: le prove non avanzano la versione pubblica.

Comprende tutte le integrazioni già unite: nuova Dashboard e live persistente, sperimentali in Integrazioni, asciugatura piatto opzionale, avviso di fine stampa, cronologia bobine compatta e indicazione firmware V4.4.2; aggiunge Spoolman opzionale (#208, tharje) e il ricordo del codice LAN nel browser (#225, richiesta Neme77). Rimangono i crediti storici e i ringraziamenti a DamiBFryta e agli altri contributor.

È una prova della sola applicazione: la patch webcam presente sulla stampante resta installata; questo pacchetto non la reinstalla e non impone un riavvio completo. Nessuna pubblicazione, modifica dei cataloghi o aggiornamento automatico.

## Installazione

Stampante inattiva e fredda. Estrai lo ZIP in una nuova cartella e usa Install-Windows.cmd (oppure Install-CC2-Control.ps1) su Windows; su Linux/macOS usa install-cc2-control.sh. Per la prova sulla stessa versione usa l'installer del computer: il pannello aggiornamenti mantiene il confronto di versione. Non caricare questo ZIP nel pannello.

Conserva il percorso del backup stampato dall'installer per il ripristino. Non cancellare i dati delle bobine o le impostazioni prima della prova.

## Controlli

- Dashboard, live locale/Matrix, Canvas, mesh, Orca e tracciamento bobine continuano a funzionare.
- Impostazioni → Connessione: il ricordo del codice LAN è spento inizialmente. Verifica e salva il codice, passa ad Aggiornamenti/Integrazioni e ricarica: il campo resta mascherato. Le azioni richiedono sempre conferma. Dimentica codice deve svuotare i campi, anche nelle altre schede aperte. Usalo soltanto su un dispositivo personale.
- Spoolman: senza configurazione il tracciamento resta locale. Per la prova completa occorre un server Spoolman reale. Configura l'integrazione, verifica importazione, collegamenti, colori/materiali/pesi e aggiornamento del consumo; una copia di una bobina deve avere un nuovo collegamento indipendente. Evita correzioni concorrenti dei pesi mentre controlli i valori.
- Verifica che un backup non contenga le credenziali del proxy; dopo ripristino Spoolman resta da riconfigurare. Non modificare il backup originale.
- Le funzioni sperimentali rimangono opzionali; non occorre attivarle per provare Spoolman o il codice LAN.

Riporta il risultato prima di procedere con i merge. Nessun test automatico sostituisce la prova sulla stampante con Spoolman reale.

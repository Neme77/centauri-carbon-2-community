# Installare Community Firmware V4.4.4 — Universal 07 con correzioni ungheresi

**Base ELEGOO 02.01.01.13 · CC2 Control 1.2.29 · Solo Centauri Carbon 2**

Dopo un ampio lavoro sulle diverse diramazioni degli aggiornamenti precedenti,
la procedura torna semplice: **copiare il firmware sulla USB e avviare Offline
Update dal display**. La migrazione dei componenti riconosciuti è automatica.

Non sono più richiesti normalmente probe, comandi SSH, pulizia manuale,
downgrade, aggiornamenti intermedi del pannello o ripristino preventivo Canvas.
Queste istruzioni sostituiscono quelle del vecchio kit di preparazione 4.4.4.

## File da utilizzare

Scaricare **[CC2_4.4.4_UNIVERSAL_07_HU_02.01.01.13.zip.sig](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.4/CC2_4.4.4_UNIVERSAL_07_HU_02.01.01.13.zip.sig)**
dalla [release V4.4.4](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.4).
SHA-256: `a140564016c3ca7402cb91e18434dc350c183b9cdf08a8b90e12c462d5978a2e`

È lo stesso pacchetto verificato Universal 07 HU, con un nome pubblico diverso.
Le correzioni ungheresi riguardano esclusivamente CC2 Control: non cambiano
le lingue del touchscreen, il backend o i componenti Canvas. La Recovery
separata non è inclusa e non è ancora pubblicata.

## Procedura

1. Terminare la stampa, lasciare raffreddare la stampante e azzerare i target.
   Completare eventuali operazioni Canvas e i riavvii già richiesti.
2. Conservare un backup verificato già disponibile. Se manca, salvare i dati
   del pannello dal menu **Impostazioni → Backup e ripristino** sul PC.
   È un backup selettivo, non un'immagine completa del disco.
3. Verificare lo SHA-256 del file scaricato e copiare il **.zip.sig integro
   nella directory principale della USB**. Non estrarlo.
4. Dal display scegliere **Impostazioni → Controlla aggiornamenti → Offline
   Update**, selezionare il file e confermare. Le etichette possono variare.
5. Attendere installazione e riavvio senza togliere alimentazione o USB.
6. Aprire `http://IP-STAMPANTE:8081` e verificare versione **1.2.29**,
   temperature, impostazioni, mesh, bobine, camera e riconoscimento Canvas
   se collegato. Provare poi una normale stampa.

Non occorre un reset di fabbrica. Wi-Fi, codice LAN, G-code e dati personali
rimangono fuori dai percorsi di migrazione. Conservare backup e archivi creati
dalla migrazione fino alla verifica del nuovo sistema.

## Canvas e versioni precedenti

Il Canvas non è obbligatorio. Il software RFID assente non significa che manchi
il dispositivo fisico. Se assente, il supporto viene installato inattivo;
non viene attivato automaticamente. Gli stati attivi, mai attivati e
ripristinati riconosciuti sono gestiti dalla migrazione. Non attivare o
ripristinare RFID soltanto per installare questo aggiornamento.

I percorsi provati comprendono 4.4.3 con pannello 1.2.25, 1.2.26, 1.2.27,
la variante privata 1.2.28 verificata e le preview .13 supportate.
Il controllo riconosce i componenti e i loro registri: la sola etichetta di
versione non rende compatibile qualsiasi variante privata.
Anche la prima installazione da originale .13 non richiede il probe.

## Assistenza

In caso di problemi [aprire una richiesta](https://github.com/Neme77/centauri-carbon-2-community/issues/new/choose):
siamo a disposizione per individuare il percorso corretto. Indicare versioni
di partenza, nome del pacchetto, messaggio e funzioni ancora disponibili.
Non pubblicare password, codici LAN o token. Conservare file, log e backup;
non cancellare registri per aggirare un rifiuto.

La procedura risolve i casi di incompatibilità finora identificati e verificati;
non promette compatibilità con qualsiasi stato sconosciuto o corrotto.
Per tornare al firmware originale, ripristinare prima il Canvas modificato
seguendo il suo percorso supportato: è un'operazione diversa da questo upgrade.

Grazie a **pezso92**, **beanbo**, ai tester della **#327** e a tutti coloro che
hanno verificato le diverse installazioni. I rapporti di compilazione e quelli
delle prove fisiche restano separati; la revisione madrelingua di questa esatta
proiezione del catalogo ungherese rimane da completare.

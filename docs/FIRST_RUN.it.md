# Prima installazione e codice di accesso LAN

[English](FIRST_RUN.md) · [OrcaSlicer e sync Canvas](ORCASLICER.it.md) · [Homepage](../README.md)

La guida riguarda il Community Firmware **V4.4.1 con CC2 Control 1.2.10**, basato sul firmware ufficiale **CC2 02.01.01.07**. La 4.4.1 è il rilascio completo del 14 ottobre 2026. Scaricare firmware, builder sorgente e checksum dalla [release V4.4.1](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.1).

**Compatibilità del solo CC2 Control:** questa release include l’applicazione nel firmware completo 4.4. Non installare separatamente questo CC2 Control sulla 4.3 o sul firmware originale. Impostazioni → Aggiornamenti è già incluso nella 4.4.1; gli update applicativi successivi mantengono la famiglia compatibile V4.4. [Guida agli update](UPDATE_CC2.it.md).

## 1. Scegliere il file corretto

Aprire i [rilasci GitHub del progetto](https://github.com/Neme77/centauri-carbon-2-community/releases) e leggere le note della versione da installare.

| File | A cosa serve |
| --- | --- |
| Firmware firmato con estensione `.zip.sig` | Installazione completa del firmware sulla stampante. |
| ZIP del builder | Compila il firmware sul computer; non si installa sulla stampante. |
| Aggiornamento del solo CC2 Control, se presente nella release | Aggiorna un'installazione compatibile del Community Firmware. Non installa il firmware completo. |
| File dei checksum SHA-256 | Permette di verificare i file scaricati. |

Usare i file della stessa release. La 4.4 utilizza la nuova base 02.01.01.07: builder e input firmware delle versioni precedenti non sono intercambiabili. Il progetto riguarda la **Centauri Carbon 2**, non la prima Centauri Carbon.

Prima dell’installazione, verificare il checksum da PowerShell:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath '.\CC2_V4_4_1_STOCK_20261014_205610_dd82af94.zip.sig'
```

Per il firmware V4.4.1 indicato sopra, lo SHA-256 atteso è `61356c3a83be374988d6154d429c27c403985974663d3780b194164e774a22c1`. Confrontarlo con [SHA256SUMS.txt](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.1/SHA256SUMS.txt). Se non coincide, riscaricare il file e non installarlo.

## 2. Installare e attendere l'avvio

1. Scaricare **`CC2_V4_4_1_STOCK_20261014_205610_dd82af94.zip.sig`** dalla [release V4.4.1](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.1) e verificarne il checksum. Il pacchetto completo include la base firmware; lo ZIP del builder sorgente non è un file di installazione.
2. A stampante inattiva, copiare il file firmato **`.zip.sig` integro nella cartella principale della chiavetta USB**, leggibile dalla stampante. Non estrarlo, non rinominarlo in `update.bin` e non copiare soltanto i file contenuti al suo interno.
3. Inserire la chiavetta nella stampante. Sul display aprire **Impostazioni → Verifica aggiornamenti → Aggiornamento offline** e selezionare il pacchetto firmato. Le etichette possono variare con la lingua del display. Confermare l’installazione sulla stampante.
4. Lasciare terminare l’installazione e il normale riavvio. Non interrompere l’alimentazione e lasciare inserita la chiavetta fino al completamento.
5. Se necessario, ricollegare la stampante alla rete locale, leggere l’indirizzo IP nella schermata di rete e attendere l’avvio dei servizi prima di aprire CC2 Control.
6. Dopo l’avvio verificare la base della stampante **02.01.01.07** e CC2 Control **1.2.10**. Il display mostra la versione della base vendor; **Community Firmware V4.4.1** è il nome del rilascio del progetto.

Il percorso USB è documentato nella [procedura OpenCentauri per CC2](https://docs.opencentauri.cc/software/updates-cc2/#updating-locally-via-usb).

Il ripristino di fabbrica non è necessario per questo aggiornamento. Credenziali CC2 Control, profili materiale e preferenze dell'interfaccia vengono normalmente conservati; il ripristino di fabbrica può cambiare il codice LAN.

## 3. Recuperare il codice LAN dal display

Sul display della stampante, aprire **Impostazioni → Solo LAN** e visualizzare il **codice di accesso LAN** attuale. Questo percorso è stato confermato dal maintainer sulla stampante della release.

Trascrivere il codice esattamente, rispettando maiuscole e minuscole. Attenzione a **O** e **0**, oppure **I** e **1**. Serve il codice LAN: non la password del Wi-Fi né il PIN/QR di associazione dell'account. Il seriale non va inserito manualmente: viene rilevato da CC2 Control.

Il computer o il telefono usato per configurare deve poter raggiungere la stampante sulla stessa rete locale.

## 4. Completare la prima configurazione

1. Aprire nel browser `http://IP-STAMPANTE:8081`, sostituendo `IP-STAMPANTE` con l'indirizzo appena letto. Esempio: `http://192.168.1.50:8081`.
2. Nella schermata iniziale inserire il codice LAN e premere **Collega la stampante**.
3. Attendere il salvataggio del codice, il riavvio del servizio CC2 Control e la sincronizzazione di stampante e Canvas. Le credenziali restano salvate localmente sulla stampante.
4. Se compare un messaggio di sincronizzazione ancora in corso, attendere e ricaricare la pagina.
5. Verificare **Stampante connessa** nella barra superiore, temperature visibili e slot corrispondenti al display nella pagina Canvas, se il Canvas è collegato.

Dopo una configurazione riuscita non serve un ulteriore spegnimento manuale. Una configurazione pulita richiede il codice LAN; un aggiornamento che conserva la configurazione può riconnettersi senza richiederlo.

## 5. Scegliere lingua e tema

Aprire **Impostazioni → Aspetto**. Le lingue dell'interfaccia sono **inglese, italiano, francese, cinese semplificato, russo, polacco, ceco e ungherese**. Temi: **Dark, Light, Dracula, Nord, Monokai e Solarized Light**. Le preferenze salvate sono condivise tra browser e pagina CC2 Control incorporata in OrcaSlicer.

## Aggiornamenti dal pannello

La V4.4.1 include già **Impostazioni → Aggiornamenti**: non occorre un secondo pacchetto applicativo dal computer. Controllare la disponibilità non richiede il codice; scaricare/caricare e installare richiedono la conferma con il codice LAN della stampante, anche dopo la prima configurazione. È diverso dalla password SSH. [Perché serve e dove trovarlo](UPDATE_CC2.it.md#perché-viene-richiesto-il-codice-lan). Gli update richiedono sempre una conferma. Il pannello aggiorna solo CC2 Control; il firmware completo usa la procedura USB descritta sopra.

## Se la pagina non si apre

Verificare che l’avvio sia terminato, che l’indirizzo IP non sia cambiato e che il browser utilizzi `http://IP-STAMPANTE:8081`. Il computer deve poter raggiungere quell’indirizzo locale. Se CC2 Control si apre ma rimane in attesa della stampante, ricontrollare il codice LAN attuale: un ripristino di fabbrica può averlo cambiato. Non ripristinare tutta la stampante soltanto per reinserire il codice.

## Se cambia il codice LAN

Aprire **Impostazioni → Connessione**, inserire il nuovo codice, premere **Cambia / Rivalida** e confermare. CC2 Control lo salva e riavvia il proprio servizio. Per il codice LAN questo pulsante completa la modifica: non c'è un ulteriore passaggio “Salva modifiche”.

Se non si riconnette, ricontrollare codice attuale sul display, IP e raggiungibilità della stampante. Un codice salvato non significa ancora che sia stato accettato: attendere **Stampante connessa**.

Per riavviare il servizio o recuperare una configurazione bloccata tramite SSH, consultare [la guida di installazione](INSTALL.md#recovery-if-the-old-code-blocks-configuration-access). Il reset della configurazione è un'opzione di recupero, non un passaggio normale della prima installazione.

## Riferimento per l’accesso SSH

Il Community Firmware permette l’accesso SSH locale per la manutenzione:

| Parametro | Valore predefinito |
| --- | --- |
| Utente | `root` |
| Password | La propria password SSH; [riferimento predefinito pubblicato](https://github.com/Neme77/centauri-carbon-2-community/blob/V4.4/docs/FIRST_RUN.it.md#riferimento-per-laccesso-ssh) |

Da PowerShell o da un terminale eseguire `ssh root@IP-STAMPANTE`, sostituendo `IP-STAMPANTE` con l’indirizzo locale della stampante, e inserire la password quando richiesta. Durante la digitazione la password non viene mostrata. Se la password di root è stata cambiata, utilizzare quella personalizzata. È la password SSH, distinta dal codice LAN usato da CC2 Control. Questi riferimenti riguardano il Community Firmware; SSH non serve per la normale configurazione dal browser.

## Tornare al firmware originale ELEGOO

[Scarica il firmware originale CC2 02.01.01.07](https://public.suchmeme.nl/cc2-02.01.01.07-1cbee81a985601d1944cea50773793ba-release-abroad.zip.sig). Il file è ospitato su un mirror esterno, non su un server ELEGOO. I suoi byte coincidono con la base ufficiale verificata dal builder V4.4.

SHA-256: `278a0606a072ae432e9bf2d4f0157acba4570d1c85dbc768c359fef53ac5f406`.

**ELEGOO ha bloccato il downgrade alle versioni precedenti.** Questo link fornisce il firmware originale della **stessa versione 02.01.01.07**; non permette di tornare alla 02.01.00.00 o a versioni più vecchie. Reinstallare il firmware originale è diverso dal ripristino di fabbrica: quest’ultimo non sostituisce il firmware installato.

Utilizzare la stessa procedura USB/aggiornamento offline descritta sopra con il pacchetto originale. L’identità del file è verificata e il maintainer ha confermato sulla stampante il ritorno dalla V4.4 al firmware originale della stessa versione. Se la stampante rifiuta il pacchetto, fermarsi e annotare il messaggio; non modificare la versione del file né aggirare i controlli. Il firmware originale non include CC2 Control o le funzioni community.

## Passo successivo

Seguire [Configurazione OrcaSlicer e sincronizzazione Canvas](ORCASLICER.it.md), poi caricare un piccolo file già affettato e confermare la mappatura dei filamenti prima di stampare.

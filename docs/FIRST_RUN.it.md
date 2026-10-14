# Prima installazione e codice di accesso LAN

[English](FIRST_RUN.md) · [OrcaSlicer e sync Canvas](ORCASLICER.it.md) · [Homepage](../README.md)

La guida riguarda il Community Firmware **V4.4 con CC2 Control 1.2.3**, basato sul firmware ufficiale **CC2 02.01.01.07**. La 4.4 è stata pubblicata il 13 ottobre 2026. Scaricare firmware, builder sorgente e checksum dalla [release V4.4](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4).

**Compatibilità del solo CC2 Control:** questa release include l’applicazione nel firmware completo 4.4. Non installare separatamente il nuovo CC2 Control sulla 4.3; l’updater della sola applicazione sarà pubblicato in seguito.

## 1. Scegliere il file corretto

Aprire i [rilasci GitHub del progetto](https://github.com/Neme77/centauri-carbon-2-community/releases) e leggere le note della versione da installare.

| File | A cosa serve |
| --- | --- |
| Firmware firmato con estensione `.zip.sig` | Installazione completa del firmware sulla stampante. |
| ZIP del builder | Compila il firmware sul computer; non si installa sulla stampante. |
| Aggiornamento del solo CC2 Control, se presente nella release | Aggiorna un'installazione compatibile del Community Firmware. Non installa il firmware completo. |
| File dei checksum SHA-256 | Permette di verificare i file scaricati. |

Usare i file della stessa release. La 4.4 utilizza la nuova base 02.01.01.07: builder e input firmware delle versioni precedenti non sono intercambiabili. Il progetto riguarda la **Centauri Carbon 2**, non la prima Centauri Carbon.

Per verificare facoltativamente il checksum da PowerShell:

```powershell
Get-FileHash -Algorithm SHA256 -LiteralPath '.\NOME-FIRMWARE.zip.sig'
```

Confrontare il risultato con il checksum pubblicato per quel preciso file firmware.

## 2. Installare e attendere l'avvio

1. A stampante inattiva, installare il file firmato `.zip.sig` attraverso la procedura di aggiornamento supportata dalla stampante, seguendo le istruzioni della release. Conservare il file firmato integro.
2. Lasciare terminare l'aggiornamento e il normale riavvio. Non interrompere l'alimentazione durante l'aggiornamento.
3. Se necessario, ricollegare la stampante alla rete locale e leggere il suo **indirizzo IP** nella schermata di rete del display.
4. Attendere l'inizializzazione dei servizi prima di configurare CC2 Control. Il suo avvio è ritardato per consentire ai servizi originali di partire.

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

Aprire **Impostazioni → Aspetto**. Le lingue dell'interfaccia sono **inglese, italiano, francese, cinese semplificato, russo, polacco e ceco**. Temi: **Dark, Light, Dracula, Nord, Monokai e Solarized Light**. Le preferenze salvate sono condivise tra browser e pagina CC2 Control incorporata in OrcaSlicer.

## Se cambia il codice LAN

Aprire **Impostazioni → Connessione**, inserire il nuovo codice, premere **Cambia / Rivalida** e confermare. CC2 Control lo salva e riavvia il proprio servizio. Per il codice LAN questo pulsante completa la modifica: non c'è un ulteriore passaggio “Salva modifiche”.

Se non si riconnette, ricontrollare codice attuale sul display, IP e raggiungibilità della stampante. Un codice salvato non significa ancora che sia stato accettato: attendere **Stampante connessa**.

Per riavviare il servizio o recuperare una configurazione bloccata tramite SSH, consultare [la guida di installazione](INSTALL.md#recovery-if-the-old-code-blocks-configuration-access). Il reset della configurazione è un'opzione di recupero, non un passaggio normale della prima installazione.

## Passo successivo

Seguire [Configurazione OrcaSlicer e sincronizzazione Canvas](ORCASLICER.it.md), poi caricare un piccolo file già affettato e confermare la mappatura dei filamenti prima di stampare.

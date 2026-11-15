# Prima installazione e codice LAN

[English](FIRST_RUN.md) · [Guida aggiornamento](FIRMWARE_UPGRADE.it.md) · [Homepage](../README.it.md)

Release attuale: **V4.4.4 Universal 07 con correzioni ungheresi**, base
**ELEGOO 02.01.01.13**, pannello **CC2 Control 1.2.29**.

## 1. Scaricare

Usare [CC2_4.4.4_UNIVERSAL_07_HU_02.01.01.13.zip.sig](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.4/CC2_4.4.4_UNIVERSAL_07_HU_02.01.01.13.zip.sig) dalla [release V4.4.4](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.4).
SHA-256: `a140564016c3ca7402cb91e18434dc350c183b9cdf08a8b90e12c462d5978a2e`.
È firmware completo: non usare il pacchetto sorgente o un updater del solo pannello.

## 2. Installare

Seguire la [procedura aggiornata](FIRMWARE_UPGRADE.it.md): terminare la stampa,
conservare il backup, copiare il file integro sulla USB e scegliere **Offline
Update** dal menu aggiornamenti del display.

La prima installazione da originale .13 non richiede il probe. Anche gli
aggiornamenti Community riconosciuti vengono migrati automaticamente:
non servono normalmente script, pulizia, downgrade o ripristino Canvas.
Completare prima le eventuali operazioni Canvas in corso e i riavvii richiesti.
Non occorre un reset di fabbrica.

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


## Lingua, aggiornamenti e assistenza

Per la lingua e il tema di CC2 Control usare **Impostazioni → Aspetto**.
Le correzioni ungheresi riguardano il pannello web, non il touchscreen.
Il firmware completo si installa con Offline Update; gli aggiornamenti del
solo pannello devono dichiarare compatibilità con la base installata.

Se la pagina non si apre, attendere l'avvio e verificare l'IP. Se si apre ma
non si collega, controllare il codice LAN senza resettare la stampante.
In caso di problemi conservare log e backup e [chiedere assistenza](https://github.com/Neme77/centauri-carbon-2-community/issues/new/choose).
La [guida completa](FIRMWARE_UPGRADE.it.md) distingue l'upgrade dal ritorno
al firmware originale.

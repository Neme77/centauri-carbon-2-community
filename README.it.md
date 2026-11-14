# Centauri Carbon 2 Community Firmware

Firmware e controllo locale per la **ELEGOO Centauri Carbon 2**, mantenuti da
**Neme77** con il contributo di sviluppatori e tester.

[English](README.md) · [Discord](https://discord.gg/FGfCaFpm38) ·
[Facebook](https://www.facebook.com/groups/1658610779139107) ·
[Assistenza](https://github.com/Neme77/centauri-carbon-2-community/issues/new/choose)

## V4.4.4 Universal 07 · ELEGOO 02.01.01.13 · CC2 Control 1.2.29

**L'installazione torna semplice: firmware sulla USB e Offline Update.**

Un ampio lavoro sulle diverse diramazioni degli aggiornamenti precedenti ha
permesso di automatizzare la migrazione dei componenti riconosciuti.
La Universal 07 risolve i casi di incompatibilità finora identificati e
verificati, conservando i dati personali. Include le correzioni ungheresi
di CC2 Control provenienti dalla PR #352.

**Non sono più obbligatori probe, pulizia manuale, downgrade, aggiornamenti
intermedi del pannello o ripristino Canvas per un upgrade supportato.**
Terminare prima eventuali operazioni Canvas in corso e i riavvii richiesti.
Il Canvas è facoltativo; gli stati RFID riconosciuti vengono gestiti automaticamente.

| Download | Contenuto |
| --- | --- |
| [Firmware Universal 07 con correzioni ungheresi](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.4/CC2_4.4.4_UNIVERSAL_07_HU_02.01.01.13.zip.sig) | File .zip.sig completo da copiare integro sulla USB. |
| [Prima installazione](docs/FIRST_RUN.it.md) · [Guida aggiornamento](docs/FIRMWARE_UPGRADE.it.md) | Procedura, backup e verifiche dopo l'avvio. |
| [Release e checksum](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.4) | Identità del file, sorgenti e prove. |

Usare il nuovo file **Universal 07**. Il vecchio pacchetto RC6 e il kit di
preparazione sono storici: le loro istruzioni non si applicano alla Universal.
La Recovery separata non è ancora pubblicata. Gli aggiornamenti del solo
pannello rimangono distinti dal firmware completo.

## L'esperienza CC2 Control sulla nuova base

- **Display:** Home e Rete Community, palette e integrazione offset Z.
- **Dashboard:** camera, stampa, temperature, ventole, offset Z effettivo,
  mesh attiva e lato del piatto.
- **Stampe:** file USB/interni, anteprime, storico, coda e percorsi G-code del
  layer. Per accedere alla mappa estesa attuale abilitare le etichette oggetto
  in OrcaSlicer; il parser dei percorsi non richiede quelle etichette.
- **Calibrazione:** mesh, libreria piatti, livellamento viti e storico, PID,
  Pressure Advance, Input Shaper, Rotation Distance e cella di carico.
- **Materiali:** Canvas, bobina esterna/TPU, inventario, Spoolman e assegnazione
  RFID con revisione dei dati prima della conferma.
- **Camera e rete:** video locale e remoto, compatibilità Matrix e slicer,
  Wi-Fi e adattatori Ethernet USB RTL8153 qualificati.
- **Funzioni native:** Reactor, minimo EXHAUST, flap e pullback Canvas fino
  a 1000 mm, mantenendo il valore originale finché non viene modificato.
- **Pannello:** undici lingue, temi, backup selettivo, diagnostica, Console,
  aggiornamenti con log e integrazioni Panda/Dragon Breath.

Le funzioni dipendono dall'hardware e dallo stato della stampante.
[Guida](docs/CC2_CONTROL.md) · [Immagini](docs/SCREENSHOTS.md) ·
[Cronologia](CHANGELOG.md).

## Canvas RFID facoltativo

L'attivazione si richiede da **Impostazioni → Integrazioni**, seguendo i
riavvii e le verifiche. È disponibile anche il ripristino originale.
Il componente assente viene installato inattivo: l'upgrade non attiva RFID.
Il Canvas non serve per stampare con la bobina esterna.

Le famiglie di tag provate non equivalgono alla compatibilità con ogni marca.
Il progetto resta aperto a nuovi formati verificabili.
Attribuzione dei decoder: **OpenRFID / DnG-Crafts**.

## Prove e limiti dichiarati

Le prove del maintainer e indipendenti comprendono 1.2.25, 1.2.26, 1.2.27,
la variante privata 1.2.28 verificata e le preview .13 supportate.
Sono stati riportati avvio, temperature, riconoscimento Canvas, dati conservati
e controlli di stampa, camera e rete. Le letture RFID sono state provate dove
erano disponibili tag compatibili; armare il lettore non equivale a leggere un tag.

La variante ungherese modifica soltanto il catalogo web: backend e migrazione
rimangono identici alla 07. Le prove offline sono distinte da quelle fisiche;
la revisione madrelingua dell'esatto catalogo proiettato rimane da completare.
Non dichiariamo una percentuale statistica né compatibilità con ogni variante
privata o registro corrotto.

## Se serve aiuto, ci siamo

Conservare i backup verificati. Se mancano, salvare i dati dal menu del pannello
**Backup e ripristino** prima dell'installazione. La migrazione conserva gli
archivi dei componenti sulla stampante.

[Aprire una richiesta](https://github.com/Neme77/centauri-carbon-2-community/issues/new/choose) indicando versioni iniziali,
file utilizzato e messaggio visualizzato. Non pubblicare password, codici LAN
o token. Conservare i registri e non ripetere installazioni per forzare un rifiuto:
vi aiuteremo a individuare il percorso corretto.

Il ritorno al firmware originale è distinto da questo upgrade e richiede
il percorso Canvas appropriato. Non promettiamo recupero senza rischi per
qualsiasi guasto. La Recovery separata è ancora in lavorazione.

## Aggiornamenti futuri e contributi

Le funzioni compatibili del pannello continueranno con il normale updater.
Le modifiche native con dubbi di compatibilità diventeranno una nuova base
con migrazione documentata. Gli autori delle PR devono documentare e provare
il proprio lavoro secondo [linee guida](CONTRIBUTING.md),
[regole](AGENTS.md) e [test](docs/TESTING.md).

## Community e crediti

Grazie a **Surfoo, beanbo, efiten, Skcycos, pezso92, asiersan, Noahwhite2014,
tharje, Nick Plateringen-Paul, lpgamerzockt-ops, BorjaLL** e a tutti i contributor.
Grazie a **DamiBFryta** per le prove TPU; **pezso92** e **beanbo** per le
traduzioni ungheresi; **SVLoneStar**, **Natan**, **arsian** e i tester delle migrazioni.

Grazie ai tester della **#327**: **Eldurinn, kalross, jgj1107, matefon, SVLoneStar,
fechy, RoisinDubh79, lpgamerzockt-ops e Noahwhite2014**.
[Crediti completi](docs/CONTRIBUTORS.md) · [Prove upgrade](docs/UPGRADE_CREDITS.md).
Integrazione firmware e prove estese: **Neme77**.

## Un caffè per il progetto

Se il mio lavoro ti piace e vuoi offrirmi un caffè, il contributo facoltativo
aiuta sviluppo, prove ed esplorazione di altre stampanti. Non è necessario
donare per usare il progetto e non promette compatibilità con modelli non provati.

[Supporta il progetto con PayPal](https://www.paypal.com/cgi-bin/webscr?cmd=_donations&business=gino.nencioni%40gmail.com&item_name=CC2%20Community%20Firmware&currency_code=EUR)

## Sorgenti e licenze

Progetto indipendente, non affiliato a ELEGOO, dedicato alla Centauri Carbon 2.
Codice Community e componenti vendor mantengono le rispettive licenze.
[LICENSE](LICENSE) · [NOTICE](NOTICE.md) · [Sorgenti](docs/SOURCE.md).

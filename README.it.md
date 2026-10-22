# Centauri Carbon 2 Community Firmware

Firmware community, controllo locale e strumenti per costruire il firmware
della **ELEGOO Centauri Carbon 2**, mantenuti da **Neme77**.

[English](README.md) · [Documentazione](docs/README.md) · [Download](https://github.com/Neme77/centauri-carbon-2-community/releases) · [Immagini](docs/SCREENSHOTS.md)

> Progetto indipendente, non affiliato né supportato da ELEGOO.
> È dedicato alla **Centauri Carbon 2**, non alla prima Centauri Carbon.

## Da dove iniziare

| Cosa vuoi fare | Guida |
| --- | --- |
| Installare il firmware community completo | [Prima installazione](docs/FIRST_RUN.it.md) |
| Aggiornare CC2 Control già installato | [Aggiornamenti dell’app](docs/UPDATE_CC2.it.md) |
| Collegare OrcaSlicer e sincronizzare gli slot Canvas | [Configurazione OrcaSlicer](docs/ORCASLICER.it.md) |

Scarica i file dalle [release GitHub](https://github.com/Neme77/centauri-carbon-2-community/releases)
e segui la guida e le note corrispondenti. **Firmware completo**, **applicazione
CC2 Control** e **builder dei sorgenti** sono download diversi: lo ZIP del
builder non si installa sulla stampante. Verifica i checksum SHA-256 forniti.

La famiglia firmware attualmente qualificata è **V4.4/V4.4.1**, sulla base
ELEGOO **02.01.01.07**. I suoi aggiornamenti dell’app non sono intercambiabili
con firmware originale, versioni community precedenti o build sperimentali.

Apri **`http://IP-STAMPANTE:8081`** nella rete locale. Anche in OrcaSlicer
l’indirizzo deve terminare con **`:8081`**. Per installare gli aggiornamenti
serve il **codice LAN** della stampante, diverso dalla password SSH; per
cercarli non serve. L’installazione richiede sempre conferma. Gli aggiornamenti
del componente webcam richiedono un **riavvio completo della stampante**,
come indicato nelle rispettive note.

## Cosa offre CC2 Control

- Dashboard locale, video live, temperature, ventole, velocità e flusso di stampa.
- File interni/USB, anteprime dei modelli ed esclusione interattiva degli oggetti.
- Stati Canvas, materiali, auto refill e sincronizzazione filamenti con OrcaSlicer.
- Livellamento, mesh per piatti/ugelli e inventario/tracciamento bobine opzionale.
- Backup e ripristino selettivo, diagnostica, Console protetta e update firmati.
- Più lingue, temi e interfaccia per computer e telefono.

Consulta la [guida dell’applicazione](docs/CC2_CONTROL.md) e la
[galleria](docs/SCREENSHOTS.md). Le funzioni sperimentali richiedono una scelta
esplicita e una validazione separata.

## Aiuto e sviluppo

[Indice delle guide](docs/README.md) · [Segnala un problema](https://github.com/Neme77/centauri-carbon-2-community/issues) · [Contribuire](CONTRIBUTING.md) · [Compilazione](docs/BUILD.md) · [Sorgenti e provenienza](docs/SOURCE.md)

La cronologia delle versioni è nel [CHANGELOG](CHANGELOG.md) e nelle
[note di rilascio](https://github.com/Neme77/centauri-carbon-2-community/releases).
Le istruzioni dettagliate e la compatibilità sono nelle guide collegate sopra.

## Community

Grazie a **Surfoo, beanbo, efiten, Skcycos, pezso92, asiersan, Noahwhite2014,
tharje, Nick Plateringen-Paul** e a tutti i contributor, traduttori e tester.
Un ringraziamento speciale a **[DamiBFryta](https://www.reddit.com/user/DamiBFryta/),
il nostro beta tester definitivo**. [Crediti completi e contributi originali](docs/CONTRIBUTORS.md).

Il lavoro assistito dall’IA viene revisionato e verificato. Architettura, prove
sulla stampante e approvazione delle release restano al maintainer e ai tester.

## Licenza

Il codice del progetto è GPL-3.0-only nei limiti dei diritti dei contributor.
Firmware vendor e componenti di terze parti mantengono le proprie condizioni.
Vedi [LICENSE](LICENSE) e [NOTICE.md](NOTICE.md).

# Centauri Carbon 2 Community Firmware

Firmware community, controllo locale e strumenti per costruire il firmware
della **ELEGOO Centauri Carbon 2**, mantenuti da **Neme77**.

<p align="center">
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.2/CC2_V4_4_2_STOCK_20261024_125406_99842c8a.zip.sig"><img src="https://img.shields.io/badge/SCARICA-Firmware%20V4.4.2-087f8c?style=for-the-badge&amp;logo=github&amp;logoColor=white" alt="Scarica il firmware completo V4.4.2"></a>
  &nbsp;
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.21-V4.4/CC2-Control-1.2.21-Multiplatform-Update.zip"><img src="https://img.shields.io/badge/SCARICA-CC2%20Control%201.2.21-2563eb?style=for-the-badge&amp;logo=github&amp;logoColor=white" alt="Scarica il pacchetto multipiattaforma CC2 Control 1.2.21"></a>
</p>

| **🖨️ Firmware completo · V4.4.2** | **🔄 CC2 Control · 1.2.21** |
| --- | --- |
| [**Scarica il firmware firmato**](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.2/CC2_V4_4_2_STOCK_20261024_125406_99842c8a.zip.sig) · [Note e checksum](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.2) | [**Scarica il pacchetto Windows / macOS / Linux**](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.21-V4.4/CC2-Control-1.2.21-Multiplatform-Update.zip) · [Note e checksum](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.21-V4.4) |
| Installazione completa con CC2 Control **1.2.21** e patch webcam già inclusa. Nessun riavvio aggiuntivo dopo il firmware. | Per firmware community **V4.4/V4.4.1/V4.4.2**. Include i fix di stabilità e recupero della webcam. |

**Aggiorni dal pannello di CC2 Control?** [Scarica il file firmato `.cc2update`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.21-V4.4/CC2-Control-1.2.21-V4.4.cc2update) e selezionalo in **Impostazioni → Aggiornamenti**. Aggiornamento della sola app: riavvio completo solo alla prima attivazione del fix webcam, già presente dalla 1.2.19. Il firmware completo V4.4.2 non richiede un riavvio aggiuntivo.

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

La famiglia firmware attualmente qualificata è **V4.4/V4.4.1/V4.4.2**, sulla base
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

## Novità di V4.4.2 / CC2 Control 1.2.21

Il firmware completo include l'applicazione aggiornata e il fix webcam dall'avvio.
Chi usa già V4.4/V4.4.1 può installare il solo aggiornamento di CC2 Control.

### Nuovo Canvas

Schede slot compatte, azioni condivise e pannelli impostazioni/cronologia bilanciati rendono Canvas più leggibile su PC e telefono. Il caricamento di recupero permette di risolvere uno stato della testina non assegnato dopo un riavvio: scegli uno slot presente e conferma il caricamento nativo. La cronologia osservata è limitata alla sessione e si azzera ricaricando la pagina. Layout ispirato alla proposta di **Surfoo**, rifinito con le prove e le scelte del maintainer.

![Nuovo layout Canvas](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.21-V4.4/Canvas-1.2.21.png)

### Funzioni sperimentali indipendenti

**Canvas UDS** e **controlli motori Quiet** si abilitano separatamente in Impostazioni → Aggiornamenti. Partono disattivati sulle nuove installazioni; le scelte esistenti vengono conservate. UDS mantiene il ripiego MQTT. Quiet/Normal deriva dal contributo di **BorjaLL**: i comandi dello slicer possono sovrascrivere i limiti iniziali. Completa Normal prima di disattivare i controlli Quiet o cambiare sorgente Canvas. Oltre 350 mm rimane necessario il runtime firmware separato e qualificato.

![Impostazioni sperimentali indipendenti](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.21-V4.4/Experiments-1.2.21.png)

*Immagini dell'interfaccia effettiva con dati dimostrativi simulati.*

### Tutte le migliorie della release

- **Aggiornamenti affidabili:** corretti i download GitHub con intestazioni di sicurezza superiori a 8 KiB, mantenendo il limite totale di 16 KiB e i controlli TLS/redirect; risolto l'errore HTTPS/orologio fuorviante (#214, segnalazione di **efiten**, #215).
- **Webcam:** recupero dei thread client e delle notifiche per video locale/Matrix dall'avvio di V4.4.2 e negli aggiornamenti cumulativi, anche saltando versioni. Migrazione corretta delle installazioni verificate già presenti o inattive; modifiche di avvio sconosciute restano rifiutate. Diagnosi di **tharje**.
- **Aggiornamento unificato:** un solo flusso stabile; chi proviene dalla precedente sperimentale riceve lo stesso pacchetto 1.2.21. Le opzioni restano indipendenti dagli update.
- **Canvas:** nuovo layout e azioni condivise (#207), caricamento di recupero con conferma (#190), telemetria UDS facoltativa (#200).
- **Quiet/Normal:** attivazione separata, conferma LAN, controlli stampante inattiva/fredda e indicazioni per riconfermare Normal dopo una riconnessione (contributo originale di **BorjaLL**, #179).
- **File:** accesso anche oltre i primi 128 file e caricamenti in streaming fino a **512 MiB** (#197).
- **Esclusione oggetti:** una sola mappa 2D rifinita, selezione collegata e ID G-code originali conservati (#199). Idea/prototipo di **Nick Plateringen-Paul**, rifinitura suggerita da **DamiBFryta**.
- **Popup nuova bobina:** copia indipendente senza spostare la bobina originale e calcolo del filamento residuo da peso totale meno tara, mantenendo separata la capacità nominale (#210), su suggerimento di **DamiBFryta**.
- **Interfaccia:** spaziature e titoli uniformi, focus da tastiera e accessibilità (#196); download evidenziati nella homepage (#201).
- **Funzioni conservate:** backup/ripristino, mesh per piatti/ugelli, Orca, lingue, video persistente, diagnostica e campanella aggiornamenti.
- **Riavvio:** necessario per la sola app soltanto alla prima attivazione del fix webcam, già incluso dalla 1.2.19. Il firmware V4.4.2 si riavvia durante l'installazione e non richiede un riavvio aggiuntivo.

[Note complete, checksum e crediti](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.21-V4.4).
Spoolman, il restyling della dashboard, asciugatura bobine e popup di fine stampa restano fuori da questa release.

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

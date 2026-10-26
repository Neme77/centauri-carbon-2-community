# Centauri Carbon 2 Community Firmware

Firmware community, controllo locale e strumenti per costruire il firmware
della **ELEGOO Centauri Carbon 2**, mantenuti da **Neme77**.

<p align="center">
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.2/CC2_V4_4_2_STOCK_20261024_125406_99842c8a.zip.sig"><img src="https://img.shields.io/badge/SCARICA-Firmware%20V4.4.2-087f8c?style=for-the-badge&amp;logo=github&amp;logoColor=white" alt="Scarica il firmware completo V4.4.2"></a>
  &nbsp;
  <a href="https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.22-V4.4/CC2-Control-1.2.22-Multiplatform-Update.zip"><img src="https://img.shields.io/badge/SCARICA-CC2%20Control%201.2.22-2563eb?style=for-the-badge&amp;logo=github&amp;logoColor=white" alt="Scarica il pacchetto multipiattaforma CC2 Control 1.2.22"></a>
</p>

| **🖨️ Firmware completo · V4.4.2** | **🔄 CC2 Control · 1.2.22** |
| --- | --- |
| [**Scarica il firmware firmato**](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V4.4.2/CC2_V4_4_2_STOCK_20261024_125406_99842c8a.zip.sig) · [Note e checksum](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V4.4.2) | [**Scarica il pacchetto Windows / macOS / Linux**](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.22-V4.4/CC2-Control-1.2.22-Multiplatform-Update.zip) · [Note e checksum](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.22-V4.4) |
| Installazione completa con CC2 Control **1.2.21** e patch webcam già inclusa. Nessun riavvio aggiuntivo dopo il firmware. | Per firmware community **V4.4/V4.4.1/V4.4.2**. Include i fix di stabilità e recupero della webcam. |

> **Sperimentale facoltativa · Retrazione Canvas R4:** [**Scarica il kit Windows / Linux / macOS**](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.22-V4.4/CC2-Canvas-Pullback-Experimental-R4.zip). Estende il limite configurabile fino a 1000 mm sui runtime qualificati **V4.4.1/V4.4.2**, con Control **1.2.21/1.2.22**. Di **Noahwhite2014**. [Istruzioni](experimental/canvas-pullback-kit/README.it.md). Rimuovere il componente e riavviare prima degli update app/firmware.

**Aggiorni dal pannello di CC2 Control?** [Scarica il file firmato `.cc2update`](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.22-V4.4/CC2-Control-1.2.22-V4.4.cc2update) e selezionalo in **Impostazioni → Aggiornamenti**. Aggiornamento della sola app: riavvio completo solo alla prima attivazione del fix webcam, già presente dalla 1.2.19. Il firmware completo V4.4.2 non richiede un riavvio aggiuntivo.

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

## Novità di CC2 Control 1.2.22

> **Piccolo errore di testo nella 1.2.22:** la pagina Aggiornamenti riporta ancora «Le funzioni sperimentali si scelgono separatamente qui sotto». Si trovano invece in **Impostazioni → Integrazioni**. Ci scusiamo per l’indicazione imprecisa: le funzioni operano regolarmente. Correggeremo il testo nel prossimo aggiornamento.

- **Bobina esterna, anche per chi acquista la CC2 senza Combo:** comandi nativi **Carica / Scarica** quando Canvas è assente e modifica **Marchio → Tipo → Sottocategoria → Colore**. Canvas condivide catalogo originale e palette di 24 colori (#233). L’update non abilita la bobina esterna con Canvas collegato.
- **Dashboard:** sfondo camera di **pezso92**, slider velocità/flow con campi numerici Applica/Ripristina, riquadri allineati e **Mantieni attiva** nell’intestazione camera (#223).
- **Controllo:** azioni macchina compatte, Z offset sopra la regolazione stampa e calibrazioni allineate (#226).
- **Spoolman:** sincronizzazione facoltativa di **tharje** (#208), selettore indipendente in Integrazioni che conserva configurazione e tracciamento locale (#227).
- **Codice LAN:** salvataggio facoltativo verificato nel browser, campi mascherati precompilati e Dimentica (#225).
- **Fine stampa:** avviso non bloccante con file/durata, Conferma e Stampa di nuovo; percentuale azzerata quando inattiva senza home (#219).
- **Cronologia bobine:** pannello scorrevole con filtri Tutti / Solo consumi, suggerito da **DamiBFryta** (#220).
- **Integrazioni sperimentali:** UDS, Quiet e riscaldamento piatto in Impostazioni → Integrazioni, indipendenti e con azioni allineate (#223/#232).
- **Esperimento asciugatura:** riscaldamento temporizzato, arresto e conferma target zero (#198/#218/#230); richiesta di **romeopuch**, diagnosi di **Noahwhite2014**, documentazione di **pezso92** (#235).
- **Aggiornamenti e pacchetti:** requisiti corretti nelle nove lingue (#222), rimosso overlay web inutilizzato (#229). Fix webcam cumulativo, preferenze, mesh e backup conservati.



[![Dashboard](docs/images/cc2-control/dashboard-current.png)](https://raw.githubusercontent.com/Neme77/centauri-carbon-2-community/main/docs/images/cc2-control/dashboard-current.png)

[![Control](docs/images/cc2-control/control-current.png)](https://raw.githubusercontent.com/Neme77/centauri-carbon-2-community/main/docs/images/cc2-control/control-current.png)

### Retrazione Canvas estesa facoltativa

[**Kit sperimentale separato Windows / Linux / macOS**](https://github.com/Neme77/centauri-carbon-2-community/releases/download/CC2-Control-1.2.22-V4.4/CC2-Canvas-Pullback-Experimental-R4.zip), di **Noahwhite2014** (#228). Qualificato solo per **V4.4.1/V4.4.2**, con Control **1.2.21/1.2.22**. Estende il limite configurabile fino a 1000 mm senza scegliere la distanza né inviare movimenti; il valore adatto dipende dal percorso. **Rimuovere e riavviare prima degli update app/firmware.** Non installato dall’updater automatico. [Istruzioni](experimental/canvas-pullback-kit/README.it.md).

[![Nuovo layout Canvas](docs/images/cc2-control/canvas-current.png)](https://raw.githubusercontent.com/Neme77/centauri-carbon-2-community/main/docs/images/cc2-control/canvas-current.png)

[![Impostazioni sperimentali indipendenti](docs/images/cc2-control/experiments-current.png)](https://raw.githubusercontent.com/Neme77/centauri-carbon-2-community/main/docs/images/cc2-control/experiments-current.png)

Canvas ispirato a **Surfoo**; Quiet originale di **BorjaLL**. Le sperimentali mantengono le scelte. Anteprime con dati simulati. Firmware V4.4.2 e builder includono ancora Control 1.2.21: le nuove funzioni si ottengono con questo update dell’app.

[Release notes / Note complete, validation, checksums and credits](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.22-V4.4).

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

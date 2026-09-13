# Centauri Carbon 2 Community

Progetto indipendente di Neme77 per ELEGOO Centauri Carbon 2. Non affiliato né supportato da ELEGOO.

[English](README_EN.md) · [Installazione USB](docs/INSTALLAZIONE.md) · [Test](docs/TEST.md) · [Builder](docs/BUILD.md)

## 📦 Download firmware

### Centauri Carbon 2 Community V3.7

⬇️ **[DOWNLOAD FIRMWARE V3.7](https://github.com/Neme77/centauri-carbon-2-community/releases/download/V3.7/CC2_FULL_V3_7_STOCK_BOOTSTRAP.zip.sig)**

**Base verificata:** ELEGOO firmware 02.01.00.00  
**Dimensione:** 129.897.560 byte

**SHA-256**  
`149be825757593323aeca8ffd8e8bb3a86354dbedfab7c14e60298b3bb942f7f`

➡️ [Note della release V3.7](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/V3.7)

## Compatibilità

Base verificata: **02.01.00.00**, pacchetto `cc2_eeb001_02.01.00.00_20260707170825.zip.sig`.
Non è stata verificata la compatibilità con Centauri 2, altre versioni o tutte le revisioni hardware.

## Modifiche

- Ripristino del servizio SSH con accesso root.
- Patch Z-offset della GUI.
- Dual Trust v2: verifica delle firme stock e della chiave community del progetto.
- HTTP locale sulla porta 80 anche nel percorso WAN/cloud.
- Upload v1 durante la stampa, collaudato con OrcaSlicer in modalità LAN.

Elegoo Slicer può impedire l'upload lato client quando la stampante è occupata. Utilizzare OrcaSlicer LAN con il codice/password della stampante e il solo upload. La modifica non aggiunge una coda di stampa né avvia automaticamente il file caricato.

## Stato

Test riusciti di installazione da originale, ritorno all'originale e aggiornamento community. Vedere la matrice dei test per limiti ed evidenze.

È un **firmware modificato firmato stock**, installabile dall'originale 02.01.00.00 collaudato e verificabile anche dal nostro Dual Trust. Include già tutte le modifiche elencate, **Dual Trust v2 compreso**. Questa release distribuisce il solo pacchetto firmato stock.

SHA-256 del pacchetto collaudato:

```text
149be825757593323aeca8ffd8e8bb3a86354dbedfab7c14e60298b3bb942f7f
```

Il repository contiene sorgenti e documentazione, non l'immagine firmware. Il builder richiede componenti esterni; vedere la guida di build. Non servono chiavi private per installare il firmware già firmato.

## Firma e accesso

La prima installazione utilizza il pacchetto firmato stock; gli aggiornamenti community richiedono il Dual Trust corrispondente già attivo. Il firmware originale rimuove le modifiche dal sistema aggiornato: non presumere di conservare SSH o Dual Trust dopo il ritorno a stock.

Chiavi private, chiave AES, credenziali e immagini firmware non fanno parte del repository. La chiave pubblica community è in `cc2_builder_v3_7/dualtrust/cc2_community_release_public.pem`.

## Licenze e attribuzioni

I contributi originali di codice del progetto sono distribuiti sotto **GNU GPL versione 3**; vedere [LICENSE](LICENSE) e [NOTICE.md](NOTICE.md) per l'ambito. Licenze e attribuzioni preesistenti dei componenti di terzi rimangono applicabili. La licenza del repository non attribuisce una nuova licenza all'intera immagine firmware ELEGOO.

## Progetti correlati

[OpenCentauri Firmware Tools](https://github.com/OpenCentauri/cc-fw-tools) pubblica strumenti e patch per Centauri Carbon e release basate su firmware ELEGOO. È il riferimento organizzativo scelto per questa pubblicazione. Questo progetto CC2 è indipendente: non implica affiliazione, derivazione del codice o compatibilità delle immagini CC1/CC2.

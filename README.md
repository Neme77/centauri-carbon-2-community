# Centauri Carbon 2 Community

Progetto indipendente di Neme77 per ELEGOO Centauri Carbon 2. Non affiliato né supportato da ELEGOO.

[English](README_EN.md) · [Installazione USB](docs/INSTALLAZIONE.md) · [Test](docs/TEST.md) · [Builder](docs/BUILD.md)

## Compatibilità

Base verificata: **02.01.00.00**, pacchetto `cc2_eeb001_02.01.00.00_20260707170825.zip.sig`.
Non è stata verificata la compatibilità con Centauri 2 non Carbon, altre versioni o tutte le revisioni hardware.

## Modifiche

- Ripristino del servizio SSH con accesso root.
- Patch Z-offset della GUI.
- Dual Trust v2: verifica delle firme stock e della chiave community del progetto.
- HTTP locale sulla porta 80 anche nel percorso WAN/cloud.
- Upload v1 durante la stampa, collaudato con OrcaSlicer in modalità LAN.

Elegoo Slicer può impedire l'upload lato client quando la stampante è occupata. Utilizzare OrcaSlicer LAN con il codice/password della stampante e il solo upload. La modifica non aggiunge una coda di stampa né avvia automaticamente il file caricato.

## Stato

Test riusciti di installazione da originale, ritorno all'originale e aggiornamento community. Vedere la matrice dei test per limiti ed evidenze.

Questa prima preparazione contiene documentazione e sorgenti. **Nessun firmware scaricabile è incluso**: i pacchetti collaudati verranno allegati alle Releases dopo la verifica dei file finali e della redistribuzione dei componenti.
Il builder richiede materiale esterno: non è una distribuzione autosufficiente.

## Firma e accesso

La prima installazione utilizza il pacchetto firmato stock; gli aggiornamenti community richiedono il Dual Trust corrispondente già attivo. Il firmware originale rimuove le modifiche dal sistema aggiornato: non presumere di conservare SSH o Dual Trust dopo il ritorno a stock.

Chiavi private, chiave AES, credenziali e immagini firmware non fanno parte del repository. La chiave pubblica community è in `cc2_builder_v3_7/dualtrust/cc2_community_release_public.pem`.

## Licenze

La licenza dei contributi del progetto è ancora da definire con. Non viene attribuita una licenza ai componenti ELEGOO o di terzi. Questa bozza non attesta il diritto di redistribuire firmware o binari di terzi.

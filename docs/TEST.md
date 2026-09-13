# Evidenze e limiti dei test

Stato raccolto dalle prove riferite dal manutentore il 13 settembre 2026. Non è una certificazione su tutte le schede.

| Prova | Esito riferito |
| --- | --- |
| Build v3.7 firmata stock con verifiche dei tre contenitori | BUILD PASS |
| Installazione USB su scheda con 02.01.00.00 originale | Accettata; riavvio e SSH/root funzionanti |
| Passaggio B → A | root=mmcblk0p8 prima; root=mmcblk0p7 dopo |
| Ritorno al firmware originale | Avvio riuscito; SSH assente |
| Nuova installazione del pacchetto stock del progetto | Avvio riuscito; SSH/root ripristinati |
| Pacchetto firmato community con Dual Trust | Installazione e funzionamento confermati dal manutentore; hash del pacchetto ancora da acquisire |
| HTTP locale | Funzionante secondo il test del manutentore |
| Upload durante stampa con OrcaSlicer LAN, patch v1 | File caricato senza interrompere la stampa |
| GUI Z-offset e daemon installati | Hash corrispondenti alla build |

## Identità dei componenti v3.7

Comandi sul sistema avviato, via SSH:

```sh
cat /proc/cmdline
md5sum /proc/$(pidof elegoo_printer)/exe
sha256sum /opt/inst/daemon-000/daemon-000
sha256sum /opt/bin/ec-eeb001-gui
```

| Componente | Hash atteso |
| --- | --- |
| elegoo_printer, MD5 di identificazione | b3a607b6d1db7b3d4224c2b3b5f0341a |
| elegoo_printer, SHA-256 | fb95bd123fbbb9e77ed380a0ab7bdb53a0b86997af8452adf76710a2d242e021 |
| daemon Dual Trust v2, SHA-256 | 3af6104d0ac76cc043ecf38985e1b00a0d5ceace0a4f4b66820e21f4735a98c7 |
| GUI, SHA-256 | afa2b1f181d18dc4803a60ee61fb3fc454f1e91afa12743bc1a1e5e386e439ff |

Hash storico del pacchetto stock costruito dal manutentore: `149be825757593323aeca8ffd8e8bb3a86354dbedfab7c14e60298b3bb942f7f`. Non usarlo per una nuova compilazione: IV casuali e metadati possono cambiare il pacchetto.

## Limiti

La sola assenza di SSH non identifica crittograficamente il daemon originale. Il test pratico di ritorno a stock e reinstallazione è positivo, ma non include un hash del daemon raccolto via seriale nella fase stock.
Il controllo hash della GUI non è una misura della precisione meccanica dello Z-offset.
Non sono documentate prove esaustive di interruzione di alimentazione, spazio esaurito, upload simultanei, timeout o tutte le transizioni pausa/ripresa.

Upload v1 e Dual Trust v2 sono versioni di modifiche diverse. L'ulteriore patch upload v2 non è inclusa. Usare nomi nuovi per gli upload: il primo blocco su nome esistente e la cancellazione durante stampa vengono rifiutati. Un trasferimento interrotto può lasciare un file parziale.

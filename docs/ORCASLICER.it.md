# Configurazione OrcaSlicer e sync dei filamenti Canvas

[English](ORCASLICER.md) · [Prima installazione](FIRST_RUN.it.md) · [Homepage](../README.md)

La guida usa la connessione tramite **CC2 Control** del Community Firmware V4.4 / CC2 Control 1.2.3. Completare la [prima configurazione](FIRST_RUN.it.md) e verificare **Stampante connessa** prima di configurare OrcaSlicer.

## 1. Configurare la connessione della stampante

Usare il proprio profilo funzionante **Centauri Carbon 2**, con ugello e configurazione Canvas corretti. Aprire le impostazioni della stampante fisica / connessione di rete. Posizione e traduzione dei campi cambiano tra versioni di OrcaSlicer; per il sync serve una versione che offra l'agente stampante **Moonraker**.

IP di esempio: `192.168.1.50`. Sostituirlo con l'indirizzo reale della propria stampante.

| Campo | Valore per CC2 Control |
| --- | --- |
| Host Type / Tipo host | **Octo/Klipper** |
| Hostname, IP or URL | `http://192.168.1.50:8081` |
| Device UI / Interfaccia dispositivo | `http://192.168.1.50:8081` |
| Printer Agent / Agente stampante | **Moonraker**, nel campo previsto dalla propria versione di OrcaSlicer |
| API Key / Password | Lasciare vuoto per l'endpoint CC2 Control: il codice LAN della stampante è già stato inserito nella configurazione di CC2 Control. |

Salvare, premere **Test** e aprire la scheda **Device / Dispositivo**. Deve comparire CC2 Control con le stesse informazioni visibili nel browser.

**Usare 8081 per upload, pagina Device e sync Canvas.** La porta **7125** è il ponte separato di sola lettura per Panda Breath: non fornisce i dati dei filamenti Canvas né l'upload G-code. La connessione nativa ELEGOO Link è un percorso diverso da quello descritto qui.

## 2. Sincronizzare i filamenti Canvas

1. Sul display o nella pagina **Canvas** di CC2 Control, controllare i dati di materiale e colore salvati nei quattro slot.
2. Selezionare in OrcaSlicer la stampante appena configurata e usare **Sync / Sincronizza filamenti** vicino all'elenco dei filamenti.
3. Se viene proposta una scelta dell'origine o dell'agente, usare **Moonraker**. Se manca, verificare versione di OrcaSlicer e impostazione dell'agente.
4. Prima dello slicing, confrontare ordine, materiali e colori importati con la pagina Canvas.
5. Selezionare o correggere i preset OrcaSlicer corrispondenti ai propri filamenti e alla stampante.

CC2 Control fornisce **identità dello slot, materiale salvato, colore e temperatura massima dell'ugello**. OrcaSlicer sceglie il proprio preset corrispondente. Il sync non copia tutte le impostazioni dei profili CC2 nei preset OrcaSlicer: un nome personalizzato, come `PLA MATTE`, può richiedere la selezione manuale del preset. La risposta di compatibilità non fornisce una temperatura del piatto utilizzabile come obiettivo: impostarla nel preset del filamento dello slicer.

Dati materiale salvati e filamento fisicamente caricato sono due cose diverse. Uno slot vuoto può conservare i dati fino al reset: CC2 Control attenua gli slot vuoti usando lo stato della stampante, mentre il sync Orca legge i dati materiale salvati. Controllare gli slot reali e la mappatura della stampa: il colore importato da solo non conferma che il filamento sia caricato.

Dopo una modifica dei materiali sul display o in CC2 Control, ripetere il sync. La sincronizzazione legge i dati dalla stampante; non carica fisicamente il filamento e non riscrive automaticamente tutti i profili OrcaSlicer.

## 3. Caricare e avviare una stampa

1. Eseguire lo slicing con il profilo CC2/ugello corretto e i preset materiale verificati.
2. Usare l'azione di upload oppure upload e stampa di OrcaSlicer.
3. Aprire **Device / Dispositivo**. L'upload e stampa attende la conferma di CC2 Control: controllare il dialogo della stampa in attesa.
4. Verificare lo slot Canvas per ogni filamento del file, il lato del piatto e le altre opzioni; poi avviare.

Il sync importa i dati degli slot per lo slicing; il dialogo di stampa conferma la mappatura fisica per quel lavoro. Il solo upload non avvia la stampa. Se esiste già un file con lo stesso nome, CC2 Control salva una copia numerata: controllare il file mostrato nel dialogo.

## Problemi comuni

| Problema | Cosa controllare |
| --- | --- |
| La pagina Device non si apre | Provare prima `http://IP-STAMPANTE:8081` nel browser; controllare IP, porta e rete locale. |
| Test riuscito ma sync vuoto | Verificare agente **Moonraker** e porta **8081**; aprire Canvas e attendere il primo snapshot. |
| Manca agente o pulsante sync | Verificare la versione di OrcaSlicer: il solo upload funzionante non conferma il supporto al sync. |
| Materiali o colori non aggiornati | Confrontare Canvas e display, aggiornare la pagina e ripetere il sync. |
| Materiale personalizzato associato a un preset generico | Selezionare manualmente il preset Orca corretto e verificare le temperature di stampa. |
| Upload e stampa non parte subito | Aprire Device e confermare il dialogo CC2 Control con la mappatura degli slot. |

Per una verifica avanzata di sola lettura, aprire nel browser questi indirizzi sostituendo l'IP:

```text
http://192.168.1.50:8081/server/info
http://192.168.1.50:8081/server/database/item?namespace=lane_data
```

Il primo dovrebbe riportare `klippy_connected: true` dopo la configurazione; il secondo deve contenere i dati Canvas in `lane_data` quando lo snapshot è disponibile. Un risultato vuoto non richiede il reset del firmware.

## Fonti e ringraziamento

Campi di connessione e comportamento del sync Moonraker sono stati verificati nelle [definizioni di OrcaSlicer](https://github.com/OrcaSlicer/OrcaSlicer/blob/main/src/libslic3r/PrintConfig.cpp) e nel suo [agente Moonraker](https://github.com/OrcaSlicer/OrcaSlicer/blob/main/src/slic3r/Utils/MoonrakerPrinterAgent.cpp). Gli endpoint CC2 sono descritti nella [guida del progetto](CC2_CONTROL.md#orcaslicer-canvas-filament-synchronization) e nella [documentazione API](../cc2-control/docs/API.md).

Un ringraziamento speciale a **[@efiten](https://github.com/efiten)** per la proposta originale, l'analisi e la validazione sulla stampante di questa integrazione.

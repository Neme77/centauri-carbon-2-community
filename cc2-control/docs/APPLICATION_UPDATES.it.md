# Aggiornamenti di CC2 Control

La funzione aggiorna esclusivamente CC2 Control. Il firmware della stampante,
elegoo_printer, la libreria delle callback e l’aggiornamento dal display non
cambiano. È richiesta la Community Firmware V4.4 sulla base ELEGOO
02.01.01.07 qualificata; l’installer verifica i binari effettivi.

## Prima installazione

Installa una volta dal computer il pacchetto che include il modulo di
aggiornamento, tramite l’installer Windows o Linux/macOS. La configurazione,
il codice LAN, i materiali e l’inventario delle bobine vengono conservati.
Successivamente apri **Impostazioni → Aggiornamenti**.

Metti in pausa il live, attendi che la stampante sia inattiva e che ugello e
piatto siano a massimo 50 °C, con entrambi i target a zero. Inserisci il codice
LAN come conferma: non è la password SSH. Non viene conservato nella memoria
persistente del browser né nei dati dell’aggiornamento.

## Controllo e installazione

Il canale predefinito è **Stabile**. **Canvas UDS sperimentale** richiede una
conferma e può consumare più memoria. I due canali sono distinti; una versione
sperimentale non viene proposta al canale stabile solo perché è più recente.

GitHub ospita i pacchetti e un piccolo indice firmato che indica versione,
firmware richiesto, canale, dimensione e novità. La scheda controlla l’indice
all’apertura e riutilizza un risultato verificato e non scaduto per sei ore.
**Controlla aggiornamenti** forza un nuovo controllo. Non ci sono installazioni
automatiche o servizi aggiuntivi sempre attivi. Se l’indice non è ancora stato
pubblicato, non vengono proposti aggiornamenti online.

**Scarica e verifica** salva sulla memoria flash, sotto
`/opt/usr/cc2-control-updates`, e controlla firma e contenuto del pacchetto.
Non installa ancora nulla. Il download usa un componente HTTPS autonomo che
verifica certificati e nomi dei server, senza dipendere dal curl della
stampante. È necessario che la data della stampante sia corretta.
È disponibile anche il caricamento di un file `.cc2update` firmato dal computer.
Un normale ZIP non è un pacchetto installabile dal pannello.

**Installa aggiornamento verificato** richiede una seconda conferma. Un
processo indipendente ricontrolla il pacchetto, crea il backup, installa e
verifica versione, eseguibile e connessione MQTT. Se un controllo fallisce
dopo l’arresto del servizio, tenta il ripristino della precedente installazione.
Lo stato sopravvive al riavvio: riapri la scheda per leggerne l’esito.
Chiudere il browser non interrompe un’installazione già accettata.

Mantieni il percorso del backup e le istruzioni di ripristino. Non effettuare
un reset di fabbrica e non saltare i controlli di compatibilità.

La firma Ed25519 protegge indice e pacchetto; il digest SHA-512 è calcolato a
blocchi senza caricare l’intero aggiornamento in RAM. Versioni precedenti o
uguali, firmware incompatibili, file alterati, collegamenti e percorsi non
previsti vengono rifiutati. Le chiavi private restano sul computer del
maintainer e non sono distribuite.

La prima versione del modulo e il canale sperimentale richiedono prove
separate sulla stampante. I test automatici non sostituiscono queste prove.

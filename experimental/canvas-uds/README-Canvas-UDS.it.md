# Canvas UDS — variante sperimentale

Porta il nostro prototipo locale Canvas UDS 1.2.4 sui sorgenti attuali.
La funzione resta distinta dalla retrazione oltre 350 mm (#172) e dalla
modalità silenziosa (#179). L'espulsione completa abbandonata non è inclusa.

L'avvio ordinario continua a usare MQTT. Solo l'opzione esplicita
`--experimental-canvas-uds` amplia la sottoscrizione UDS già presente.
Lo stato completo e valido può alimentare Canvas, metadati Orca e disponibilità
delle azioni, mantenendo i controlli su testina, connessione, dati aggiornati e
stampante inattiva. Il recupero dopo riavvio rimane esplicito.

Una cache piccola e fissa conserva le variazioni parziali. Dati incompleti,
non validi o non aggiornati fanno tornare alla vista MQTT originale,
conservata separatamente. `/api/canvas` indica la sorgente effettiva;
`/api/uds` distingue opzione abilitata da dati realmente disponibili.

In questa prima versione non disabilitiamo le letture MQTT esistenti e non
spostiamo i metadati del tracciamento bobine. Non promettiamo risparmi di RAM
o CPU senza misure. I comandi nativi e le relative protezioni non cambiano.

Non è ancora un pacchetto da installare: prima servono tutti i controlli
automatici sulla revisione finale, un avvio/ripristino dedicato e la prova
V4.4/V4.4.1 di slot, materiale, sincronizzazione Orca, carico/scarico, recupero
e stampa multicolore. Nessun firmware, catalogo o numero di release è cambiato.
La patch camera già installata dovrà essere mantenuta.

Per dettagli e comandi di sola lettura vedere [README-Canvas-UDS.md](README-Canvas-UDS.md).

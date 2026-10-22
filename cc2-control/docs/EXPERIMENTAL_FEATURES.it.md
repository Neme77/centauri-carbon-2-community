# Funzioni sperimentali opzionali

Gli aggiornamenti dell’app usano il catalogo stabile firmato. In Impostazioni →
Aggiornamenti una sezione separata permette di scegliere le funzioni sperimentali.
Entrambe partono spente. Non è un negozio di plugin e gli interruttori non
scaricano eseguibili. La vecchia scelta del canale `canvas-uds` nel browser non
attiva alcuna funzione e non nasconde più gli aggiornamenti stabili.

Le due scelte sono indipendenti, salvate sulla stampante nel file privato
`/opt/usr/cc2-control-experiments.json`, conservato dagli aggiornamenti ordinari.
Un file non valido non viene sovrascritto: le funzioni restano spente. Le scelte
sperimentali non vengono esportate nel backup portabile, per non attivarle su
un’altra stampante. Una vecchia versione dell’app priva di queste funzioni non
può offrirle anche se il file delle scelte è rimasto presente.

Ogni modifica richiede codice LAN e conferma esplicita, stampante connessa,
dati aggiornati, inattività, ugello e piatto freddi e target a zero. Il salvataggio
è atomico; non modifica altre preferenze e non invia riscaldamenti, movimenti
o riavvii della stampante.

## Canvas UDS

Attivare o disattivare riconnette brevemente la sola telemetria con la relativa
sottoscrizione. I dati diretti precedenti perdono subito la loro validità.
Canvas e Orca usano la cache UDS completa e valida; in caso contrario resta il
ripiego sui dati MQTT originali. La pagina distingue la scelta abilitata dalla
sorgente realmente in uso. Le letture MQTT esistenti restano attive e non
promettiamo risparmi di memoria o CPU. L’opzione da riga di comando per gli
sviluppatori non viene sovrascritta dall’interfaccia.

## Controlli motori silenziosi

L’opzione rende visibili i controlli Quiet/Normal della PR #179 di BorjaLL:
non cambia lo stato dei motori. Quiet resta rifiutato se l’opzione è spenta;
Normal rimane disponibile come comando di ripristino. Restano conferme e limiti:
80 mm/s e 2000 mm/s² sono limiti iniziali che lo slicer può sovrascrivere. Il
G-code deve rispettarli per tutta la stampa. Normal ripristina i valori fissi
500/20000, non eventuali limiti personalizzati precedenti. La modalità mostrata
è una richiesta, non una lettura verificata dei registri del driver.

Prima di aggiornare l’app, spegnere l’opzione o cambiare sorgente Canvas mentre è accesa,
selezionare Normal in Controllo e attendere il completamento del comando senza
errori sulla connessione UDS attuale. Il salvataggio delle preferenze non invia
quel comando al posto dell’utente. Anche un comando completato non certifica
da solo lo stato fisico dei motori.

## Retrazione estesa e webcam

La retrazione oltre 350 mm richiede una modifica dell’eseguibile del firmware:
resta una prova separata con installazione e ripristino verificati. Prima di
ripristinare l’eseguibile originale serve una configurazione effettiva entro
350 mm.

Il fix webcam R2 resta incluso nei pacchetti cumulativi, anche per chi salta
la 1.2.19. La creazione di una release pubblica con `--release` rifiuta
l’assenza dei runtime webcam qualificati. L’installer esistente conserva la
patch già installata. Alla prima attivazione serve un riavvio completo della
stampante inattiva. Le build di sola applicazione per CI non sono release
cumulative da pubblicare.

I test C e del browser coprono salvataggio, indipendenza delle scelte,
conferme, errori, ripristino MQTT, protezioni e geometria. Non sostituiscono
nuove verifiche hardware. Crediti originali di #200 e #179 conservati.

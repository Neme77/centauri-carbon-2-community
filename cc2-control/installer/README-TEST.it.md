# Prova combinata CC2 Control — PR #190 e #196

Pacchetto tradizionale della sola applicazione. La versione resta 1.2.19 per questa prova; non è una nuova release. Include recupero Canvas (#190) e rifiniture interfaccia/accessibilità di Surfoo (#196).

La patch camera già installata viene conservata: non viene inclusa, reinstallata o riattivata. Anche le preferenze, i piatti e le bobine vengono mantenuti. Questo aggiornamento riavvia solo CC2 Control; il riavvio della stampante serve qui a riprodurre la condizione Canvas.

Installare a stampante inattiva, con ugello/piatto non oltre 50 °C e target zero. Chiudere le dirette. Caricare il file .cc2update firmato da Impostazioni → Aggiornamenti, oppure usare l'installer Windows dello ZIP. Conservare il percorso del backup e aggiornare la pagina dopo l'installazione.

## Verifiche

1. Espandere e ridurre il menu: icone e pulsante devono restare allineati. Controllare File, Console, Bobine e i normali dialoghi anche da telefono. La campanella apre gli Aggiornamenti se non ci sono nuove versioni; se disponibile, verificare che la sua finestra sia visibile per intero.
2. Completare un caricamento Canvas normale, lasciare il filamento caricato e riavviare normalmente la stampante quando inattiva, come nelle prove già eseguite.
3. Se ricompare il filamento rilevato senza slot assegnato, selezionare qualsiasi slot presente. Il nuovo pulsante Recupera caricando lo slot N richiede conferma: è un caricamento reale e può riscaldare l'ugello e spurgare. Usarlo con filamento Canvas, dopo averne controllato il percorso.
4. Annullando la conferma non parte alcun caricamento. Confermando, attendere il caricamento e verificare lo stato finale coerente e la disponibilità delle azioni normali. Una richiesta accettata non viene mostrata come recupero completato.
5. Controllare video locale/Matrix e preferenze già presenti, poi riferire l'esito prima dei merge. Non ripetere automaticamente un caricamento fallito.

Il ripristino tradizionale è indicato dall'installer, da eseguire con stampante inattiva e fredda. Nessun canale di aggiornamento pubblico viene modificato da questa prova.

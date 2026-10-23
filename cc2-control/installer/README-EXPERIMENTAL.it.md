# Prova sperimentale: Canvas UDS e Quiet

Pacchetto di sola applicazione per sviluppo. Non è la release stabile e
non va caricato come aggiornamento automatico o firmato. Estrarre lo ZIP e usare
Install-Windows.cmd, oppure install-cc2-control.sh su macOS/Linux, con stampante
connessa, inattiva, fredda e target a zero. Conservare il percorso del backup.

Questo pacchetto contiene la sola applicazione: conserva il fix webcam già
installato, non lo reinstalla e non modifica l'eseguibile del firmware. Non serve
un riavvio completo per i due interruttori. I futuri aggiornamenti pubblici
cumulativi continueranno a includere il componente webcam.

In Impostazioni → Aggiornamenti, inserire il codice LAN nella sezione delle
funzioni sperimentali. Le due opzioni partono spente e rimangono indipendenti.
Provare prima Canvas UDS, controllando stati degli slot e sincronizzazione Orca;
poi disattivarlo per provare Quiet separatamente. Una scelta salvata non è una
conferma della sorgente realmente disponibile: leggere lo stato mostrato.

Quiet abilita solo i controlli in Controllo. Non cambia i motori da solo. Prima di
qualsiasi movimento verificare le letture dei driver e i limiti come descritto
nella PR #179. Lo slicer può sovrascrivere i limiti iniziali 80 mm/s e 2000 mm/s²;
non usare G-code che li superi. Normal ripristina valori fissi 500/20000, non
eventuali limiti personalizzati. La modalità visualizzata è richiesta, non una
verifica dei registri hardware. Questa integrazione non è ancora validata sulla
stampante: i test degli autori e quelli automatici sono registrati separatamente.

Per disattivare Quiet, cambiare sorgente Canvas o aggiornare/ripristinare l'app
quando Quiet è abilitato, selezionare Normal in Controllo e attendere il
completamento senza errori. Poi spegnere le opzioni. Se serve, usare il comando
di ripristino dal percorso del backup stampato dall'installer, a stampante
inattiva e fredda. Le preferenze sperimentali restano esterne all'applicazione;
una vecchia app non offre automaticamente queste funzioni.

La retrazione oltre 350 mm è un esperimento separato: il riconoscimento del
firmware è incluso ma il runtime esteso non viene installato. Non modificare il
firmware originale o aumentare distanze per provare questi due interruttori.
Un eventuale runtime esteso richiede prima del ripristino configurazioni
effettive entro 350 mm e la propria procedura di rimozione.

Crediti: BorjaLL per Quiet (#179); Noahwhite2014 per retrazione estesa (#202/#209,
issue #172); Surfoo per l'idea del layout Canvas; DamiBFryta per i suggerimenti e
le prove del popup bobine. Le relative storie originali sono conservate.

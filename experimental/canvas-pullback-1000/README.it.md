# Retrazione Canvas oltre 350 mm — preparazione sperimentale

Ramo dedicato: `experimental/canvas-pullback-1000`.
Idea, analisi della costante e prove riferite a 560 mm: **Noahwhite2014**,
[issue #172](https://github.com/Neme77/centauri-carbon-2-community/issues/172).

**Non è ancora un aggiornamento installabile. Il catalogo sperimentale non è
stato attivato.** La versione stabile e il suo catalogo restano invariati.

La patch offline riproduce esattamente la modifica di due byte che porta il
limite a 1000 mm. Non modifica stampanti, configurazioni o processi; non
include eseguibili ELEGOO. Verifica impronte complete prima e dopo e rifiuta
input sconosciuti e destinazioni già esistenti. Le prove locali hanno verificato
anche il ritorno esatto ai byte originali.

La parte CC2 Control del contributore potrà essere integrata su questo ramo.
Prima di distribuire servono compatibilità dell'installatore e del runtime,
riconoscimento dell'eseguibile attivo, prove ARM e sulla stampante e ripristino
che riporti prima la configurazione a non più di 350 mm. Un programma originale
con un valore salvato superiore a 350 mm non si avvierebbe.

La prova resta distinta dall'espulsione completa abbandonata (#103/#114).
Per requisiti e comandi offline vedere [README.md](README.md).

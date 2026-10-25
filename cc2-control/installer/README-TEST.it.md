# Prova cumulativa PR 218, 219, 220, 222 e 223 — R1

Pacchetto di verifica per la stampante del manutentore, non una release pubblica.
La versione rimane 1.2.21: installare dal computer con Install-Windows.cmd
(o Install-CC2-Control.ps1), non dalla campanella o dal caricamento nel pannello.

Include:
- Dashboard approvata: sfondo camera di pezso92, Mantieni attiva nella testata,
  slider velocità/flow insieme a valori e Applica/Ripristina; spazi riallineati.
- Integrazioni sperimentali sotto quelle ufficiali, compresa l'asciugatura.
- Asciugatura del filamento sul piatto: temperatura 30–90 °C e durata finita,
  avvio/arresto espliciti, conferme e controlli di stato. Non si avvia abilitandola.
- Avviso di stampa completata, conferma e ristampa dal normale dialogo;
  percentuale azzerata a stampante inattiva.
- Consumi recenti delle bobine in un riquadro scorrevole e filtrabile.
- Requisiti aggiornamenti corretti anche per firmware V4.4.2 in tutte le lingue.

Installare soltanto con stampante connessa, inattiva e fredda, target a zero.
Le preferenze sono conservate. Questo pacchetto di sola applicazione conserva
la patch webcam già installata: non la reinstalla e non riavvia la stampante.
Conservare il percorso di backup e il comando di ripristino mostrati al termine.

Prove: Dashboard e cursori durante una stampa normale; live, cambio pagina e
Mantieni attiva; popup e percentuale alla fine; filtri bobine; Integrazioni e
pagina Aggiornamenti. Verificare prima le funzioni ordinarie. Per la nuova
asciugatura leggere cc2-control/docs/BED_DRYING.md nei sorgenti: eseguire una
prima sessione breve e sorvegliata, senza bobina, verificando arresto e target
zero. Non usare la prima prova per un ciclo incustodito.

Crediti: pezso92 per la direzione grafica e lo sfondo camera; romeopuch per la
richiesta asciugatura; DamiBFryta per prove e suggerimenti sulle bobine, oltre
ai contributor già accreditati nelle singole PR. Nessuna release, catalogo
stabile o firmware completo viene pubblicato da questa prova.

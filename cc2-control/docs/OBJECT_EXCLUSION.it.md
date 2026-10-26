# Mappa per l’esclusione degli oggetti

Durante una stampa supportata, la pagina Stampa mostra un’unica mappa del piatto
da 256 × 256 mm vista dall’alto. Il fronte della stampante è in basso (Y = 0).
I numeri corrispondono all’ordine dell’elenco compatto dei nomi. Le sagome vengono
disegnate quando sono disponibili nel G-code; altrimenti viene usato il centro
riportato, senza inventare la forma.

Seleziona un indicatore o un nome per vedere nome completo e coordinate. Il ciano
indica la selezione, l’ambra l’oggetto attualmente in stampa; gli oggetti esclusi
sono rossi e barrati. L’indicazione dell’oggetto in stampa non cambia la selezione
manuale. Gli oggetti senza una posizione utilizzabile restano selezionabili
dall’elenco e non ricevono una posizione inventata sulla mappa.

**Espandi mappa del piatto** ingrandisce la stessa vista. Chiudi la finestra o
premi Esc per tornare alla mappa nella pagina. La selezione non invia comandi.
**Escludi oggetto selezionato** richiede ancora conferma e usa l’identificativo
originale del G-code, mai il nome visualizzato o il numero sulla mappa.
L’esclusione non può essere annullata per quella stampa.

La mappa riutilizza dati e interrogazioni già esistenti. Il disegno viene gestito
dal browser, senza nuove richieste alla stampante o effetti animati continui.

Grazie a **DamiBFryta** per il suggerimento di mantenere un’unica vista più chiara
e a **Nick Plateringen-Paul** per il prototipo originale della mappa del piatto.

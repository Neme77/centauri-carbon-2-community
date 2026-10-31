# Installer completo Connessioni

Per il runtime qualificato del **firmware community V4.4.2**. Estrai tutto lo ZIP,
chiudi gli streaming e usa una stampante connessa, inattiva e fredda, con target
zero. Avvia `Install-Windows.cmd`, oppure `sh install-cc2-control.sh` su Linux/macOS.
Comprende applicazione, fix webcam cumulativo e componente di rete facoltativo.
Lo ZIP si usa dal computer e non va caricato nel pannello.

Hardware verificato: **TP-Link UE300**, Realtek **RTL8153**, USB **0bda:8153**,
revisione **31fd**. Altri marchi, identificativi o revisioni non sono qualificati
automaticamente perché condividono il chipset. Il supporto è integrato da CC2;
il firmware originale non dispone del driver. I 1000 Mb/s indicano il collegamento,
non una velocità di trasferimento misurata: la stampante usa USB 2.0.

Apri Impostazioni → Connessioni. Il Wi-Fi resta disponibile senza adattatore.
Abilita il supporto Ethernet USB solo se desiderato e scegli manualmente la rete.
Sono disponibili profili Wi-Fi, DHCP/IP manuale e attiva/disattiva delle interfacce.
Il codice LAN non serve ai soli comandi di rete; aggiornamenti e integrazioni
sperimentali conservano le autorizzazioni. Non esiste una selezione automatica
basata sul segnale né una scadenza di prova per una connessione confermata.

Dopo la commutazione apri il nuovo indirizzo e premi **Mantieni questa connessione**.
Senza conferma si ripristina la precedente; la scelta confermata resta anche dopo
il riavvio. Disattiva l'altra connessione dalla pagina dell'indirizzo che vuoi
mantenere. Non è consentito spegnere l'ultima interfaccia abilitata. La prova
dell'IP sul display originale è esclusa.

L'installazione lavora su `/opt/usr`, rimuove gli archivi solo dopo il successo
completo e conserva log e backup di ripristino dell'app e della rete. In caso di
errore conserva i percorsi stampati, senza reinstallare ripetutamente. Non avvia
riscaldamento, movimento o riavvio automatico. Segui l'eventuale richiesta di
riavvio completo alla prima attivazione del fix webcam, con stampante inattiva.

Il runtime di rete è identico alla R5h accettata: sono stati confermati commutazione,
video locale/remoto, trasferimento Orca, persistenza al riavvio e avvio regolare
del Canvas. Il nuovo cumulativo ricompilato richiede una propria verifica sul
dispositivo. Sorgenti GPL e licenze della rete sono in `cc2-connections.tar.gz`;
quelli dell'app in `cc2-control-sources.zip`. Nessun ELF ELEGOO è incluso.

Alla prima installazione e attivazione del supporto Ethernet USB, il Canvas può
lampeggiare temporaneamente in rosso durante il riallineamento dei servizi
camera/stampante. Al completamento del riallineamento torna operativo.

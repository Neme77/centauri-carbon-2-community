# UE300 — sonda preliminare R1

Questa prova non abilita Ethernet. Il pacchetto contiene soltanto una sonda, non i driver mii/r8152.

La sonda verifica la risoluzione delle dipendenze kernel e alcuni campi di net_device, sk_buff e del dispositivo USB. Non registra interfacce, non trasmette pacchetti, non modifica indirizzi/route, non cambia servizi o file di avvio. Alloca e libera temporaneamente strutture in memoria e legge i descrittori USB già presenti. Il modulo viene rimosso subito dopo il test.

Il sorgente candidato OpenCentauri/oc-linux cd4ee0cf38af4f4b21f2918e2ad9f0627fdfde57 riporta 5.4.61-ab1175, diverso dal kernel ELEGOO. La sonda è compilata con EXTRAVERSION vuoto per il rilascio osservato 5.4.61: questo non dimostra compatibilità. Non si usa insmod forzato né si modificano i binari per alterarne la versione.

Le dimensioni e gli accessi osservati nel modulo Wi-Fi originale sono un controllo preliminare, non una dimostrazione completa di compatibilità ABI. Anche una sonda nel kernel può bloccare il sistema in caso di incompatibilità. Eseguire solo a stampante inattiva e senza riscaldamenti; mantenere il Wi-Fi attivo. In caso di blocco può servire un riavvio manuale. Nessun riavvio automatico.

1. Copiare il pacchetto in /tmp ed estrarlo in /tmp/cc2-usb-lan-abi-probe-r1.
2. `sh /tmp/cc2-usb-lan-abi-probe-r1/probe.sh check` verifica impronte e stato senza caricare moduli.
3. Dopo aver verificato il risultato: `sh /tmp/cc2-usb-lan-abi-probe-r1/probe.sh probe` carica la sola sonda e la rimuove dopo il test.

Un PROBE PASS indica soltanto che i collegamenti e i campi verificati hanno superato questa prova. Non dimostra ancora il funzionamento del driver r8152 o dell'adattatore. Non installare i moduli di sola analisi prodotti in precedenza.

Toolchain OpenCentauri/arm-openwrt-linux-gnueabi-gcc f4210c9d613a5e1cb4fc07a1b03bc35ab332f596, GCC 6.4.1. Build riproducibile nel ramo test/usb-ethernet-ue300-probe-r1 del repository Neme77/centauri-carbon-2-community. I file del driver originale e /proc/kallsyms forniti dall'utente sono rimasti locali.

Licenza della sonda: GPL-2.0. Sorgenti e Makefile sono inclusi. Il kernel candidato e la toolchain mantengono autori e licenze originali.

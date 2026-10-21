# Canvas recovery after restart / Recupero dopo il riavvio

## English

The printer can report filament in the head but lose its Canvas channel association after a reboot: head sensor true, active channel -1, tray statuses 1. The maintainer subsequently verified that the ELEGOO display permits loading any present slot in this condition and that completing native loading restores coherent state. This supersedes the earlier last-used-slot-only guidance; it does not identify the physical source of filament already in the head.

CC2 Control offers a separate **Recover by loading slot N** action for this exact unassigned-head condition. Select any present slot and explicitly confirm the native load. Check the filament path before confirming, and do not use this procedure with an external spool. Native loading may heat the nozzle and purge extra material.

Backend eligibility requires fresh registered MQTT telemetry (at most 15 seconds), fresh direct head/channel/idle readback, idle machine status, active channel -1, a reported head-presence signal and a present selected slot. All four tray entries must be known empty/present; a reported loaded channel or missing/ambiguous entry blocks recovery. Normal Load and Unload policy stays unchanged. Recovery emits the existing `CANVAS_LOAD_FILAMENT CHANNEL=N` command through the protected control/console path, not raw feeder movement, slot reassignment or status clearing.

The API adds a separate `recovery_load` permission. The UI sends no command until confirmation, prevents repeated submissions and shows that the request is awaiting real printer confirmation. Accepted command delivery is not successful loading. There is no automatic loading, retry, power-loss print resumption, remembered-slot inference, or extra polling. Other transitions, printing/paused/busy, disconnected and stale states remain blocked. Firmware rejection leaves the readback and ordinary protections intact.

## Italiano

La prova successiva del manutentore ha confermato che dal display ELEGOO si può caricare **qualsiasi slot presente** per recuperare la condizione rilevamento filamento in testina/canale Canvas non assegnato. Non è necessario scegliere l’ultimo slot usato. Questa osservazione sostituisce l’indicazione precedente e non determina da quale bobina provenga fisicamente il filamento già in testina.

CC2 aggiunge **Recupera caricando lo slot N** solo per quel caso. Selezionare uno slot presente, controllare il percorso e confermare il caricamento nativo, che può riscaldare l’ugello e spurgare altro materiale. Non usare la procedura con la bobina esterna.

Il backend richiede dati freschi, collegamento e registrazione validi, stampante inattiva, rilevamento in testina e canale -1; tutti gli slot devono avere stati noti vuoto/presente. Usa il comando nativo già esistente e i normali controlli di esecuzione. Carica e Scarica ordinari restano protetti. Una richiesta accettata non equivale a recupero completato: la conferma deve arrivare dalla stampante. Nessuna selezione automatica, nessun movimento diretto, nessuna cancellazione artificiale dello stato e nessun tentativo automatico.

## Validation status

Native policy/command fixtures cover each present slot and refuse empty/malformed/out-of-range slots, ordinary transitions, busy/printing, missing/duplicate/incomplete tray data, disconnected/unregistered sources, stale/future telemetry and missing direct state. Browser interaction covers selection, cancel/confirm, exact command, single attempt, no optimistic state change and final real-readback confirmation, plus mobile bounds.

The display procedure has been observed on the printer. The new CC2 command path is **not yet hardware-qualified**: the printer currently works normally. Test this dedicated path only when the same real condition recurs, with the operator observing native loading and the final state. This is not part of published 1.2.19; no release/version increment is made for this development trial.

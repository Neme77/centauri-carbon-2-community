import hashlib,json,shutil,zipfile
from pathlib import Path,PurePosixPath
root=Path.cwd()
source=Path('/tmp/validated-1214/CC2-Control-1.2.14-Sources.zip')
assert hashlib.sha256(source.read_bytes()).hexdigest()=='ebdd6355f43607ee8c3bdcb9743f4e8b0c8351a0c699157ddb4de220fe280be4'
with zipfile.ZipFile(source) as z:
    assert z.testzip() is None
    for name in z.namelist():
        assert not PurePosixPath(name).is_absolute() and '..' not in PurePosixPath(name).parts
        dest=root/'cc2-control'/name
        dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(z.read(name))
assert (root/'cc2-control/VERSION').read_text().strip()=='1.2.14'
section="""## CC2 Control 1.2.14 — printer-validated stable update

**[Download CC2 Control 1.2.14](https://github.com/Neme77/centauri-carbon-2-community/releases/tag/CC2-Control-1.2.14-V4.4)** · [English update guide](docs/UPDATE_CC2.md) · [Guida italiana](docs/UPDATE_CC2.it.md).

**Only Community Firmware V4.4 / V4.4.1. Do not install on V4.3 or stock ELEGOO firmware.** Updates the application while retaining the installed firmware and callback runtime. Existing preferences and profiles are retained.

| Improvement | Result |
| --- | --- |
| Object Exclusion | Spatial order from front to back and left to right, readable names and numbered copies; original G-code identifiers remain separate from display names. Handles ELEGOO's string-encoded coordinates and clears completed-print objects. |
| Canvas state | Distinguishes empty slots, filament present but unloaded from the head, loaded filament and transitions. Actions are enabled only when applicable. |
| Camera and fans | Persistent live keeps its original size; fan sliders drag correctly and numeric values still apply with Enter. |
| Plate workflow | Create a plate before its first mesh, select it for calibration and retain the destination when changing tabs. Uncalibrated profiles cannot be mounted. |
| Mesh details | Side A/B labels for default/default1, calibration from 30 to 110 °C, and measurement date plus exact time and timezone. |
| Languages and updates | Nine languages including Spanish, refined Hungarian, and localized update punctuation. |

Open **Settings → Updates → Stable → Check for updates**. Enter the configured LAN code, download and verify, then separately confirm installation. Pause live view and use an idle, connected printer with nozzle/bed at or below 50 °C and heater targets zero. No factory reset is needed. The LAN code is on the display under **Settings → LAN only** and differs from the SSH password. Without the updater panel, use the release's computer-assisted installer ZIP; panel upload accepts the signed .cc2update instead.

**Validated on the maintainer's printer:** Canvas, camera geometry, mouse fan controls, Spanish, localized punctuation, A/B labels, Object Exclusion and the new plate/calibration workflow. Native, browser and ARM tests passed; the published signed payload is identical to the tested candidate. This is not a new long-duration printing qualification.

**Credits:** Spanish by **[asiersan](https://github.com/asiersan)** ([#130](https://github.com/Neme77/centauri-carbon-2-community/pull/130), integration #133); Hungarian refinements by **[pezso92](https://github.com/pezso92)** ([#129](https://github.com/Neme77/centauri-carbon-2-community/pull/129), integration #132); localized punctuation by **[Surfoo](https://github.com/Surfoo)** ([#128](https://github.com/Neme77/centauri-carbon-2-community/pull/128), integration #135). Advanced mesh builds on **beanbo's #104**. Thanks to the beta testers for the reports and real-printer trials; [full credits](docs/CONTRIBUTORS.md).

**Library compatibility:** older applications enforcing a 40 °C minimum cannot read measurements created at 30–39 °C, and may discard uncalibrated profiles when saving. Back up the plate library before reverting to an older application. Experimental Canvas ejection, continuous Canvas UDS and webcam-service restart are excluded.

**Italiano:** la 1.2.14 aggiorna esclusivamente CC2 Control su V4.4/V4.4.1. Include esclusione oggetti corretta, stato Canvas, dimensioni del live, cursori ventole, lingue e gestione piatti migliorata. Si possono creare profili prima della prima mesh, calibrare da 30 °C e cambiare scheda senza perdere la destinazione. Installazione dal pannello con codice LAN e conferma separata; firmware e callback restano invariati.

"""
p=root/'README.md';s=p.read_text();start=s.index('## CC2 Control 1.2.11 — stable update from the panel');end=s.index('## Previous application update',start);s=s[:start]+section+s[end:];s=s.replace('Eight app languages','Nine app languages');p.write_text(s)
p=root/'CHANGELOG.md';s=p.read_text();i=s.find('\n## ');s=s[:i]+'\n## CC2 Control 1.2.14 — 2026-10-16\n\n'+section.split('\n',2)[2]+s[i:];p.write_text(s)
for name in ['UPDATE_CC2.md','UPDATE_CC2.it.md']:
    p=root/'docs'/name;s=p.read_text().replace('1.2.11','1.2.14')
    s+='\n\n'+('Plate library: older apps enforcing a 40 °C minimum cannot read new 30–39 °C measurements and may discard uncalibrated profiles on save. Back up the library before reverting.\n' if name=='UPDATE_CC2.md' else 'Libreria piatti: le vecchie app con minimo 40 °C non leggono nuove misure a 30–39 °C e possono scartare i profili non calibrati al salvataggio. Conservare una copia prima di tornare a una versione precedente.\n')
    p.write_text(s)
p=root/'docs/CONTRIBUTORS.md';s=p.read_text();i=s.index('## Beta testers');s=s[:i]+"""## CC2 Control 1.2.14 language and interface contributions

Spanish: **[asiersan](https://github.com/asiersan)**, original #130, integration #133. Hungarian refinements: **[pezso92](https://github.com/pezso92)**, original #129, integration #132. Localized update punctuation: **[Surfoo](https://github.com/Surfoo)**, original #128, integration #135. These original contributions retain exclusive author credit. The plate workflow extends the advanced mesh system by **beanbo** in #104. Thank you to the beta testers who reported the camera, fan, object-layout and plate-workflow issues and validated the combined application on a real printer.

"""+s[i:];p.write_text(s)

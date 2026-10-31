"""Assemble a private diagnostic installer from the qualified GNU ARM output."""
import gzip,hashlib,io,json,re,tarfile,zipfile,sys
from pathlib import Path

root=Path(__file__).resolve().parent
workspace=root.parents[3]
old=workspace/'outputs/CC2-Control-Connections-Manual-Trial-R4/CC2-Control-Connections-Manual-Trial-R4'
qualified=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else workspace/'work/network-connections-qualified/final'
commit=sys.argv[2] if len(sys.argv)>2 else 'a55bc024869df0837c74ea88dfe86e9d84c4adbb'
ci_run=int(sys.argv[3]) if len(sys.argv)>3 else 41015530793
assert commit == 'local' or re.fullmatch('[0-9a-f]{40}',commit)
revision=sys.argv[4] if len(sys.argv)>4 else 'R5'
assert re.fullmatch(r'R[0-9]+[a-z]?',revision)
stage='/opt/usr/cc2-control-connections-'+revision.lower()
result=workspace/('outputs/CC2-Control-Connections-Diagnostics-'+revision)
result.mkdir(exist_ok=True)
package=result/('CC2-Control-Connections-Diagnostics-'+revision)
assert not package.exists(),'Never overwrite a delivered revision'
package.mkdir()
sha=lambda b:hashlib.sha256(b).hexdigest()
report=json.loads((qualified/'validation.json').read_text(encoding='utf-8'))
for name,digest in report['sha256'].items(): assert sha((qualified/name).read_bytes())==digest,(name,digest)
def read_tar(path):
    with tarfile.open(path) as t:
        data={}
        for entry in t:
            assert entry.isfile() or entry.isdir(),entry.name
            if not entry.isfile():continue
            assert not entry.name.startswith('/') and '..' not in Path(entry.name).parts
            data[entry.name]=(t.extractfile(entry).read(),entry.mode)
        return data
def manifest(data):
    return ''.join(f'{sha(blob)}  {name}\n' for name,(blob,_) in sorted(data.items()) if name!='SHA256SUMS').encode()
def write_tar(path,data):
    with path.open('wb') as output,gzip.GzipFile(filename='',mode='wb',fileobj=output,mtime=0) as zipped,tarfile.open(fileobj=zipped,mode='w') as t:
        for name,(blob,mode) in sorted(data.items()):
            entry=tarfile.TarInfo(name);entry.size=len(blob);entry.mode=mode;entry.mtime=0;entry.uid=entry.gid=0
            t.addfile(entry,io.BytesIO(blob))
def text(name,content):
    content=re.sub(r'/opt/usr/cc2-control-connections-r5(?=$|[^a-z0-9])',lambda _:stage,content)
    content=re.sub(r'\bR5\b',lambda _:revision,content)
    (package/name).write_text(content,encoding='utf-8',newline='\n')
app=read_tar(old/'cc2-control-payload.tar.gz')
original=app['cc2-control'][0]
assert sha(original)=='f0a7844c07f50313ddaeea07e827ca3233f421e5183446fdf242ffc19050113f'
candidate=(qualified/'cc2-control-network').read_bytes()
installer=app['install-on-printer.sh'][0]
matches=re.findall(rb'(?m)^EXPECTED=([a-f0-9]{64})$',installer)
assert matches==[sha(original).encode()]
installer,count=re.subn(rb'(?m)^EXPECTED=[a-f0-9]{64}$',b'EXPECTED='+sha(candidate).encode(),installer)
assert count==1
app['cc2-control']=(candidate,0o755);app['install-on-printer.sh']=(installer,0o755)
for p in (qualified/'cc2-control/web').rglob('*'):
    if p.is_file(): app['web/'+p.relative_to(qualified/'cc2-control/web').as_posix()]=(p.read_bytes(),0o644)
info=json.loads(app['build-info.json'][0]);info.update({'trial':'connections-diagnostics-'+revision.lower(),'source_commit':commit,'automatic_network_selection':False,'unconditional_network_timer':False})
app['build-info.json']=(json.dumps(info,indent=2).encode()+b'\n',0o644)
app['SHA256SUMS']=(manifest(app),0o644)
app_archive=package/'cc2-control-payload.tar.gz';write_tar(app_archive,app)
driver=read_tar(old/'UE300/cc2-usb-lan-ue300-r1.tar.gz')
compat=read_tar(old/'cc2-network-compat-r1.tar.gz')
component={'control':((qualified/'control').read_bytes(),0o700)}
pins={
 'cc2_ue300_abi_probe.ko':'24c757788815abe5bbabb59d64b4768efedf7a65b1a8d93f3a6ce4d1e979a3f8',
 'cc2_ue300_mii.ko':'9051751ccefa1c1795b644764fd4c2de31b54d871e863927ef17b6d65978a7be',
 'cc2_ue300_r8152.ko':'5118667293fe05753f38c31bafe654d110104c6d7239ba8c2cfa1d3151b83ebd',
 'libcc2-network-compat-r1.so':'bde512195dd6bf3846f3103b5e1acbd89a62e5ee819e405389c97f5fc188ff3a',
}
for name,digest in pins.items():
    blob=(driver if name.endswith('.ko') else compat)[name][0];assert sha(blob)==digest
    component[name]=(blob,0o600)
for p in root.iterdir():
    if not p.is_file() or p.name=='IMPLEMENTATION.md':continue
    if p.suffix in ('.sh','.awk'):component[p.name]=(p.read_bytes(),0o700 if p.suffix=='.sh' else 0o600)
    if p.suffix in ('.c','.h','.py','.tsx','.json','.mjs','.sh','.awk','.in'):
        component['source/connections/'+p.name]=(p.read_bytes(),0o600)
for name,(blob,_) in driver.items():
    if name.startswith(('source/','reports/')):component['source/ue300/'+name]=(blob,0o600)
for name,(blob,_) in compat.items():
    if name.startswith('source/') or name in ('provenance.json','offline-validation.json','required-symbol-versions.txt'):
        component['source/compatibility/'+name]=(blob,0o600)
component['LICENSE.GPL-2.0']=(driver['LICENSE.GPL-2.0'][0],0o600)
component['LICENSE.GPL-3.0']=((root.parents[1]/'LICENSE').read_bytes(),0o600)
component['THIRD-PARTY-NOTICES.txt']=(b'USB kernel drivers and ABI probe: GPL-2.0, original Linux/Realtek authors and CC2 adaptation.\nConnection manager and CC2 Control: GPL-3.0, repository contributors.\nNetwork compatibility library: preserve source/compatibility/source/LICENSE and provenance.\nVendor binaries are not redistributed by this installer.\n',0o600)
component['validation.json']=((qualified/'validation.json').read_bytes(),0o600)
component['SHA256SUMS']=(manifest(component),0o600)
network_archive=package/'cc2-connections.tar.gz';write_tar(network_archive,component)
app_sha=sha(app_archive.read_bytes());net_sha=sha(network_archive.read_bytes())
runner=(root/'run-install.sh.in').read_text(encoding='utf-8').replace('@APP_SHA@',app_sha).replace('@NETWORK_SHA@',net_sha)
assert '@APP_SHA@' not in runner and '@NETWORK_SHA@' not in runner
text('run-install.sh',runner)
text('prepare-disk.sh', '''#!/bin/sh
set -eu
umask 077
[ "$(id -u)" = 0 ] || exit 1
stage=/opt/usr/cc2-control-connections-r5
[ ! -e "$stage" ] && [ ! -L "$stage" ] || { echo 'STOP: previous R5 stage exists; preserve its logs';exit 1; }
free=$(df -k /opt/usr | awk 'NR==2 {print $4}')
case "$free" in ''|*[!0-9]*) exit 1;;esac
[ "$free" -ge 65536 ] || { echo 'STOP: less than 64 MiB free on /opt/usr';exit 1; }
mkdir "$stage"
echo 'Disk staging prepared'
''')
runner_sha=sha((package/'run-install.sh').read_bytes())
text('verify-and-run.sh',f'''#!/bin/sh
set -eu
umask 077
BASE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
[ "$BASE" = /opt/usr/cc2-control-connections-r5 ] && [ ! -L "$BASE" ] || exit 1
[ "$(sha256sum "$BASE/run-install.sh" | awk '{{print $1}}')" = {runner_sha} ] || exit 1
exec sh "$BASE/run-install.sh"
''')
ps=(old/'Install-CC2-Control.ps1').read_text(encoding='utf-8')
ps=ps.replace('/opt/usr/cc2-control-network-trial-r4','/opt/usr/cc2-control-connections-r5')
ps=ps.replace('5ddff591e762078962e096a4e9e7d8d5044dd329f1568a8c868fb96f224ff87e',app_sha)
ps=ps.replace('71bf7dc43283a0bc8176c3c5ea32a89a6534fbe7780c58dc71b8122f88877318',runner_sha)
ps=ps.replace('3d6990a6ddb8d08fccd68634499f3a9e889752929014cafd49b3cecd2057a421',sha((package/'prepare-disk.sh').read_bytes()))
ps=ps.replace('7a32e049451a8798fa56d9261d0015026d32bde536aafb53904f0f6fc01c2de8',sha((package/'verify-and-run.sh').read_bytes()))
ps=ps.replace("Write-Host 'Uploading disk preparation script.'",f"$network = Join-Path $PSScriptRoot 'cc2-connections.tar.gz'\nif ((Get-FileHash -LiteralPath $network -Algorithm SHA256).Hash.ToLowerInvariant() -ne '{net_sha}') {{ throw 'Connections archive checksum mismatch' }}\nWrite-Host 'Uploading disk preparation script.'")
ps=ps.replace('scp.exe -O $archive $runner $verify','scp.exe -O $archive $network $runner $verify')
text('Install-CC2-Control.ps1',ps)
(package/'Install-Windows.cmd').write_bytes((old/'Install-Windows.cmd').read_bytes().replace(b' -ResetHostKey %*',b' %*'))
text('install-cc2-control.sh',(root/'unix-install.sh.in').read_text(encoding='utf-8'))
(package/'cc2-control-sources.zip').write_bytes((qualified/'integrated-sources.zip').read_bytes())
text('LEGGIMI.it.md','''# Connessioni — pacchetto completo con diagnosi R5

Pacchetto privato da validare sulla stampante. Non è una release pubblica.
Profilo qualificato: Community V4.4.2, base già verificata, TP-Link UE300
Realtek 0bda:8153 revisione 31fd. Il Wi-Fi resta disponibile anche senza adattatore.

1. Con stampante inattiva e fredda, target a zero e streaming chiuso,
   estrai tutto lo ZIP ed esegui Install-Windows.cmd. macOS/Linux:
   `sh install-cc2-control.sh`. Usa inizialmente l'indirizzo Wi-Fi.
2. Attendi INSTALLATION COMPLETE, poi ricarica CC2 Control con Ctrl+F5.
3. Apri Impostazioni → Connessioni. Abilita il supporto Ethernet se lo desideri.
   Il componente prepara da solo driver, indirizzo e compatibilità dei servizi.
4. Usa Commuta per scegliere la connessione. Se richiesto, apri il nuovo
   indirizzo e premi Mantieni questa connessione. Dopo la conferma non esiste
   un limite di tre minuti né un cambio automatico basato sul segnale.
5. Verifica video locale, Matrix tramite rete mobile, trasferimento Orca e
   ritorno al Wi-Fi. Solo dopo queste prove verifica un riavvio da inattiva.

Sono attivi cambio rete Wi-Fi, profili salvati, DHCP/IP manuale e attiva/disattiva
delle interfacce. Il gestore impedisce di spegnere l'ultima connessione abilitata.
Una modifica non confermata ripristina la precedente per evitare di perdere
l'accesso. Una selezione confermata viene salvata per i riavvii successivi.
Le password Wi-Fi passano al servizio originale e non sono incluse nei log.
Nessun codice LAN richiesto per i soli comandi di rete: Ricorda codice LAN,
aggiornamenti e integrazioni sperimentali mantengono i propri controlli.

Diagnosi è nel pannello. Log installazione: /opt/usr/cc2-control-connections-r5/.
Log rete: /opt/usr/cc2-network-state/action.log. Stato di sola lettura:
`/opt/usr/cc2-network-manager/control --status`.
Se l'installazione fallisce, conserva i log prima di riprovare. Gli archivi
sono rimossi dal disco della stampante soltanto dopo il completamento.
I backup dell'app e dei delegati di avvio restano in /opt/usr, con restore.sh.
L'installazione non riavvia la stampante e non avvia riscaldamento o movimento.
Alla prima attivazione Ethernet possono riavviarsi i servizi camera/stampante,
preservando le patch R2 e reactor. La restrizione WAN ELEGOO resta rispettata.

Non sono supportati in questa revisione altri chipset/kernel o reti Wi-Fi
Enterprise/WEP/WPA3-only. Le route DHCP aggiuntive non qualificate vengono
rifiutate senza interpretarle come comandi. Non pubblicare il pacchetto prima
della validazione fisica; le prove precedenti riguardavano il gestore temporaneo.
''')
text('README.en.md','''# Complete Connections diagnostics R5

Private hardware-validation package for the qualified Community V4.4.2 and
TP-Link UE300 (0bda:8153, bcdDevice 31fd). Not a public release.
Unzip fully, run Install-Windows.cmd or `sh install-cc2-control.sh`, while idle,
cold, heater targets zero and video paused. Installation stages on persistent
storage, retains logs/backups and removes payloads only after complete success.
Reload the page, open Settings → Connections and enable optional Ethernet.
Use Switch; open/confirm the new endpoint when requested. Confirmed changes
persist without a three-minute timer or signal-quality switching. Unconfirmed
changes recover the previous connection. Wi-Fi profiles use the original service.
Wi-Fi/IP configuration, interface toggles and Diagnostics are enabled. The LAN
code gate is removed only for network actions. Existing update/experiment gates,
camera R2/reactor patches and native WAN restrictions are preserved.
Verify local/cloud video, Orca transfer, Wi-Fi return and an idle reboot before
publishing. Installation never reboots, heats or moves the printer.
Full corresponding sources and GPL notices are included. See the Italian guide
for log paths, supported-profile limits and recovery details.
''')
text('validation.json',json.dumps({'revision':revision,'source_commit':commit,'ci_run':ci_run,'build':report,'app_payload_sha256':app_sha,'connections_payload_sha256':net_sha,'physical_validation':False},indent=2)+'\n')
files={p.relative_to(package).as_posix():(p.read_bytes(),0o644) for p in package.rglob('*') if p.is_file()}
text('SHA256SUMS',manifest(files).decode())
print(package)
print('Payload app:',app_sha,'network:',net_sha)

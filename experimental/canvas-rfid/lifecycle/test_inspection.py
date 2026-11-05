"""In-memory snapshot parser tests; no printer, subprocess or filesystem writes."""
import hashlib
import io
import tarfile
from inspection import ARCHIVE, FILES, META, MAX_FILE, parse_capture, remote_script

def fixture(entries,checksums=None):
    output=io.BytesIO();lines=[]
    with tarfile.open(fileobj=output,mode='w:') as archive:
        for name,data,kind in entries:
            info=tarfile.TarInfo(name);info.uid=info.gid=0;info.mode=0o600
            if kind=='file':
                info.size=len(data);archive.addfile(info,io.BytesIO(data))
                lines.append(f'FILE_SHA {hashlib.sha256(data).hexdigest()}  /{name}\n')
            else:
                info.type=tarfile.SYMTYPE if kind=='link' else tarfile.CHRTYPE
                info.linkname='/outside/not-followed';archive.addfile(info)
    return META+(''.join(lines) if checksums is None else checksums).encode()+ARCHIVE+output.getvalue()

def run():
    count=0
    def refused(blob):
        nonlocal count
        try:parse_capture(blob)
        except (ValueError,tarfile.TarError,UnicodeError):count+=1;return
        raise AssertionError('Invalid snapshot accepted')
    path=FILES[0];data=b'\x00\xff\r\nfixture\x00'
    good=fixture([(path,data,'file')])
    report,contents=parse_capture(good)
    assert list(contents.values())==[data] and report['files'][0]['uid']==0
    assert report['printer_modified'] is False;count+=1
    report,contents=parse_capture(fixture([('etc/rc.d/S23cc2-network-manager',b'','link')]))
    assert not contents and report['files'][0]['type']=='symlink';count+=1
    refused(fixture([('../escape',b'bad','file')]))
    refused(fixture([('/'+path,b'bad','file')]))
    refused(fixture([(path,data,'file'),(path,data,'file')]))
    refused(fixture([(path,data,'device')]))
    refused(fixture([(path,data,'file')],checksums='FILE_SHA '+'0'*64+'  /'+path+'\n'))
    refused(fixture([(path,b'x'*(MAX_FILE+1),'file')]))
    refused(fixture([(path,data,'file')],checksums=''))
    refused(b'x'*8193+good)
    refused(good.replace(ARCHIVE,b'\nmissing\n',1))
    refused(good.replace(META,b'bad-header\n',1))
    refused(good.replace(ARCHIVE,b'x'*262145+ARCHIVE,1))
    refused(good[:-10240])
    assert len(FILES)==len(set(FILES))
    assert not any(name.endswith(('.so','/elegoo_printer')) or 'wpa_supplicant' in name for name in FILES)
    script=remote_script()
    assert 'exec tar -cf - "$@"' in script
    assert not any(verb in script for verb in ['chmod ','chown ','rm ','mv ','cp ','reboot','insmod ','rmmod ','kill ','/proc/$P/mem'])
    count+=1
    print(f'PASS: {count} snapshot parser/collection cases; bounded binary preservation, checksum consistency, closed paths, no link extraction or mutation commands')
    return count

if __name__=='__main__':run()

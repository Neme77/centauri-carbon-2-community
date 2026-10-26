"""Extract the pinned official TLS source into the build directory only."""
import hashlib
from pathlib import Path
import tarfile

root = Path(__file__).resolve().parent.parent
archive = root / 'third-party/mbedtls-3.6.7.tar.bz2'
assert hashlib.sha256(archive.read_bytes()).hexdigest() == 'a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6'
destination = root / 'build/tls'
destination.mkdir(parents=True, exist_ok=True)
with tarfile.open(archive) as source:
    members = source.getmembers()
    assert all(m.name.startswith('mbedtls-3.6.7/') and '..' not in Path(m.name).parts and not m.issym() and not m.islnk() for m in members)
    source.extractall(destination, members=members, filter='data')
(destination / '.ready').write_text('mbedtls-3.6.7\n')

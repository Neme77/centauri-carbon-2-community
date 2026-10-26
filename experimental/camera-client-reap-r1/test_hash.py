import ctypes,hashlib,tempfile
from pathlib import Path
import sys
lib=ctypes.CDLL(sys.argv[1])
lib.cc2_sha256_file.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
lib.cc2_sha256_file.restype=ctypes.c_int
with tempfile.TemporaryDirectory() as temp:
    p=Path(temp)/'input'
    for n in [0,1,2,55,56,57,63,64,65,127,128,129,1000,4369420]:
        data=bytes((i*17+23)%256 for i in range(n));p.write_bytes(data)
        output=ctypes.create_string_buffer(65)
        assert lib.cc2_sha256_file(str(p).encode(),output)==0
        assert output.value.decode()==hashlib.sha256(data).hexdigest(),n
    assert lib.cc2_sha256_file(str(Path(temp)/'missing').encode(),output)==-1
print('PASS: SHA-256 vectors, padding boundaries, camera-sized input and missing input refusal')

"""Installer pins must bind the exact candidate, not just a valid manifest."""
import hashlib
import re

PIN=re.compile(rb'(?m)^EXPECTED=([0-9a-f]{64})$')

def expected(script):
    matches=PIN.findall(script)
    if len(matches)!=1:
        raise ValueError('Exactly one literal installer binary pin is required')
    return matches[0].decode('ascii')

def validate(script,binary):
    digest=hashlib.sha256(binary).hexdigest()
    if expected(script)!=digest:
        raise ValueError('Installer expects another binary; do not export this package')
    return digest

def bind_candidate(script,original_binary,candidate):
    validate(script,original_binary)
    digest=hashlib.sha256(candidate).hexdigest().encode('ascii')
    rebound,count=PIN.subn(b'EXPECTED='+digest,script)
    if count!=1 or PIN.sub(b'EXPECTED=@BINARY_SHA256@',script)!=PIN.sub(b'EXPECTED=@BINARY_SHA256@',rebound):
        raise ValueError('Installer changes exceed the candidate binary pin')
    validate(rebound,candidate)
    return rebound

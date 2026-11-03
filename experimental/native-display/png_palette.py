"""Independent PNG parser: verify checksums, dimensions and lossless export."""
from pathlib import Path
import hashlib
import json
import struct
import zlib



def decode(data):
    assert data[:8]==b'\x89PNG\r\n\x1a\n'
    p=8;compressed=[];header=None
    while p<len(data):
        length=struct.unpack_from('>I',data,p)[0];kind=data[p+4:p+8];body=data[p+8:p+8+length]
        assert len(body)==length
        assert zlib.crc32(kind+body)&0xffffffff==struct.unpack_from('>I',data,p+8+length)[0]
        if kind==b'IHDR':header=struct.unpack('>IIBBBBB',body)
        elif kind==b'IDAT':compressed.append(body)
        elif kind==b'IEND':assert p+12==len(data)
        p+=length+12
    assert p==len(data) and header is not None
    width,height,bits,color,compression,filter_method,interlace=header
    assert bits==8 and color in (2,6) and (compression,filter_method,interlace)==(0,0,0)
    bpp=3 if color==2 else 4;stride=width*bpp
    raw=zlib.decompress(b''.join(compressed));assert len(raw)==height*(stride+1)
    previous=bytearray(stride);rgba=bytearray()
    for y in range(height):
        start=y*(stride+1);mode=raw[start];assert mode<=4
        row=bytearray(raw[start+1:start+1+stride])
        for x in range(stride):
            a=row[x-bpp] if x>=bpp else 0;b=previous[x];c=previous[x-bpp] if x>=bpp else 0
            predict=0
            if mode==1:predict=a
            elif mode==2:predict=b
            elif mode==3:predict=(a+b)//2
            elif mode==4:
                q=a+b-c;pa,pb,pc=abs(q-a),abs(q-b),abs(q-c)
                predict=a if pa<=pb and pa<=pc else b if pb<=pc else c
            row[x]=(row[x]+predict)&255
        if bpp==4:rgba+=row
        else:
            for x in range(0,stride,3):rgba+=row[x:x+3]+b'\xff'
        previous=row
    return width,height,bytes(rgba)


def encode(width, height, rgba):
    if width < 1 or height < 1 or len(rgba) != width*height*4:
        raise ValueError("Invalid RGBA dimensions")
    def chunk(kind, body):
        return (struct.pack('>I', len(body))+kind+body+
                struct.pack('>I', zlib.crc32(kind+body)&0xffffffff))
    rows=b''.join(b'\0'+rgba[y*width*4:(y+1)*width*4] for y in range(height))
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+
            chunk(b'IDAT',zlib.compress(rows,9))+chunk(b'IEND',b''))


def recolor(data, entry):
    # Check exact owner-supplied bytes before decoding any resource.
    if hashlib.sha256(data).hexdigest()!=entry['baseline_sha256']:
        raise ValueError('Unsupported native glyph: '+entry['name'])
    width,height,rgba=decode(data)
    if (width,height)!=(entry['width'],entry['height']):
        raise ValueError('Native glyph dimensions changed')
    pixels=bytearray(rgba)
    for i in range(0,len(pixels),4):
        r,g,b,a=pixels[i:i+4]
        if a and g>r*1.3 and g>b*1.3:
            pixels[i:i+3]=bytes((126,234,216))
    if hashlib.sha256(pixels).hexdigest()!=entry['expected_rgba_sha256']:
        raise ValueError('Unexpected recolor result')
    output=encode(width,height,pixels)
    if decode(output)!=(width,height,bytes(pixels)):
        raise ValueError('PNG round-trip mismatch')
    return output

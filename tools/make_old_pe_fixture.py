#!/usr/bin/env python3
"""Generate an old-linker layout from an authored MinGW fixture, never game data."""
import argparse
from pathlib import Path
import struct
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('source',type=Path)
parser.add_argument('output',type=Path)
args=parser.parse_args()
b=bytearray(args.source.read_bytes())
def u16(at):return struct.unpack_from('<H',b,at)[0]
def u32(at):return struct.unpack_from('<I',b,at)[0]
def put(at,value):struct.pack_into('<I',b,at,value)
assert b[:2]==b'MZ'
pe=u32(60);assert b[pe:pe+4]==b'PE\0\0' and u16(pe+4)==0x14c
opt=pe+24;assert u16(opt)==0x10b
sections=[];table=opt+u16(pe+20)
for n in range(u16(pe+6)):
    at=table+n*40;assert at+40<=len(b)
    vs,va,raw,ptr=struct.unpack_from('<IIII',b,at+8)
    sections.append((va,max(vs,raw),raw,ptr))
    put(at+8,0)
    if u32(at+36)&0x80 and ptr==0:put(at+16,max(vs,4))
def offset(rva,count):
    for va,size,raw,ptr in sections:
        delta=rva-va
        if ptr and 0<=delta and delta+count<=raw and ptr+delta+count<=len(b):return ptr+delta
    raise ValueError('Fixture RVA has no file bytes')
imports=u32(opt+104)
if imports:
    n=0
    while True:
        at=offset(imports+n*20,20);ilt,stamp,forward,name,iat=struct.unpack_from('<IIIII',b,at)
        if not any((ilt,stamp,forward,name,iat)):break
        assert ilt and iat and name
        j=0
        while u32(offset(ilt+j*4,4)):
            put(offset(iat+j*4,4),0x00281000+j*16)
            j+=1;assert j<=65536
        n+=1;assert n<=1024
args.output.write_bytes(b)

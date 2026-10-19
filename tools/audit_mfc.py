import struct, re, sys
sys.path.insert(0,'.')
from mfc_abi import analyze

def imports(path):
    d=open(path,'rb').read()
    e=struct.unpack_from('<I',d,0x3C)[0]; coff=e+4
    _,nsec,_,_,_,osz,_=struct.unpack_from('<HHIIIHH',d,coff); opt=coff+20
    base=struct.unpack_from('<I',d,opt+28)[0]
    irva,_=struct.unpack_from('<II',d,opt+96+8)
    secs=[struct.unpack_from('<IIII',d,opt+osz+i*40+8) for i in range(nsec)]
    def off(r):
        for vs,va,rs,rp in secs:
            if va<=r<va+max(vs,rs): return rp+r-va
    out=[]; o=off(irva); k=0
    while True:
        oft,_,_,nr,iat=struct.unpack_from('<IIIII',d,o+k*20)
        if not(oft or nr or iat): break
        dll=d[off(nr):off(nr)+64].split(b'\0')[0].decode()
        to=off(oft or iat); j=0
        while True:
            v=struct.unpack_from('<I',d,to+j*4)[0]
            if not v: break
            if v&0x80000000: out.append((dll,v&0xFFFF,None,base+iat+j*4))
            else:
                h=off(v); out.append((dll,None,d[h+2:h+66].split(b'\0')[0].decode(),base+iat+j*4))
            j+=1
        k+=1
    return out

names={}
for m in re.finditer(r'ORDINAL="(\d+)" NAME="([^"]+)"', open('mfc42u.exports').read()):
    names[int(m.group(1))]=m.group(2)

reg={}
src=open('/home/claude/w/source/win32/host_functions.cpp').read()
for m in re.finditer(r'\{\s*"MFC42u\.DLL",\s*"",\s*(\d+),\s*CallConv::(\w+),\s*&(\w+),\s*(\d+)\s*\}', src):
    reg[int(m.group(4))]=(int(m.group(1)), m.group(2), m.group(3))

bad=[]; ok=0; unk=0
for dll,ordn,name,iat in imports(sys.argv[1]):
    if not dll.upper().startswith('MFC42') or ordn is None: continue
    nm=names.get(ordn,'?'); a=analyze(nm) if nm!='?' else None
    if not a or a.get('conv')=='data': continue
    if a['bytes'] is None: unk+=1; exp='?'
    else: exp=a['bytes'] if a['callee'] else 0
    if ordn in reg:
        ab,cc,fn=reg[ordn]; have=ab if cc=='Stdcall' else 0; who=fn
    else:
        have=0; who='(generic stub)'
    if exp=='?' or exp!=have:
        bad.append((ordn,nm,a['conv'],exp,have,who))
    else: ok+=1
print(f"MFC-импортов проверено: ok={ok}, расхождений/неизвестных={len(bad)} (из них неизвестный размер={unk})")
for b in sorted(bad, key=lambda x:(x[5]!='(generic stub)', x[0])):
    print("  ord %-5d %-9s ожид=%-3s у нас=%-3s %-32s %s" % (b[0], b[2], b[3], b[4], b[5], b[1]))

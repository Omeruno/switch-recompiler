# Разбор декорированных MSVC-имён MFC42u: соглашение вызова и сколько байт
# стековых аргументов чистит сама функция (callee-cleanup).
import re, struct, sys

STRUCT_SIZES = {'CPoint':8,'CSize':8,'CRect':16,'tagPOINT':8,'tagSIZE':8,'tagRECT':16,
                'CString':4,'CTime':4,'CTimeSpan':4,'_GUID':16,'tagVARIANT':16,'COleVariant':16,
                'CRuntimeClass':None,'_SYSTEMTIME':16,'_FILETIME':8,'tagMSG':28}
SIMPLE = {'C':4,'D':4,'E':4,'F':4,'G':4,'H':4,'I':4,'J':4,'K':4,'M':4,'N':8,'O':8}

class P:
    def __init__(s, t): s.t=t; s.i=0; s.names=[]; s.types=[]
    def peek(s): return s.t[s.i] if s.i < len(s.t) else ''
    def get(s): c=s.peek(); s.i+=1; return c
    def frag(s):
        c=s.peek()
        if c.isdigit():
            s.i+=1; n=int(c); return s.names[n] if n < len(s.names) else '?'
        if s.t.startswith('?$', s.i):  # шаблон: ?$Name@args@
            s.i+=2; name=s.ident(); depth=0
            # пропускаем аргументы шаблона до '@' на верхнем уровне
            while s.i < len(s.t):
                if s.peek()=='@': s.i+=1; break
                s.typ()
            return name
        return s.ident()
    def ident(s):
        j=s.t.index('@', s.i); name=s.t[s.i:j]; s.i=j+1
        if name not in s.names: s.names.append(name)
        return name
    def qname(s):
        parts=[]
        while s.peek()!='@' and s.i < len(s.t): parts.append(s.frag())
        s.i+=1
        return parts[0] if parts else '?'
    def typ(s):
        """возвращает размер в байтах на стеке (None = неизвестно)"""
        start=s.i; c=s.get()
        if c in SIMPLE: return SIMPLE[c]
        if c=='X': return 0
        if c=='_':
            d=s.get(); return 8 if d in 'JK' else 4
        if c in 'PQRS':
            cv=s.get()
            if cv=='6':  # указатель на функцию
                s.get(); s.typ()
                while s.peek() not in ('@','Z',''): s.typ()
                s.i+=1
                if s.peek()=='Z': s.i+=1
                sz=4
            else:
                s.typ(); sz=4
            return sz
        if c=='A':
            s.get(); s.typ(); return 4
        if c in 'VUT':
            name=s.qname(); sz=STRUCT_SIZES.get(name, None) if c!='T' else None
            return sz
        if c=='W':
            s.get(); s.qname(); return 4
        if c=='?':  # ?A / ?B модификатор возвращаемого класса
            s.get(); return s.typ()
        if c.isdigit():
            n=int(c); return s.types[n][1] if n < len(s.types) else None
        raise ValueError('тип %r в %s@%d' % (c, s.t, s.i))
    def _remember(s, start, sz=4):
        if s.i-start > 1 and len(s.types) < 10: s.types.append((s.t[start:s.i], sz))

def analyze(mangled):
    m=re.sub(r'^Duplicate\d+_', '', mangled)
    if not m.startswith('?'): return None
    p=P(m); p.i=1
    if p.t.startswith('?', p.i):   # операторы/конструкторы: ?0 ?1 ?_G ?4 и т.п.
        p.i+=1
        if p.peek()=='_': p.i+=1
        p.i+=1
        ctor=m[1:3] in ('?0','?1')
    else:
        p.frag(); ctor=False
    # имена классов до '@@'
    while p.peek()!='@': p.frag()
    p.i+=1
    acc=p.get()
    if acc in 'YZ': member=False; static=True
    elif acc in 'CDKLST': member=True; static=True
    elif acc in 'ABEFIJMNQRUV': member=True; static=False
    else: return {'conv':'data','bytes':0}
    if member and not static: p.get()   # cv для this
    cc=p.get()
    conv={'A':'cdecl','E':'thiscall','G':'stdcall','I':'fastcall'}.get(cc,'?'+cc)
    hidden=0
    if p.peek()=='@': p.i+=1            # ctor/dtor: нет возвращаемого типа
    else:
        # возврат класса ПО ЗНАЧЕНИЮ (?AV.../?AU...) — MSVC передаёт скрытый
        # указатель на слот результата первым стековым аргументом (+4)
        if p.t.startswith('?A', p.i) and p.t[p.i+2:p.i+3] in ('V','U'): hidden=4
        p.typ()
    total=hidden; unknown=False; vararg=False
    if p.peek()=='X': p.i+=1
    else:
        while p.i < len(p.t) and p.peek() not in ('@','Z'):
            st=p.i; sz=p.typ(); p._remember(st, sz)
            if sz is None: unknown=True
            else: total+=sz
        if p.peek()=='Z': vararg=True
    callee = conv in ('thiscall','stdcall') and not vararg
    return {'conv':conv,'bytes':(None if unknown else total),'callee':callee,'this':member and not static}

if __name__=='__main__':
    for t in sys.argv[1:]: print(t, analyze(t))

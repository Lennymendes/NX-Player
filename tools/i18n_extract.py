# Extracts the translatable keys: sequences of adjacent C++ string literals where one carries _t.
import re,glob,json,sys
def unescape(s):
    out=bytearray(); i=0; b=s.encode('utf-8')
    while i<len(b):
        c=b[i]
        if c==92 and i+1<len(b):
            n=chr(b[i+1])
            if n=='n': out.append(10); i+=2; continue
            if n=='t': out.append(9); i+=2; continue
            if n in '"\'\\': out.append(ord(n)); i+=2; continue
            if n=='x':
                m=re.match(rb'[0-9a-fA-F]{1,2}',b[i+2:i+4]); out.append(int(m.group(0),16)); i+=2+len(m.group(0)); continue
        out.append(c); i+=1
    return out.decode('utf-8',errors='replace')
def tokens(src):
    """yields ('str', text, suffix) or ('other',) tokens; skips comments and char literals."""
    i=0; n=len(src)
    while i<n:
        c=src[i]
        if src.startswith('//',i): i=src.find('\n',i); i=n if i<0 else i; continue
        if src.startswith('/*',i): j=src.find('*/',i+2); i=n if j<0 else j+2; continue
        if c=='R' and i+1<n and src[i+1]=='"' and (i==0 or not (src[i-1].isalnum() or src[i-1]=='_')):
            m=re.match(r'R"([^(]*)\(',src[i:]); d=m.group(1); end=src.find(')'+d+'"',i); i=end+len(d)+2; yield ('other',); continue
        if c=="'":
            j=i+1
            while j<n and src[j]!="'":
                j+=2 if src[j]=='\\' else 1
            i=j+1; yield ('other',); continue
        if c=='"':
            j=i+1
            while j<n and src[j]!='"':
                j+=2 if src[j]=='\\' else 1
            text=src[i+1:j]; i=j+1
            suf=''
            if src.startswith('_t',i) and not (i+2<n and (src[i+2].isalnum() or src[i+2]=='_')): suf='_t'; i+=2
            yield ('str',text,suf); continue
        if c.isspace(): i+=1; continue
        yield ('other',); i+=1
keys=[]
for f in sorted(glob.glob('Source/**/*.cpp',recursive=True)+glob.glob('Source/**/*.h',recursive=True)):
    if '/win/' in f: continue
    seq=[]
    def flush():
        if seq and any(s[2] for s in seq):
            k=unescape(''.join(s[1] for s in seq))
            if k.strip() and k not in keys: keys.append(k)
        seq.clear()
    for t in tokens(open(f,encoding='utf-8').read()):
        if t[0]=='str': seq.append(t)
        else: flush()
    flush()
json.dump(keys,open(sys.argv[1],'w'),ensure_ascii=False,indent=0)
print(len(keys))

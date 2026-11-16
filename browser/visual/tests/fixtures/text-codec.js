// Owned Encoding API cases; the same fixture runs in native QuickJS and Chromium.
(()=>{
 const check=(ok,label)=>{if(!ok)throw Error(label);};
 const throws=(fn,name)=>{let error;try{fn();}catch(e){error=e;}check(error?.name===name,'Expected '+name+', got '+error);};
 const bytes=(...v)=>new Uint8Array(v),enc=new TextEncoder();
 for(const label of ['utf8',' UTF-8\n','unicode-1-1-utf-8','unicode11utf8','unicode20utf8','x-unicode20utf8'])check(new TextDecoder(label).encoding==='utf-8','UTF-8 label');
 for(const label of ['utf-16','ucs-2','unicode','unicodefeff'])check(new TextDecoder(label).encoding==='utf-16le','UTF-16LE alias');
 check(new TextDecoder('unicodefffe').encoding==='utf-16be','BE alias');
 for(const label of ['ascii','latin1','iso-8859-1','windows-1252'])check(new TextDecoder(label).encoding==='windows-1252','Latin1 aliases use Windows index');
 for(const label of ['replacement','utf16','\vutf-8','\u00a0utf-8','utf-8\u00a0','unknown'])throws(()=>new TextDecoder(label),'RangeError');
 const conversionError={owned:true};let conversionCaught;try{new TextDecoder('not-an-encoding',{get fatal(){throw conversionError;}});}catch(e){conversionCaught=e;}check(conversionCaught===conversionError,'Dictionary conversion precedes label validation');
 throws(()=>new TextDecoder(Symbol()),'TypeError');throws(()=>new TextDecoder('utf-8',7),'TypeError');
 const strict=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true});check(strict.fatal&&strict.ignoreBOM&&strict.encoding==='utf-8','Decoder options');
 throws(()=>TextDecoder.prototype.decode.call({},bytes(1)),'TypeError');throws(()=>Object.getOwnPropertyDescriptor(TextDecoder.prototype,'fatal').get.call({}),'TypeError');
 throws(()=>TextEncoder.prototype.encode.call({},'x'),'TypeError');throws(()=>enc.encode(Symbol()),'TypeError');throws(()=>enc.encodeInto('x'),'TypeError');throws(()=>enc.encodeInto('x',new Uint8ClampedArray(4)),'TypeError');
 for(const text of ['', 'a\0b', 'Ёж 🦔', '\ud800', '\udc00', '\ud800x\udc00', '\ud800\ud800\udc00']){
  const normalized=text.replace(/[\ud800-\udbff](?![\udc00-\udfff])|(?<![\ud800-\udbff])[\udc00-\udfff]/g,'\ufffd');
  const encoded=enc.encode(text);check(new TextDecoder('utf-8',{fatal:true}).decode(encoded)===normalized,'Unicode scalar roundtrip');
  for(let size=0;size<=encoded.length+1;size++){
   const backing=new Uint8Array(size+4);backing.fill(73);const into=backing.subarray(2,2+size),result=enc.encodeInto(text,into);
   check(backing[0]===73&&backing[1]===73&&backing.at(-1)===73,'Destination view boundaries');
   check(result.written<=size&&result.read<=text.length,'Counts bounded');
   check(new TextDecoder('utf-8',{fatal:true}).decode(into.subarray(0,result.written))===new TextDecoder().decode(enc.encode(text.slice(0,result.read))),'encodeInto whole scalar prefix');
   check([...into.subarray(0,result.written)].join()=== [...encoded.subarray(0,result.written)].join(),'Prefix bytes');
   if(result.read<text.length)check(result.written===size||enc.encode(text.slice(result.read,result.read+2)).length>size-result.written,'Destination cannot fit next scalar');
  }
 }
 check(enc.encode().length===0&&enc.encode(undefined).length===0,'Encoder optional source');const undefinedOut=new Uint8Array(20);const undefinedCount=enc.encodeInto(undefined,undefinedOut);check(undefinedCount.read===9&&undefinedCount.written===9,'encodeInto required source coerces undefined');
 check([...enc.encode('\ud800')].join()==='239,191,189','Lone surrogate replacement');
 const bad=[[0x80],[0xc0,0xaf],[0xed,0xa0,0x80],[0xf4,0x90,0x80,0x80],[0xf5],[0xe2,0x82],[0xe2,0x41],[0xf0,0x80,0x80,0x80]];
 for(const v of bad){throws(()=>new TextDecoder('utf-8',{fatal:true}).decode(new Uint8Array(v)),'TypeError');}
 check(new TextDecoder().decode(bytes(0xe2,0x82))==='\ufffd','Truncated sequence one replacement');check(new TextDecoder().decode(bytes(0xe2,0x41))==='\ufffdA','Reconsume non-continuation');
 const samples=[enc.encode('Привет\0🦔Z'),bytes(239,187,191,65,239,187,191,66),bytes(0xe2,0x82,0x41,0xff,66),bytes(0xf4,0x90,0x80,0x80)];
 for(const source of samples)for(let split=0;split<=source.length;split++){
  const d=new TextDecoder(),text=d.decode(source.subarray(0,split),{stream:true})+d.decode(source.subarray(split));check(text===new TextDecoder().decode(source),'Chunk split invariant');
 }
 for(const ignoreBOM of [false,true]){
  const d=new TextDecoder('utf-8',{ignoreBOM});let text='';for(const b of [239,187,191,65,239,187,191,66])text+=d.decode(bytes(b),{stream:true});text+=d.decode();check(text===(ignoreBOM?'\ufeffA\ufeffB':'A\ufeffB'),'Streaming BOM');check(d.decode(bytes(239,187,191,67))===(ignoreBOM?'\ufeffC':'C'),'BOM reset after flush');
 }
 const fatal=new TextDecoder('utf-8',{fatal:true});check(fatal.decode(bytes(0xe2),{stream:true})==='','Strict partial');check(fatal.decode(bytes(0x82,0xac))==='€','Strict chunk completion');throws(()=>fatal.decode(bytes(0xe2)),'TypeError');check(fatal.decode(bytes(65))==='A','Reset after nonstream error');
 const reentry=new TextDecoder('utf-8',{fatal:true});reentry.decode(bytes(0xe2),{stream:true});check(reentry.decode(bytes(0xac),{get stream(){reentry.decode(bytes(0x82),{stream:true});return true;}})==='€','Reentrant option getter observes current state');
 for(const [label,source,text] of [['utf-16le',bytes(0xff,0xfe,0x41,0,0x3e,0xd8,0x94,0xdd),'A🦔'],['utf-16be',bytes(0xfe,0xff,0,0x41,0xd8,0x3e,0xdd,0x94),'A🦔']]){
  for(let split=0;split<=source.length;split++){const d=new TextDecoder(label,{fatal:true});check(d.decode(source.subarray(0,split),{stream:true})+d.decode(source.subarray(split))===text,'UTF-16 stream split');}
  check(new TextDecoder(label,{ignoreBOM:true}).decode(source)==='\ufeff'+text,'UTF-16 preserve BOM');
 }
 const utf16bad=[bytes(0x00,0xd8),bytes(0x00,0xdc),bytes(65),bytes(0x00,0xd8,65,0),bytes(0x00,0xd8,65)];
 for(const v of utf16bad)throws(()=>new TextDecoder('utf-16le',{fatal:true}).decode(v),'TypeError');
 check(new TextDecoder('utf-16le').decode(bytes(0,0xd8,65,0))==='\ufffdA','UTF-16 reconsume unit');check(new TextDecoder('utf-16le').decode(bytes(0,0xd8,65))==='\ufffd','UTF-16 odd EOF with high surrogate');
 
 check(new TextDecoder('windows-1251',{fatal:true}).decode(bytes(0xcf,0xf0,0xe8,0xe2,0xe5,0xf2,0x98))==='Привет\u0098','Windows-1251');check(new TextDecoder('latin1',{fatal:true}).decode(bytes(0x80,0x81,0x91))==='€\u0081‘','Windows-1252 C1 slots');
 const raw=bytes(9,9,0xd0,0x81,9,9);check(new TextDecoder().decode(new DataView(raw.buffer,2,2))==='Ё','DataView slice');check(new TextDecoder().decode(new Uint16Array(raw.buffer,2,1))==='Ё','TypedArray bytes rather than elements');
 const hostile=bytes(65);for(const name of ['buffer','byteOffset','byteLength'])Object.defineProperty(hostile,name,{get(){throw Error('View property must not run');}});check(new TextDecoder().decode(hostile)==='A','Input intrinsic slots');const hostileBuffer=new ArrayBuffer(1);new Uint8Array(hostileBuffer)[0]=66;Object.defineProperty(hostileBuffer,'constructor',{get(){throw Error('Species must not run');}});check(new TextDecoder().decode(hostileBuffer)==='B','Decode without species');
 const detached=new ArrayBuffer(4),detachedArray=new Uint8Array(detached),detachedView=new DataView(detached);structuredClone(detached,{transfer:[detached]});for(const source of [detached,detachedArray,detachedView])check(new TextDecoder().decode(source)==='','Detached decode is empty');check(enc.encodeInto('x',detachedArray).written===0,'Detached encodeInto is empty');
 const rab=new ArrayBuffer(4,{maxByteLength:8}),oob=new Uint8Array(rab,2,2);rab.resize(1);throws(()=>new TextDecoder().decode(oob),'TypeError');
 throws(()=>new TextDecoder().decode(null),'TypeError');throws(()=>new TextDecoder().decode([65]),'TypeError');throws(()=>new TextDecoder().decode(bytes(65),7),'TypeError');
 const target=new Uint8Array(8);let inheritedCalls=0;
 try{
  for(const key of ['read','written','state','pending','error','text'])Object.defineProperty(Object.prototype,key,{configurable:true,set(){inheritedCalls++;throw Error('Inherited result setter ran');}});
  const result=enc.encodeInto('Ё',target);check(result.read===1&&result.written===2&&new TextDecoder().decode(target.subarray(0,2))==='Ё','Own result data properties');check(inheritedCalls===0,'No inherited setters in native codec');
 }finally{for(const key of ['read','written','state','pending','error','text'])delete Object.prototype[key];}
 const fingerprint=text=>{let value=2166136261;for(let i=0;i<text.length;i++)value=Math.imul(value^text.charCodeAt(i),16777619)>>>0;return value;};
 const allBytes=new Uint8Array(256);for(let i=0;i<256;i++)allBytes[i]=i;
 // Independent reference values from Chromium 151, covering every legacy index slot.
 for(const [label,expected] of [['windows-1251',2254105437],['windows-1252',521122991],['utf-8',2281756101],['utf-16le',1072706065],['utf-16be',2684329067]])check(fingerprint(new TextDecoder(label).decode(allBytes))===expected,'Complete byte corpus '+label);
 globalThis.textCodecDone=true;
})();

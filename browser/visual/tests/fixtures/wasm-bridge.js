(()=>{
 const check=(ok,message)=>{if(!ok)throw Error(message);},bytes=name=>new Uint8Array(wasmBinaries[name]);
 const throws=(fn,type,message)=>{let error;try{fn();}catch(e){error=e;}check(error instanceof type,message||('expected '+type.name));return error;};
 check(!WebAssembly.validate(new Uint8Array([0,1,2])),'invalid validate');check(WebAssembly.validate(bytes('bridge')),'valid validate');throws(()=>new WebAssembly.Module(new Uint8Array([0])),WebAssembly.CompileError);
 const module=new WebAssembly.Module(bytes('bridge'));check(Object.prototype.toString.call(module)==='[object WebAssembly.Module]','Module brand');
 const imports=WebAssembly.Module.imports(module);check(imports.length===1&&imports[0].module==='env'&&imports[0].name==='mix'&&imports[0].kind==='function','imports');
 check(WebAssembly.Module.exports(module).some(item=>item.name==='memory'&&item.kind==='memory'),'exports');check([...new Uint8Array(WebAssembly.Module.customSections(module,'owned')[0])].join()==='1,2,3','custom section');check(WebAssembly.Module.customSections(module,'missing').length===0,'missing section');
 const instance=new WebAssembly.Instance(module,{env:{mix:(a,b,c,d)=>BigInt(a)+b+BigInt(c+d)}}),e=instance.exports;
 check(e.add(40,2)===42&&e.add.length===2,'numeric function');check(e.mixed(2,7n,1.5,2.5)===13n,'typed JS import');throws(()=>e.mixed(2,7,1.5,2.5),TypeError,'i64 rejects Number');
 check(e.multi(9).join()==='9,123,1.5,2.5','multi value');check(Object.isExtensible(e)===false&&Object.getOwnPropertyDescriptor(e,'add').writable===false,'immutable export object');
 check(e.counter instanceof WebAssembly.Global&&e.counter.value===7,'ctors not auto run');e.counter.value=13;check(e.counter.value===13,'global write');e.__wasm_call_ctors();check(e.counter.value===99,'explicit ctor');
 check(e.memory instanceof WebAssembly.Memory,'memory class');const old=e.memory.buffer;new Uint8Array(old)[0]=123;check(e.read(0)===123,'real zero-copy memory');check(e.memory.buffer===old,'buffer identity');check(e.grow(1)===1&&old.byteLength===0,'Wasm growth detaches');const grown=e.memory.buffer;check(grown.byteLength===131072&&new Uint8Array(grown)[0]===123&&new Uint8Array(grown)[65536]===0,'growth retains data/zeros');e.growZero();check(grown.byteLength===0,'opcode grow zero detaches');
 const stable=e.memory.buffer;check(e.grow(20)===-1&&stable.byteLength===131072,'failed opcode growth preserves buffer');check(e.memory.grow(0)===2&&stable.byteLength===0,'JS grow zero');throws(()=>e.memory.grow(20),RangeError,'JS failed grow');
 throws(()=>e.read(131071),WebAssembly.RuntimeError,'bounds trap');throws(()=>e.trap(),WebAssembly.RuntimeError,'unreachable trap');check(e.add(1,2)===3,'instance usable after trap');
 check(e.table instanceof WebAssembly.Table&&e.table.length===2,'table export');const hidden=e.table.get(0);check(hidden()===77&&hidden===e.table.get(0),'table function identity');e.table.set(1,hidden);check(e.indirect(1)===77,'table write');check(e.table.grow(1)===2&&e.table.get(2)===null,'table growth');throws(()=>e.table.get(8),RangeError);throws(()=>e.table.grow(20),RangeError);
 throws(()=>e.hang(),WebAssembly.RuntimeError,'instruction budget');check(e.add(2,3)===5,'recover after budget');
 let starts=0;new WebAssembly.Instance(new WebAssembly.Module(bytes('start')),{env:{started:()=>starts++}});new WebAssembly.Instance(new WebAssembly.Module(bytes('start-import')),{env:{started:()=>starts++}});check(starts===2,'start sections including imported start');
 const reason={owned:'exception'};try{new WebAssembly.Instance(new WebAssembly.Module(bytes('start')),{env:{started:()=>{throw reason;}}});throw Error('missing import exception');}catch(e){check(e===reason,'original JS exception preserved');}
 throws(()=>new WebAssembly.Instance(new WebAssembly.Module(bytes('start-loop'))),WebAssembly.RuntimeError,'bounded start');
 const multi=new WebAssembly.Instance(new WebAssembly.Module(bytes('multi-import')),{env:{values:()=>[5,6n]}});check(multi.exports.values().join()==='5,6','multi result import');
 const standalone=new WebAssembly.Memory({initial:1,maximum:2});check(standalone.buffer.byteLength===65536&&standalone.grow(1)===1,'standalone memory');throws(()=>new WebAssembly.Instance(new WebAssembly.Module(bytes('import-memory')),{env:{memory:standalone}}),WebAssembly.LinkError,'import memory explicitly rejected');
 const global=new WebAssembly.Global({value:'i32'},23);check(global.value===23&&global.valueOf()===23,'standalone global');throws(()=>{global.value=24;},TypeError,'immutable global');
 const globalModule=new WebAssembly.Module(bytes('global-import'));check(new WebAssembly.Instance(globalModule,{env:{value:global}}).exports.value()===23,'immutable global import');
 const table=new WebAssembly.Table({element:'anyfunc',initial:1,maximum:3});check(table.length===1&&table.get(0)===null&&table.grow(1)===1,'standalone table');throws(()=>table.set(0,e.add),TypeError,'cross instance refs rejected');
 globalThis.wasmBridgeDone=true;
})();

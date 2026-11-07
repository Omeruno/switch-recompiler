(()=>{
 const check=(ok,label)=>{if(!ok)throw Error(label);},module=name=>new WebAssembly.Module(new Uint8Array(wasmBinaries[name]));
 const throws=(fn,type,label)=>{let error;try{fn();}catch(e){error=e;}check(error instanceof type,label||('Expected '+type.name));};
 const memory=new WebAssembly.Memory({initial:1,maximum:4}),counter=new WebAssembly.Global({value:'i32',mutable:true},10),wide=new WebAssembly.Global({value:'i64',mutable:true},20n);
 const compiled=module('external-imports');let notification=()=>{};
 const env={memory,counter,wide,notify:()=>notification()};
 Object.defineProperty(counter,'value',{get(){throw Error('Global import consulted user getter');},configurable:true});
 const first=new WebAssembly.Instance(compiled,{env}).exports,second=new WebAssembly.Instance(compiled,{env}).exports;
 delete counter.value;
 check(first.memory===memory&&second.memory===memory&&first.counter===counter&&second.wide===wide,'import/re-export object identity');
 check(new Uint8Array(memory.buffer)[128]===111,'active data into imported memory');
 new Uint8Array(memory.buffer)[4]=43;check(first.read(4)===43&&second.read(4)===43,'zero-copy read across instances');first.write(5,67);check(new Uint8Array(memory.buffer)[5]===67&&second.read(5)===67,'Wasm write across instances');
 check(first.inc()===11&&counter.value===11&&second.inc()===12,'shared mutable i32 storage');counter.value=30;check(second.inc()===31&&first.counter.value===31,'JS mutation reaches both modules');
 check(first.incWide()===22n&&wide.value===22n&&second.incWide()===24n,'shared mutable i64');wide.value=100n;check(first.incWide()===102n,'JS i64 mutation');
 const old=memory.buffer;notification=()=>check(old.byteLength===0&&first.memory.buffer===second.memory.buffer&&memory.buffer.byteLength===131072,'immediate imported grow detachment inside host callback');check(first.grow(1)===1&&second.size()===2,'grow imported memory');
 const stable=memory.buffer;notification=()=>{};check(second.grow(10)===-1&&stable.byteLength===131072,'failed imported grow retains backing');notification=()=>check(stable.byteLength===0,'grow(0) detaches canonical buffer');check(second.grow(0)===2,'imported grow zero');notification=()=>{};
 const low=new WebAssembly.Memory({initial:0,maximum:4});check(low.buffer.byteLength===0,'zero-page buffer');throws(()=>new WebAssembly.Instance(compiled,{env:{...env,memory:low}}),WebAssembly.LinkError,'minimum validation');
 const limited=module('import-memory');throws(()=>new WebAssembly.Instance(limited,{env:{memory}}),WebAssembly.LinkError,'maximum validation');
 const unbounded=new WebAssembly.Memory({initial:1});throws(()=>new WebAssembly.Instance(limited,{env:{memory:unbounded}}),WebAssembly.LinkError,'missing maximum rejected');
 throws(()=>new WebAssembly.Instance(compiled,{env:{...env,memory:{buffer:memory.buffer}}}),WebAssembly.LinkError,'fake memory rejected');throws(()=>new WebAssembly.Instance(compiled,{env:{...env,counter:1}}),WebAssembly.LinkError,'mutable global requires object');throws(()=>new WebAssembly.Instance(compiled,{env:{...env,counter:wide}}),WebAssembly.LinkError,'global type checked');
 const otherMemory=new WebAssembly.Memory({initial:1,maximum:4}),otherCounter=new WebAssembly.Global({value:'i32',mutable:true},70);
 const third=new WebAssembly.Instance(compiled,{env:{...env,memory:otherMemory,counter:otherCounter}}).exports;third.write(5,99);check(second.read(5)===67&&third.read(5)===99,'module reuse isolates import bindings');check(third.inc()===71&&counter.value===31,'global reuse isolates bindings');
 const exception={owned:'start-failure'},before=memory.buffer;try{new WebAssembly.Instance(module('import-start-fail'),{env:{memory,boom:()=>{throw exception;}}});throw Error('missing start exception');}catch(e){check(e===exception,'original start exception');}
 check(before.byteLength===0&&new Uint8Array(memory.buffer)[256]===85&&second.read(256)===85,'failed start preserves live imports and side effects');
 check(low.grow(1)===0,'grow previously rejected memory');const fourth=new WebAssembly.Instance(compiled,{env:{...env,memory:low}}).exports;check(fourth.memory===low&&fourth.read(128)===111,'failed link does not poison later instantiation');
 globalThis.wasmImportsDone=true;
})();

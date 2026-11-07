(()=>{
 const check=(ok,label)=>{if(!ok)throw Error(label);},module=name=>new WebAssembly.Module(new Uint8Array(wasmBinaries[name]));
 const throws=(fn,type,label)=>{let error;try{fn();}catch(e){error=e;}check(error instanceof type,label||('Expected '+type.name));};
 const memory=new WebAssembly.Memory({initial:1,maximum:4}),old=memory.buffer,compiled=module('import-callback');
 const grower=new WebAssembly.Instance(compiled,{env:{memory,notify:()=>{}}}).exports;
 const reader=new WebAssembly.Instance(compiled,{env:{memory,notify:()=>{check(grower.grow(1)===1,'nested module growth');new Uint8Array(memory.buffer)[65540]=123;check(old.byteLength===0,'nested grow detachment');}}}).exports;
 check(reader.probe(65540)===123,'active caller refreshes bounds after host/nested growth');
 const stable=memory.buffer;throws(()=>new WebAssembly.Instance(module('import-data-fail'),{env:{memory}}),WebAssembly.RuntimeError,'data initialization bounds trap');check(stable===memory.buffer&&new Uint8Array(memory.buffer)[65540]===123,'failed initialization preserves live backing');
 const failure={owned:'nested-exception'},faulty=new WebAssembly.Instance(compiled,{env:{memory,notify:()=>{throw failure;}}}).exports;
 try{faulty.probe(0);throw Error('missing callback throw');}catch(e){check(e===failure,'callback exception identity');}check(grower.grow(0)===2,'recovery after callback failure');
 if(!globalThis.wasmReference){const table=new WebAssembly.Table({element:'anyfunc',initial:1,maximum:4});throws(()=>new WebAssembly.Instance(module('import-table'),{env:{table}}),WebAssembly.LinkError,'unsupported table imports fail closed');}
 globalThis.wasmImportsDone=true;
})();

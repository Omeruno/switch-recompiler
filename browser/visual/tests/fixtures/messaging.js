// Shared with Chromium. Owned data only; no provider challenge is submitted.
(async()=>{
 const check=(ok,label)=>{if(!ok)throw Error(label);};
 const throws=(fn,name)=>{let error;try{fn();}catch(e){error=e;}check(error?.name===name,'Expected '+name+', got '+error);};
 const next=(port,start=true)=>new Promise(resolve=>{port.addEventListener('message',resolve,{once:true});if(start)port.start();});
 const delay=()=>new Promise(resolve=>setTimeout(resolve,5));
 try{
  throws(()=>new MessagePort(),'TypeError');throws(()=>MessagePort.prototype.start.call({}),'TypeError');
  const original={text:'Привет',zero:-0,nan:NaN,infinite:Infinity,big:12345678901234567890n,missing:undefined};original.self=original;
  original.array=[];original.array.length=4;original.array[2]=original;
  original.map=new Map([[original,new Set([original,7])]]);original.date=new Date(1234567);original.regexp=/ab+/gi;original.regexp.lastIndex=8;
  const buffer=new ArrayBuffer(24);original.bytes=new Uint8Array(buffer,4,8);original.bytes.fill(91);original.view=new DataView(buffer,2,12);original.buffer=buffer;
  Object.defineProperty(original,'__proto__',{value:{polluted:true},enumerable:true});
  const copy=structuredClone(original);
  check(copy!==original&&copy.self===copy&&copy.array[2]===copy&&!(0 in copy.array)&&copy.array.length===4,'cycles, holes');
  check(copy.text==='Привет'&&Object.is(copy.zero,-0)&&Number.isNaN(copy.nan)&&copy.big===original.big&&Object.hasOwn(copy,'missing')&&copy.missing===undefined,'primitive clones');
  check(copy.map.get(copy).has(copy)&&copy.date.getTime()===1234567&&copy.regexp.source==='ab+'&&copy.regexp.flags==='gi'&&copy.regexp.lastIndex===0,'map/set/date/regexp');
  check(copy.bytes.buffer===copy.buffer&&copy.view.buffer===copy.buffer&&copy.bytes.byteOffset===4&&copy.bytes[0]===91&&copy.view.byteLength===12,'shared backing views');
  check(Object.getPrototypeOf(copy)===Object.prototype&&copy.__proto__.polluted&&!({}).polluted,'prototype pollution resistance');
  for(const value of [()=>1,Symbol(),document.body,new WeakMap(),Promise.resolve()])throws(()=>structuredClone(value),'DataCloneError');
  const hostileBuffer=new ArrayBuffer(4),hostileView=new Uint8Array(hostileBuffer);hostileView[0]=93;Object.defineProperty(hostileBuffer,'constructor',{get(){throw Error('Species must not run');}});for(const key of ['buffer','byteOffset','length'])Object.defineProperty(hostileView,key,{get(){throw Error('View getter must not run');}});const hostileCopy=structuredClone({hostileBuffer,hostileView});check(hostileCopy.hostileView.buffer===hostileCopy.hostileBuffer&&hostileCopy.hostileView[0]===93,'native buffer snapshot and view slots');
  const cause={};const error=new TypeError('owned error',{cause});cause.error=error;const copiedError=structuredClone(error);check(copiedError instanceof TypeError&&copiedError.message==='owned error'&&copiedError.cause.error===copiedError,'error and cyclic cause');
  const empty=structuredClone(new ArrayBuffer(0));check(empty.byteLength===0,'empty buffer');const emptyTransfer=new ArrayBuffer(0);structuredClone(emptyTransfer,{transfer:[emptyTransfer]});throws(()=>structuredClone(emptyTransfer),'DataCloneError');
  const wasmMemory=new WebAssembly.Memory({initial:1,maximum:2});const wasmBuffer=wasmMemory.buffer;new Uint8Array(wasmBuffer)[0]=41;
  throws(()=>structuredClone(wasmBuffer,{transfer:[wasmBuffer]}),'TypeError');check(wasmBuffer.byteLength===65536&&new Uint8Array(wasmMemory.buffer)[0]===41,'WASM backing must remain attached');const wasmCopy=structuredClone(wasmBuffer);check(new Uint8Array(wasmCopy)[0]===41&&wasmCopy!==wasmBuffer,'WASM backing may be copied');
  const moved=new ArrayBuffer(4);new Uint8Array(moved)[0]=77;const result=structuredClone({moved},{transfer:[moved]});check(moved.byteLength===0&&new Uint8Array(result.moved)[0]===77,'buffer transfer');
  const keep=new ArrayBuffer(4);throws(()=>structuredClone(()=>{}, {transfer:[keep]}),'DataCloneError');check(keep.byteLength===4,'failed clone preserves buffer');throws(()=>structuredClone(1,{transfer:[keep,keep]}),'DataCloneError');throws(()=>structuredClone(1,{transfer:[new Uint8Array(3)]}),'DataCloneError');
  const closedChannel=new MessageChannel();closedChannel.port1.close();const closedCopy=structuredClone(closedChannel.port1,{transfer:[closedChannel.port1]});check(closedCopy instanceof MessagePort,'closed port transfer');const unconsumed=new ArrayBuffer(4);closedCopy.postMessage(1,[unconsumed]);check(unconsumed.byteLength===0,'transferred closed endpoint serializes');const consumed=new ArrayBuffer(4);closedChannel.port2.postMessage(1,[consumed]);check(consumed.byteLength===0,'open endpoint with closed peer serializes');closedChannel.port2.close();closedCopy.close();
  const reentrant=new MessageChannel(),reentrantBuffer=new ArrayBuffer(4);const evil={get value(){structuredClone(reentrantBuffer,{transfer:[reentrantBuffer]});return 1;}};throws(()=>structuredClone(evil,{transfer:[reentrant.port1,reentrantBuffer]}),'DataCloneError');const reentrantReply=next(reentrant.port1);reentrant.port2.postMessage('preserved after reentry');check((await reentrantReply).data==='preserved after reentry','failed reentrant transfer preserves ports');reentrant.port1.close();reentrant.port2.close();
  const cloneChannel=new MessageChannel();const clonePort=structuredClone(cloneChannel.port1,{transfer:[cloneChannel.port1]});const cloneReply=next(clonePort);cloneChannel.port2.postMessage('cloned port');check((await cloneReply).data==='cloned port','synchronous port transfer');clonePort.close();cloneChannel.port2.close();
  const channel=new MessageChannel();let observed=0;channel.port2.addEventListener('message',()=>observed++);channel.port1.postMessage({value:17});check(observed===0,'asynchronous delivery');await delay();check(observed===0,'addEventListener requires start');
  const first=next(channel.port2);const event=await first;check(event.data.value===17&&event.source===null&&event.origin===''&&event.ports.length===0&&event.isTrusted&&event.target===channel.port2,'channel event semantics');
  const onmessageDone=new Promise(resolve=>{channel.port1.onmessage=e=>{check(e.data===undefined,'explicit undefined data');resolve();};});channel.port2.postMessage(undefined);await onmessageDone;
  const iterable=new MessageChannel(),iterableBuffer=new ArrayBuffer(2);iterable.port1.postMessage(1,new Set([iterableBuffer]));check(iterableBuffer.byteLength===0,'iterable transfer sequence');iterable.port1.close();iterable.port2.close();
  const nested=new MessageChannel();const incoming=next(channel.port2);channel.port1.postMessage({port:nested.port2},[nested.port2]);const received=await incoming;check(received.ports.length===1&&received.data.port===received.ports[0],'port identity in graph and ports');
  // Old wrapper cannot close or receive from the moved endpoint.
  nested.port2.close();const reply=next(received.ports[0]);nested.port1.postMessage('through transferred port');check((await reply).data==='through transferred port','transferred ownership');
  throws(()=>channel.port1.postMessage(nested.port1),'DataCloneError');throws(()=>channel.port1.postMessage(1,[channel.port1]),'DataCloneError');
  const queued=new MessageChannel();queued.port1.postMessage('queued before move');const delivery=next(channel.port2);channel.port1.postMessage(queued.port2,[queued.port2]);const movedEvent=await delivery;check((await next(movedEvent.data)).data==='queued before move','queued messages survive transfer');
  const doneWindow=new Promise(resolve=>{const listener=e=>{if(e.data?.marker!=='owned-window-test')return;removeEventListener('message',listener);check(e.source===window&&e.origin===location.origin&&e.isTrusted,'self window source/origin');check(e.data.snapshot===1,'serialization at send time');resolve();};addEventListener('message',listener);});
  const payload={marker:'owned-window-test',snapshot:1};postMessage(payload,{targetOrigin:location.origin});payload.snapshot=2;await doneWindow;
  let wrong=false;const wrongListener=e=>{if(e.data==='wrong-origin')wrong=true;};addEventListener('message',wrongListener);postMessage('wrong-origin','https://different.invalid');await delay();removeEventListener('message',wrongListener);check(!wrong,'targetOrigin drops mismatch');throws(()=>postMessage(1,'not a url'),'SyntaxError');
  for(const port of [channel.port1,channel.port2,nested.port1,received.ports[0],queued.port1,movedEvent.data])port.close();
  globalThis.messagingDone=true;
 }catch(error){globalThis.messagingError=String(error)+' '+error.stack;}
})();

(async()=>{
 const check=(value,message)=>{if(!value)throw Error(message);};
 const throws=(fn,name)=>{let error;try{fn();}catch(e){error=e;}check(error&&(!name||error.name===name),'expected '+name);};
 const endpoint=path=>document.URL.replace(/^https:/,'wss:').replace(/\/[^/]*$/,'')+path;
 const events=[];
 function connect(url,protocols){return new Promise((resolve,reject)=>{const socket=new WebSocket(url,protocols);socket.onopen=()=>resolve(socket);socket.onerror=()=>reject(Error('connect failed '+url));});}
 function message(socket){return new Promise((resolve,reject)=>{socket.onmessage=event=>resolve(event);socket.onerror=()=>reject(Error('socket message error'));});}
 function close(socket,code,reason){return new Promise(resolve=>{socket.onclose=resolve;socket.close(code,reason);});}
 function failure(url,expectError=true){return new Promise((resolve,reject)=>{const socket=new WebSocket(url);let failed=false;socket.onopen=()=>{};socket.onerror=()=>{failed=true;check(socket.readyState===3,'error closed state');};socket.onclose=e=>{try{check(failed===expectError&&!e.wasClean&&e.code===1006,'failure lifecycle '+url+' '+failed+'/'+e.wasClean+'/'+e.code);resolve();}catch(e){reject(e);}};});}
 if(!globalThis.browserReference)throws(()=>new WebSocket('ws://localhost/'),'SyntaxError');throws(()=>new WebSocket('/ws-echo#fragment'),'SyntaxError');throws(()=>new WebSocket('/ws-echo',['same','same']),'SyntaxError');throws(()=>new WebSocket('/ws-echo','bad protocol'),'SyntaxError');
 check(WebSocket.prototype instanceof EventTarget&&WebSocket.OPEN===1&&WebSocket.prototype.CLOSED===3,'WS interface');
 globalThis.websocketStage=0;
 const pending=new WebSocket(endpoint('/ws-slow'));throws(()=>pending.send('x'),'InvalidStateError');let timer=false;setTimeout(()=>timer=true,1);
 await new Promise((resolve,reject)=>{pending.onopen=()=>{try{check(timer,'connect blocks timers');resolve();}catch(e){reject(e);}};pending.onerror=reject;});await close(pending,1000,'');
 globalThis.websocketStage=1;
 const socket=await connect(endpoint('/ws-meta'),['test.v1','test.v2']);check(socket.protocol==='test.v1'&&socket.binaryType==='blob'&&socket.extensions==='','negotiation');socket.binaryType='bogus';check(socket.binaryType==='blob','binaryType invalid ignored');
 const meta=JSON.parse((await message(socket)).data);check(meta.origin===location.origin&&meta.cookie.includes('hidden=1')&&meta.protocol==='test.v1','Origin/cookie/protocol');check(!document.cookie.includes('ws_secret')&&!document.cookie.includes('hidden'),'HttpOnly');document.cookie='ws_secret=hacked; Path=/';
 let echoed=message(socket);socket.send('Привет сокет');check(socket.bufferedAmount>0,'queued amount');check((await echoed).data==='Привет сокет','UTF8 echo');
 let typed=new Uint8Array([9,1,2,3,8]);echoed=message(socket);socket.send(typed.subarray(1,4));typed.fill(0);const blob=(await echoed).data;check(blob instanceof Blob&&blob.size===3&&blob.type==='','default Blob');check([...new Uint8Array(await blob.arrayBuffer())].join()==='1,2,3','view copied');
 socket.binaryType='arraybuffer';echoed=message(socket);socket.send(new Blob(['Ж',new Uint8Array([0])]));const binary=(await echoed).data;check(binary instanceof ArrayBuffer&&new Uint8Array(binary).length===3,'Blob send/arraybuffer receive');
 echoed=message(socket);const bulk=new Uint8Array(524288);bulk[524287]=123;socket.send(bulk);socket.send('tail');const large=(await echoed).data;const tail=message(socket);check(new Uint8Array(large)[524287]===123&&(await tail).data==='tail','maximum message followed by another frame');
 throws(()=>socket.close(1006),'InvalidAccessError');throws(()=>socket.close(1000,'я'.repeat(62)),'SyntaxError');
 const closing=close(socket,4000,'пока');check(socket.readyState===2,'local closing');socket.send('discarded');const ended=await closing;check(ended instanceof CloseEvent&&ended.code===4000&&ended.reason==='пока'&&ended.wasClean&&socket.readyState===3,'clean close');check(socket.bufferedAmount===9,'post-close buffered');
 globalThis.websocketStage=2;
 const fragmented=await connect(endpoint('/ws-fragments'));const part=await message(fragmented);check(part.data==='Живой сокет'&&part.origin===location.origin.replace(/^https:/,'wss:')&&part instanceof MessageEvent,'fragments/UTF8/origin');await close(fragmented,1000);
 globalThis.websocketStage=3;
 const peer=await connect(endpoint('/ws-peer-close'));const peerEnd=await new Promise(resolve=>peer.onclose=resolve);check(peerEnd.code===4001&&peerEnd.reason==='пока'&&peerEnd.wasClean,'peer close');
 await new Promise((resolve,reject)=>{const fast=new WebSocket(endpoint('/ws-fast-close'));let received=false;fast.onopen=()=>fast.send('opening send');fast.onmessage=e=>{received=e.data==='last message';};fast.onclose=e=>{try{check(received&&e.code===1000,'queued message survived transport close');resolve();}catch(e){reject(e);}};});
 globalThis.websocketStage=4;
 for(const path of ['/ws-bad-accept','/ws-bad-protocol','/ws-invalid-utf8','/ws-masked'])await failure(endpoint(path));
 globalThis.websocketStage=5;
 if(!globalThis.browserReference)await failure(endpoint('/ws-oversize'));
 globalThis.websocketStage=6;
 await failure(endpoint('/ws-eof'),false);
 if(!globalThis.browserReference)await failure(config.expired.replace(/^https:/,'wss:')+'/ws-echo');
 globalThis.websocketStage=7;
 const anonymous=await connect(config.cross.replace(/^https:/,'wss:')+'/ws-meta');const anonymousMeta=JSON.parse((await message(anonymous)).data);check((globalThis.browserReference?anonymousMeta.cookie.includes('hidden=1'):!anonymousMeta.cookie)&&anonymousMeta.origin===location.origin,'cross-origin cookie policy');await close(anonymous,1000);
 globalThis.websocketStage=8;
 const cancelled=new WebSocket(endpoint('/ws-slow'));let abortError=false;const aborted=new Promise(resolve=>{cancelled.onerror=()=>abortError=true;cancelled.onclose=resolve;});cancelled.close();check(cancelled.readyState===2,'cancel connecting');check((await aborted).code===1006&&abortError,'cancel lifecycle');
 globalThis.websocketStage=9;
 const response=new Response(new Blob(['Русский'],{type:'TEXT/PLAIN'}));const responseBlob=await response.blob();check(responseBlob.type==='text/plain'&&await responseBlob.text()==='Русский','body Blob');
 const original=new Uint8Array([1,2,3]);const immutable=new Blob([original]);original.fill(0);check([...new Uint8Array(await immutable.slice(-2).arrayBuffer())].join()==='2,3','immutable slice');
 // Eight live sockets and page lifetime bounds must not start extra transfers.
 globalThis.websocketStage=10;
 if(!globalThis.browserReference){const live=Array.from({length:8},()=>new WebSocket(endpoint('/ws-slow')));
 throws(()=>new WebSocket(endpoint('/ws-slow')));
 await Promise.all(live.map(s=>new Promise(resolve=>{s.onclose=resolve;s.close();})));}
 globalThis.websocketStage=11;
 const policy=document.createElement('meta');policy.setAttribute('http-equiv','Content-Security-Policy');policy.setAttribute('content',"connect-src 'none'");document.head.appendChild(policy);
 if(globalThis.browserReference){await new Promise((resolve,reject)=>{const denied=new WebSocket(endpoint('/ws-echo'));denied.onerror=()=>{try{check(denied.readyState===3,'CSP closed state');resolve();}catch(e){reject(e);}};});}else await failure(endpoint('/ws-echo'));policy.remove();
 globalThis.websocketDone=true;document.documentElement.setAttribute('data-network-done','1');
})().catch(error=>{globalThis.websocketError=String(error);document.documentElement.setAttribute('data-network-error','1');});

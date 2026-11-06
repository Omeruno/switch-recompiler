(async()=>{
  const check=(ok,label)=>{if(!ok)throw Error(label);};
  const headers=new Headers([['X-Test',' one '],['x-test','two']]);check(headers.get('X-TEST')==='one, two','headers normalize');headers.set('x-other','value');check([...headers.keys()].join(',')==='x-other,x-test','headers order');let invalid=0;try{headers.set('bad\nname','value');}catch(e){invalid++;}try{headers.set('ok','bad\r\nvalue');}catch(e){invalid++;}check(invalid===2,'header validation');
  const request=new Request('https://example.test/data',{method:'post',headers:{'Cookie':'spoof','Origin':'spoof'},body:new URLSearchParams({name:'Иван + &'})});
  check(request.method==='POST'&&!request.headers.has('cookie')&&!request.headers.has('origin')&&request.headers.get('content-type').startsWith('application/x-www-form-urlencoded'),'request metadata');
  const cloned=request.clone();check(!request.bodyUsed&&(await cloned.text()).includes('%D0%98%D0%B2%D0%B0%D0%BD'),'request clone');
  const response=new Response('Привет',{status:201,headers:{'x-test':'ok'}}),copy=response.clone();check(response.ok&&response.status===201&&await response.text()==='Привет'&&response.bodyUsed,'response text');let reused=false;try{await response.text();}catch(e){reused=e instanceof TypeError;}check(reused&&await copy.text()==='Привет','response consumption');
  const immediate=new Response('used');const pending=immediate.text();check(immediate.bodyUsed,'immediate body disturbance');await pending;
  check((await Response.json({ok:42}).json()).ok===42,'response JSON');
  const bytes=new Uint8Array([0,1,127,255]);check([...new Uint8Array(await new Response(bytes).arrayBuffer())].join(',')==='0,1,127,255','binary response');
  const encoder=new TextEncoder(),decoder=new TextDecoder();check(decoder.decode(encoder.encode('Ёж 🦔'))==='Ёж 🦔','UTF-8 roundtrip');check(decoder.decode(new Uint8Array([239,187,191,97]))==='a','UTF-8 BOM');check([...encoder.encode('\ud800')].join(',')==='239,191,189','unpaired surrogate');check(decoder.decode(new Uint8Array([192,175]))==='\ufffd\ufffd','invalid UTF-8');
  const controller=new AbortController();let events=0;controller.signal.onabort=()=>events++;const reason={cancelled:true};controller.abort(reason);controller.abort();let thrown=false;try{controller.signal.throwIfAborted();}catch(e){thrown=e===reason;}check(controller.signal.aborted&&controller.signal.reason===reason&&events===1&&thrown,'abort semantics');
  check(AbortSignal.abort().reason.name==='AbortError','default abort reason');check(new DOMException('message','AbortError').code===20,'DOMException code');
  const progress=new ProgressEvent('progress',{loaded:5,total:10,lengthComputable:true});check(progress.loaded===5&&progress.total===10&&progress.lengthComputable,'progress event');
  globalThis.networkTypesResult='headers|request-body|response-clone|one-shot-body|binary|utf8|abort|progress';
})().catch(e=>{globalThis.networkTypesError=String(e);});

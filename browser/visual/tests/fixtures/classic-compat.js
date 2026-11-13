(async()=>{
 const check=(ok,label)=>{if(!ok)throw Error(label);};
 const load=(url,integrity='',cors=true)=>new Promise(resolve=>{const s=document.createElement('script');s.src=url;if(cors)s.crossOrigin='anonymous';if(integrity)s.integrity=integrity;s.onload=()=>{s.remove();resolve('load');};s.onerror=()=>{s.remove();resolve('error');};document.head.appendChild(s);});
 const c=config.cross+'/api-classic',hash=config.integrity,wrong=value=>value.replace(/-(.)/,(_,c)=>'-'+(c==='A'?'B':'A'));
 check(await load(c,hash.sha384)==='load'&&classicLoaded===1,'SHA384 CORS script');
 check(await load(c,wrong(hash.sha384))==='error'&&classicLoaded===1,'bad SRI');
 check(await load(c,hash.sha256+' '+wrong(hash.sha512))==='error','strongest hash wins');
 check(await load(c,wrong(hash.sha256)+' '+hash.sha384)==='load'&&classicLoaded===2,'strong correct hash');
 check(await load(c+'?cors=deny',hash.sha384)==='error','CORS denies valid digest');
 check(await load(c,hash.sha384,false)==='error','cross-origin SRI requires CORS');
 check(await load('/api-classic',hash.sha256,false)==='load'&&classicLoaded===3,'same-origin SRI');
 check(await load('/api-redirect?target='+encodeURIComponent(c),hash.sha512)==='load'&&classicLoaded===4,'CORS redirect before evaluation');
 check(await load('/api-redirect?target='+encodeURIComponent(c),hash.sha256,false)==='error','SRI eligibility checked after redirect');
 check(await load(c+'?size=900000')==='load'&&classicLoaded===5,'large provider-sized bundle');
 if(!globalThis.browserReference)check(await load('/api-classic?size=2097153')==='error','per-script byte quota');
 check(await load('/api-classic?mime=bad')==='error','nosniff MIME');
 if(!globalThis.browserReference){let successes=0,failures=0;for(let i=0;i<4;i++){const result=await load('/api-classic?size=2097000&index='+i);if(result==='load')successes++;else failures++;}check(successes===3&&failures===1,'aggregate script budget');}
 globalThis.classicCompatibilityDone=true;
})().catch(e=>{globalThis.classicCompatibilityError=e.name+': '+e.message;});

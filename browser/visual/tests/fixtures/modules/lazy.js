await new Promise(resolve=>{if(document.readyState==='complete')setTimeout(resolve,0);else addEventListener('load',()=>setTimeout(resolve,0),{once:true});});
globalThis.moduleTrace.push('lazy');
globalThis.lazyRuns=(globalThis.lazyRuns||0)+1;
export const value=42;

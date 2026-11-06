moduleLifecycle.push('start');
await new Promise(resolve=>addEventListener('load',()=>setTimeout(resolve,0),{once:true}));
moduleLifecycle.push('end');
throw new Error('TLA fixture error');

// Wrappers delegate to the actual litehtml tree. There is no second document.
(() => {
  const bridge=globalThis.__native;
  delete globalThis.__native;
  const nodes=new Map(), listeners=new WeakMap();
  let listenerCount=0, readyState='loading';
  class Event {
    constructor(type,options={}) {this.type=String(type);this.bubbles=!!options.bubbles;this.cancelable=!!options.cancelable;this.defaultPrevented=false;this.target=null;this.currentTarget=null;this.eventPhase=0;this._stop=false;this._immediate=false;}
    preventDefault(){if(this.cancelable)this.defaultPrevented=true;}
    stopPropagation(){this._stop=true;}
    stopImmediatePropagation(){this._stop=this._immediate=true;}
  }
  class CustomEvent extends Event {constructor(type,options={}){super(type,options);this.detail=options.detail??null;}}
  const eventMethods={
    addEventListener(type,callback,options={}) {
      if(!callback)return;
      type=String(type);const capture=typeof options==='boolean'?options:!!options.capture;
      let events=listeners.get(this);if(!events){events=new Map();listeners.set(this,events);}
      let list=events.get(type);if(!list){list=[];events.set(type,list);}
      if(list.some(e=>e.fn===callback&&e.capture===capture))return;
      if(listenerCount>=256)throw new RangeError('Listener limit');
      list.push({fn:callback,capture,once:!!options.once});++listenerCount;
    },
    removeEventListener(type,callback,options={}) {
      const capture=typeof options==='boolean'?options:!!options.capture;
      const list=listeners.get(this)?.get(String(type));if(!list)return;
      const at=list.findIndex(e=>e.fn===callback&&e.capture===capture);if(at>=0){list.splice(at,1);--listenerCount;}
    },
    dispatchEvent(event) {
      if(!(event instanceof Event))throw new TypeError('Expected Event');
      event._stop=event._immediate=false;event.target=this;
      const path=[this];let p=this;
      for(let i=0;i<256;i++){p=p===document?(event.type==='load'?null:globalThis):p===globalThis?null:p.parentNode;if(!p)break;path.push(p);}
      const invoke=(node,capture,phase)=>{
        event.currentTarget=node;event.eventPhase=phase;event._immediate=false;
        for(const entry of [...(listeners.get(node)?.get(event.type)||[])]){
          if(entry.capture!==capture||event._immediate)continue;
          if(!(listeners.get(node)?.get(event.type)||[]).includes(entry))continue;
          if(entry.once)node.removeEventListener(event.type,entry.fn,{capture});
          try {typeof entry.fn==='function'?entry.fn.call(node,event):entry.fn.handleEvent?.(event);}catch(e){bridge('report',0,String(e));}
        }
        if(!capture&&!event._immediate&&typeof node['on'+event.type]==='function'){
          try {if(node['on'+event.type].call(node,event)===false)event.preventDefault();}catch(e){bridge('report',0,String(e));}
        }
      };
      for(let i=path.length-1;i>0&&!event._stop;i--)invoke(path[i],true,1);
      if(!event._stop){invoke(this,true,2);if(!event._immediate)invoke(this,false,2);}
      if(event.bubbles)for(let i=1;i<path.length&&!event._stop;i++)invoke(path[i],false,3);
      event.currentTarget=null;event.eventPhase=0;return !event.defaultPrevented;
    }
  };
  function selectorParts(s){
    s=String(s);if(s.length>512)throw new RangeError('Selector limit');
    const result=[];let depth=0,quote='',start=0;
    for(let i=0;i<s.length;i++){const c=s[i];if(c==='\\'){i++;continue;}if(quote){if(c===quote)quote='';continue;}if(c==='"'||c==="'"){quote=c;continue;}if(c==='['||c==='(')depth++;else if(c===']'||c===')')depth--;else if(c===','&&!depth){result.push(s.slice(start,i));start=i+1;}}
    result.push(s.slice(start));if(depth||quote||result.some(p=>!p.trim()))throw new SyntaxError('Invalid selector');return result;
  }
  const query=(id,selector)=>{
    const results=new Set();for(const part of selectorParts(selector))for(const key of bridge('query',id,part))results.add(key);
    return bridge('order',id,[...results]).map(wrap);
  };
  class Node {
    constructor(key){Object.defineProperty(this,'_key',{value:key});}
    get nodeType(){return this._key===0?9:bridge('kind',this._key);}
    get nodeName(){return this.nodeType===9?'#document':this.nodeType===3?'#text':this.tagName;}
    get tagName(){return bridge('tag',this._key).toUpperCase();}
    get ownerDocument(){return this._key===0?null:document;}
    get parentNode(){const key=bridge('parent',this._key);return key>=0?wrap(key):null;}
    get parentElement(){const p=this.parentNode;return p?.nodeType===1?p:null;}
    get childNodes(){return bridge('children',this._key).map(wrap);}
    get children(){return this.childNodes.filter(n=>n.nodeType===1);}
    get firstChild(){return this.childNodes[0]||null;}
    get firstElementChild(){return this.children[0]||null;}
    get lastChild(){return this.childNodes.at(-1)||null;}
    get nextSibling(){const a=this.parentNode?.childNodes||[];return a[a.indexOf(this)+1]||null;}
    get nextElementSibling(){let n=this.nextSibling;while(n&&n.nodeType!==1)n=n.nextSibling;return n;}
    get isConnected(){for(let n=this;n;n=n.parentNode)if(n===document)return true;return false;}
    get textContent(){return bridge('text',this._key);}
    set textContent(v){bridge('text',this._key,String(v??''));}
    get innerHTML(){return bridge('html',this._key);}
    set innerHTML(v){bridge('html',this._key,String(v));}
    get innerText(){return this.textContent;}set innerText(v){this.textContent=v;}
    appendChild(node){if(!(node instanceof Node))throw new TypeError('Expected Node');bridge('append',this._key,node._key);return node;}
    append(...items){for(const v of items)this.appendChild(v instanceof Node?v:document.createTextNode(String(v)));}
    insertBefore(node,before){if(!(node instanceof Node))throw new TypeError('Expected Node');bridge('insert',this._key,node._key,before?before._key:-1);return node;}
    prepend(...items){for(let i=items.length-1;i>=0;i--){const node=items[i] instanceof Node?items[i]:document.createTextNode(String(items[i]));this.insertBefore(node,this.firstChild);}}
    removeChild(node){bridge('remove',this._key,node._key);return node;}
    remove(){if(this.parentNode)this.parentNode.removeChild(this);}
    replaceChildren(...items){for(const node of this.childNodes)node.remove();this.append(...items);}
    contains(node){for(let n=node;n;n=n.parentNode)if(n===this)return true;return false;}
    getAttribute(name){return bridge('attr',this._key,String(name).toLowerCase());}
    setAttribute(name,v){bridge('attr',this._key,String(name).toLowerCase(),String(v));}
    hasAttribute(name){return this.getAttribute(name)!==null;}
    removeAttribute(name){bridge('attr',this._key,String(name).toLowerCase(),null);}
    get id(){return this.getAttribute('id')||'';}set id(v){this.setAttribute('id',v);}
    get className(){return this.getAttribute('class')||'';}set className(v){this.setAttribute('class',v);}
    get classList(){const el=this;return {add(...a){el.className=[...new Set([...el.className.split(/\s+/).filter(Boolean),...a])].join(' ');},remove(...a){el.className=el.className.split(/\s+/).filter(x=>!a.includes(x)).join(' ');},contains(x){return el.className.split(/\s+/).includes(x);},toggle(x,force){const yes=force===undefined?!this.contains(x):!!force;yes?this.add(x):this.remove(x);return yes;}};}
    get dataset(){const el=this;return new Proxy({}, {get(_,k){return el.getAttribute('data-'+String(k).replace(/[A-Z]/g,c=>'-'+c.toLowerCase()))??undefined;},set(_,k,v){el.setAttribute('data-'+String(k).replace(/[A-Z]/g,c=>'-'+c.toLowerCase()),v);return true;}});}
    get style(){const el=this;const pairs=()=>new Map((el.getAttribute('style')||'').split(';').map(x=>{const i=x.indexOf(':');return i<0?['','']:[x.slice(0,i).trim(),x.slice(i+1).trim()];}).filter(x=>x[0]));const css=k=>String(k).replace(/[A-Z]/g,c=>'-'+c.toLowerCase());return new Proxy({setProperty(k,v,p=''){const a=pairs();a.set(String(k),String(v)+(p?' !important':''));el.setAttribute('style',[...a].map(x=>x.join(':')).join(';'));},removeProperty(k){const a=pairs(),v=a.get(k)||'';a.delete(k);el.setAttribute('style',[...a].map(x=>x.join(':')).join(';'));return v;},getPropertyValue(k){return pairs().get(k)||'';}},{get(o,k){return k==='cssText'?el.getAttribute('style')||'':k in o?o[k]:o.getPropertyValue(css(k));},set(o,k,v){if(k==='cssText')el.setAttribute('style',v);else o.setProperty(css(k),v);return true;}});}
    get type(){return bridge('type',this._key);}set type(v){this.setAttribute('type',v);}
    get checked(){return bridge('checked',this._key);}set checked(v){bridge('checked',this._key,!!v);}
    get defaultChecked(){return this.hasAttribute('checked');}set defaultChecked(v){v?this.setAttribute('checked',''):this.removeAttribute('checked');}
    get selected(){return bridge('selected',this._key);}set selected(v){bridge('selected',this._key,!!v);}
    get defaultSelected(){return this.hasAttribute('selected');}set defaultSelected(v){v?this.setAttribute('selected',''):this.removeAttribute('selected');}
    get selectedIndex(){return bridge('selectedIndex',this._key);}set selectedIndex(v){bridge('selectedIndex',this._key,v);}
    get options(){return bridge('options',this._key).map(wrap);}get selectedOptions(){return this.options.filter(o=>o.selected);}
    get form(){const key=bridge('formOwner',this._key);return key===null?null:wrap(key);}
    get defaultValue(){return this.tagName==='TEXTAREA'?this.textContent:this.getAttribute('value')||'';}set defaultValue(v){if(this.tagName==='TEXTAREA')this.textContent=v;else this.setAttribute('value',v);}
    submit(){if(this.tagName!=='FORM')throw new TypeError('Expected form');bridge('submitForm',this._key);}
    requestSubmit(submitter=null){if(this.tagName!=='FORM')throw new TypeError('Expected form');const target=submitter||this;if(submitter&&submitter.form!==this)throw new TypeError('Wrong submitter');if(!bridge('validateForm',target._key,true))return;const event=new Event('submit',{bubbles:true,cancelable:true});event.submitter=submitter;if(this.dispatchEvent(event))bridge('submitForm',target._key);}
    reset(){if(this.tagName!=='FORM')throw new TypeError('Expected form');if(this.dispatchEvent(new Event('reset',{bubbles:true,cancelable:true})))bridge('resetForm',this._key);}
    checkValidity(){return bridge('validateForm',this._key,false);}reportValidity(){return bridge('validateForm',this._key,true);}
    get value(){return bridge('value',this._key);}set value(v){bridge('value',this._key,String(v));}
    get disabled(){return this.hasAttribute('disabled');}set disabled(v){v?this.setAttribute('disabled',''):this.removeAttribute('disabled');}
    get hidden(){return this.hasAttribute('hidden');}set hidden(v){v?this.setAttribute('hidden',''):this.removeAttribute('hidden');}
    get href(){return bridge('resolve',0,this.getAttribute('href')||'');}set href(v){this.setAttribute('href',v);}
    get name(){return this.getAttribute('name')||'';}set name(v){this.setAttribute('name',v);}
    get dateTime(){return this.getAttribute('datetime')||'';}set dateTime(v){this.setAttribute('datetime',v);}
    querySelectorAll(s){return query(this._key,s);}querySelector(s){return this.querySelectorAll(s)[0]||null;}
    matches(s){return selectorParts(s).some(p=>bridge('matches',this._key,p));}
    closest(s){for(let n=this;n&&n.nodeType===1;n=n.parentNode)if(n.matches(s))return n;return null;}
    getBoundingClientRect(){return bridge('rect',this._key);}
    get offsetWidth(){return this.getBoundingClientRect().width;}get offsetHeight(){return this.getBoundingClientRect().height;}
    focus(){setFocus(this);}
    blur(){if(currentFocus()===this)setFocus(null);}
    click(){this.dispatchEvent(new Event('click',{bubbles:true,cancelable:true}));}
  }
  for(const name of ['lang','dir','title','rel','target','placeholder'])Object.defineProperty(Node.prototype,name,{get(){return this.getAttribute(name)||'';},set(value){this.setAttribute(name,value);}});
  Object.defineProperty(Node.prototype,'src',{get(){const value=this.getAttribute('src');return value?bridge('resolve',0,value):'';},set(v){this.setAttribute('src',v);}});
  Object.assign(Node.prototype,eventMethods);
  function currentFocus(){const key=bridge('activeElement',0);return key===null?null:wrap(key);}
  function setFocus(target){
    const old=currentFocus();bridge('focus',target?target._key:0);const next=currentFocus();
    if(old===next)return;
    if(old){const blur=new Event('blur');blur.relatedTarget=next;old.dispatchEvent(blur);const out=new Event('focusout',{bubbles:true});out.relatedTarget=next;old.dispatchEvent(out);}
    // A blur handler may already have moved focus elsewhere.
    if(next&&currentFocus()===next){const focus=new Event('focus');focus.relatedTarget=old;next.dispatchEvent(focus);if(currentFocus()===next){const into=new Event('focusin',{bubbles:true});into.relatedTarget=old;next.dispatchEvent(into);}}
  }
  function wrap(key){if(!nodes.has(key))nodes.set(key,new Node(key));return nodes.get(key);}
  const document=wrap(0);
  document.createElement=tag=>wrap(bridge('create',0,String(tag)));
  document.createTextNode=text=>wrap(bridge('createText',0,String(text)));
  document.getElementById=id=>{const key=bridge('id',0,String(id));return key?wrap(key):null;};
  document.getElementsByClassName=s=>query(0,'.'+String(s).trim().split(/\s+/).join('.'));
  document.getElementsByTagName=s=>query(0,String(s));
  Object.defineProperties(document,{
    documentElement:{get:()=>wrap(bridge('root',0))},body:{get:()=>document.querySelector('body')},head:{get:()=>document.querySelector('head')},
    title:{get:()=>document.querySelector('title')?.textContent||'',set:v=>{const t=document.querySelector('title');if(t)t.textContent=v;}},
    currentScript:{get:()=>{const key=bridge('currentScript',0);return key===null?null:wrap(key);}},
    readyState:{get:()=>readyState},activeElement:{get:()=>currentFocus()||document.body},
    cookie:{get:()=>bridge('cookie',0),set:v=>bridge('cookie',0,String(v))},
    referrer:{get:()=>''},
    URL:{get:()=>bridge('url',0)}
  });
  Object.assign(globalThis,eventMethods,{window:globalThis,self:globalThis,document,Node,Element:Node,HTMLElement:Node,Event,CustomEvent});
  const location={get search(){const q=bridge('urlPart',0,bridge('url',0),2);return q?'?'+q:'';},get hostname(){return bridge('urlPart',0,bridge('url',0),0);},get pathname(){return bridge('urlPart',0,bridge('url',0),1)||'/';},get origin(){return bridge('origin',0,bridge('url',0));},get href(){return bridge('url',0);},set href(v){bridge('navigate',0,String(v));},assign(v){this.href=v;},replace(v){this.href=v;},reload(){bridge('reload',0);}};
  Object.defineProperty(globalThis,'location',{get:()=>location,set:v=>location.href=v});
  Object.assign(globalThis,{navigator:{language:'ru-RU',languages:['ru-RU','ru','en'],userAgent:'SwitchVisualBrowser/0.6.0'},console:{log(){},warn(){},error(){}},performance:{now:()=>bridge('now',0)}});
  class URLSearchParams {
    constructor(input='',changed=null){
      this._changed=typeof changed==='function'?changed:null;
      if(input instanceof URLSearchParams)this._pairs=[...input.entries()];
      else if(typeof input==='string'){const decode=s=>decodeURIComponent(s.replace(/\+/g,' '));this._pairs=input.replace(/^\?/,'').split('&').filter(Boolean).map(s=>{const i=s.indexOf('=');return [decode(i<0?s:s.slice(0,i)),decode(i<0?'':s.slice(i+1))];});}
      else if(input&&typeof input[Symbol.iterator]==='function')this._pairs=[...input].map(x=>[String(x[0]),String(x[1])]);
      else this._pairs=Object.entries(input||{}).map(([k,v])=>[k,String(v)]);
      if(this._pairs.length>128)throw new RangeError('URL parameter limit');
    }
    _update(){if(this._pairs.length>128)throw new RangeError('URL parameter limit');this._changed?.(this.toString());}
    get(k){return this._pairs.find(p=>p[0]===String(k))?.[1]??null;}
    getAll(k){return this._pairs.filter(p=>p[0]===String(k)).map(p=>p[1]);}
    has(k){return this._pairs.some(p=>p[0]===String(k));}
    append(k,v){this._pairs.push([String(k),String(v)]);this._update();}
    set(k,v){k=String(k);const at=this._pairs.findIndex(p=>p[0]===k);this._pairs=this._pairs.filter(p=>p[0]!==k);this._pairs.splice(at<0?this._pairs.length:at,0,[k,String(v)]);this._update();}
    delete(k){this._pairs=this._pairs.filter(p=>p[0]!==String(k));this._update();}
    *entries(){yield* this._pairs.map(p=>[...p]);}*keys(){for(const p of this._pairs)yield p[0];}*values(){for(const p of this._pairs)yield p[1];}
    [Symbol.iterator](){return this.entries();}forEach(fn,self){for(const [k,v] of this._pairs)fn.call(self,v,k,this);}
    toString(){const encode=s=>encodeURIComponent(s).replace(/%20/g,'+').replace(/[!'()~]/g,c=>'%'+c.charCodeAt(0).toString(16).toUpperCase());return this._pairs.map(p=>p.map(encode).join('=')).join('&');}
  }
  globalThis.URLSearchParams=URLSearchParams;
  class URL {
    constructor(v,base){this.href=bridge('resolve',0,String(v),base===undefined?'':String(base));if(!this.href)throw new TypeError('Invalid URL');}
    get origin(){return bridge('origin',0,this.href);}
    get searchParams(){return new URLSearchParams(this.search.slice(1),value=>this.href=bridge('urlQuery',0,this.href,value));}
    toString(){return this.href;}
    get hostname(){return bridge('urlPart',0,this.href,0);}get pathname(){return bridge('urlPart',0,this.href,1)||'/';}get search(){const q=bridge('urlPart',0,this.href,2);return q?'?'+q:'';}
  }
  globalThis.URL=URL;
  for(const [name,session] of [['localStorage',false],['sessionStorage',true]])globalThis[name]={get length(){return bridge('storageLength',0,session);},key(i){return bridge('storageKey',0,session,i);},getItem(k){return bridge('storageGet',0,session,String(k));},setItem(k,v){bridge('storageSet',0,session,String(k),String(v));},removeItem(k){bridge('storageRemove',0,session,String(k));},clear(){bridge('storageClear',0,session);}};
  const timers=new Map(), frames=new Map();let nextTimer=0,nextFrame=0,frameIDs=[],frameTime=0;
  function timer(fn,delay,repeat,args){if(typeof fn!=='function')throw new TypeError('Timer requires function');if(timers.size>=64)throw new RangeError('Timer limit');let ms=Number(delay);ms=Number.isFinite(ms)?Math.max(0,Math.min(2147483647,ms)):0;const id=++nextTimer;timers.set(id,{fn,args,due:bridge('now',0)+ms,repeat:repeat?Math.max(4,ms):0});return id;}
  globalThis.setTimeout=(fn,delay,...args)=>timer(fn,delay,false,args);globalThis.setInterval=(fn,delay,...args)=>timer(fn,delay,true,args);
  globalThis.clearTimeout=globalThis.clearInterval=id=>timers.delete(Number(id));
  globalThis.queueMicrotask=fn=>{if(typeof fn!=='function')throw new TypeError('Microtask requires function');bridge('microtask',0,fn);};
  globalThis.requestAnimationFrame=fn=>{if(typeof fn!=='function')throw new TypeError('Frame requires function');if(frames.size>=64)throw new RangeError('Frame limit');const id=++nextFrame;frames.set(id,fn);return id;};
  globalThis.cancelAnimationFrame=id=>frames.delete(Number(id));
  Object.defineProperties(globalThis,{
    innerWidth:{get:()=>bridge('viewport',0).width},innerHeight:{get:()=>bridge('viewport',0).height},devicePixelRatio:{get:()=>bridge('viewport',0).ratio},scrollX:{get:()=>bridge('viewport',0).scrollX},scrollY:{get:()=>bridge('viewport',0).scrollY}
  });
  const mediaLists=new Set();let lastViewport=bridge('viewport',0);
  globalThis.matchMedia=query=>{query=String(query);if(mediaLists.size>=32)throw new RangeError('Media query quota');const result={...eventMethods,media:query,_previous:bridge('media',0,query),get matches(){return bridge('media',0,query);},addListener(fn){this.addEventListener('change',fn);},removeListener(fn){this.removeEventListener('change',fn);}};mediaLists.add(result);return result;};
  globalThis.getComputedStyle=el=>new Proxy({getPropertyValue:key=>bridge('computed',el._key,String(key))},{get(o,key){if(key in o)return o[key];return o.getPropertyValue(String(key).replace(/[A-Z]/g,c=>'-'+c.toLowerCase()));}});
  let fetchID=0;const fetches=new Map();
  globalThis.fetch=(url,options={})=>{
    if(options.method&&String(options.method).toUpperCase()!=='GET')return Promise.reject(new TypeError('Only GET fetch is available'));
    if(options.body||options.headers||options.mode==='no-cors')return Promise.reject(new TypeError('Unsupported fetch options'));
    return new Promise((resolve,reject)=>{if(fetches.size>=32)throw new RangeError('Fetch limit');const id=++fetchID;bridge('fetch',0,id,String(url));fetches.set(id,{resolve,reject});});
  };
  // Host hooks are captured by C++, then removed from the page global object.
  globalThis.__visualHooks={
    focus(id){setFocus(id?wrap(id):null);},
    rejected(promise,reason,handled){const event=new Event(handled?'rejectionhandled':'unhandledrejection',{cancelable:!handled});event.promise=promise;event.reason=reason;return globalThis.dispatchEvent(event);},
    viewport(){const viewport=bridge('viewport',0);if(viewport.width!==lastViewport.width||viewport.height!==lastViewport.height){lastViewport=viewport;globalThis.dispatchEvent(new Event('resize'));for(const media of mediaLists){const matches=media.matches;if(matches!==media._previous){media._previous=matches;const event=new Event('change');event.matches=matches;event.media=media.media;media.dispatchEvent(event);}}}},
    task(now){let selected=null,due=Infinity;for(const [id,t] of timers){if(t.due<=now&&t.due<due){selected=id;due=t.due;}}if(selected===null)return false;const t=timers.get(selected);if(t.repeat)t.due=now+t.repeat;else timers.delete(selected);try{Reflect.apply(t.fn,globalThis,t.args);}catch(e){bridge('report',0,String(e));}return true;},
    beginFrame(now){frameTime=now;frameIDs=[...frames.keys()];},
    frame(){while(frameIDs.length){const id=frameIDs.shift(),fn=frames.get(id);if(!fn)continue;frames.delete(id);try{Reflect.apply(fn,globalThis,[frameTime]);}catch(e){bridge('report',0,String(e));}return true;}return false;},
    interactive(){readyState='interactive';document.dispatchEvent(new Event('readystatechange'));},
    domReady(){document.dispatchEvent(new Event('DOMContentLoaded',{bubbles:true}));},
    complete(){readyState='complete';document.dispatchEvent(new Event('readystatechange'));},
    loaded(){globalThis.dispatchEvent(new Event('load'));},
    event(id,type,cancelable,submitter){const event=new Event(type,{bubbles:!['focus','blur','load','error'].includes(type),cancelable});if(type==='submit')event.submitter=submitter>=0?wrap(submitter):null;return wrap(id).dispatchEvent(event);},
    inline(id,type,fn){wrap(id)['on'+type]=fn;},
    fetched(id,ok,status,url,body,error){const f=fetches.get(id);if(!f)return;fetches.delete(id);if(error){f.reject(new TypeError(error));return;}f.resolve({ok,status,url,text:()=>Promise.resolve(body),json:()=>Promise.resolve().then(()=>JSON.parse(body))});},
    blockEval(){const blocked=()=>{throw new EvalError('String compilation blocked by CSP');};for(const fn of [function(){},async function(){},function*(){},async function*(){}])Object.defineProperty(Object.getPrototypeOf(fn),'constructor',{value:blocked,writable:false,configurable:false});globalThis.eval=blocked;globalThis.Function=blocked;}
  };
})();

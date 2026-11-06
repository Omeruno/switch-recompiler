// Wrappers delegate to the actual litehtml tree. There is no second document.
(() => {
  const bridge=globalThis.__native;
  const defineNativeProperty=Object.defineProperty, nativePrototype=Object.getPrototypeOf;
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
  class EventTarget {}
  Object.assign(EventTarget.prototype,eventMethods);
  const nodeToken=Symbol();
  class Node extends EventTarget {
    constructor(key,token){super();if(token!==nodeToken)throw new TypeError('Illegal constructor');Object.defineProperty(this,'_key',{value:key});}
    get nodeType(){return this._key===0?9:bridge('kind',this._key);}
    get nodeName(){return this.nodeType===9?'#document':this.nodeType===3?'#text':this.nodeType===11?'#document-fragment':this.tagName;}
    get tagName(){return this.nodeType===1?bridge('tag',this._key).toUpperCase():undefined;}
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
    get nodeValue(){return this.nodeType===3?this.textContent:null;}set nodeValue(v){if(this.nodeType===3)this.textContent=v;}
    get data(){return this.nodeType===3?this.textContent:undefined;}set data(v){if(this.nodeType!==3)throw new TypeError('Expected CharacterData');this.textContent=v;}
    get previousSibling(){const a=this.parentNode?.childNodes||[];return a[a.indexOf(this)-1]||null;}
    get innerHTML(){return bridge('html',this._key);}
    set innerHTML(v){bridge('html',this._key,String(v));}
    get innerText(){return this.textContent;}set innerText(v){this.textContent=v;}
    appendChild(node){if(!(node instanceof Node))throw new TypeError('Expected Node');bridge('append',this._key,node._key);return node;}
    append(...items){bridge('batch',this._key,items.map(v=>(v instanceof Node?v:document.createTextNode(String(v)))._key),0);}
    insertBefore(node,before){if(!(node instanceof Node))throw new TypeError('Expected Node');bridge('insert',this._key,node._key,before?before._key:-1);return node;}
    prepend(...items){bridge('batch',this._key,items.map(v=>(v instanceof Node?v:document.createTextNode(String(v)))._key),1);}
    removeChild(node){bridge('remove',this._key,node._key);return node;}
    remove(){if(this.parentNode)this.parentNode.removeChild(this);}
    replaceChildren(...items){bridge('batch',this._key,items.map(v=>(v instanceof Node?v:document.createTextNode(String(v)))._key),2);}
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
    get async(){return this.tagName==='SCRIPT'?bridge('scriptAsync',this._key):false;}set async(v){if(this.tagName!=='SCRIPT')return;bridge('scriptForce',this._key);v?this.setAttribute('async',''):this.removeAttribute('async');}
    get defer(){return this.hasAttribute('defer');}set defer(v){v?this.setAttribute('defer',''):this.removeAttribute('defer');}
    get noModule(){return this.hasAttribute('nomodule');}set noModule(v){v?this.setAttribute('nomodule',''):this.removeAttribute('nomodule');}
    get text(){return this.tagName==='SCRIPT'?this.textContent:undefined;}set text(v){this.textContent=v;}
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
    getBoundingClientRect(){return DOMRect.fromRect(bridge('rect',this._key));}
    get offsetWidth(){return this.getBoundingClientRect().width;}get offsetHeight(){return this.getBoundingClientRect().height;}
    focus(){setFocus(this);}
    blur(){if(currentFocus()===this)setFocus(null);}
    click(){this.dispatchEvent(new Event('click',{bubbles:true,cancelable:true}));}
  }
  for(const name of ['lang','dir','title','rel','target','placeholder','integrity','crossOrigin','content'])Object.defineProperty(Node.prototype,name,{get(){return this.getAttribute(name)||'';},set(value){this.setAttribute(name,value);}});
  Object.defineProperty(Node.prototype,'httpEquiv',{get(){return this.getAttribute('http-equiv')||'';},set(v){this.setAttribute('http-equiv',v);}});
  Object.defineProperty(Node.prototype,'src',{get(){const value=this.getAttribute('src');return value?bridge('resolve',0,value):'';},set(v){this.setAttribute('src',v);}});
  Object.assign(Node.prototype,eventMethods);
  const elementBrand=new WeakSet();
  class Element extends Node {constructor(key,token){super(key,token);elementBrand.add(this);}}
  class HTMLElement extends Element {}
  class Document extends Node {}
  class HTMLDocument extends Document {}
  class CharacterData extends Node {}
  class Text extends CharacterData {
    constructor(data='',token){const internal=token===nodeToken;const key=internal?data:bridge('createText',0,String(data));super(key,nodeToken);if(!internal)nodes.set(key,this);}
  }
  class DocumentFragment extends Node {
    constructor(tokenOrKey,token){const internal=token===nodeToken;const key=internal?tokenOrKey:bridge('createFragment',0);super(key,nodeToken);if(!internal)nodes.set(key,this);}
  }
  const scriptBrand=new WeakSet();
  class HTMLScriptElement extends HTMLElement {
    constructor(key,token){super(key,token);scriptBrand.add(this);}
    static supports(type){return String(type)==='classic';}
  }
  class HTMLInputElement extends HTMLElement {}
  class HTMLTextAreaElement extends HTMLElement {}
  class HTMLSelectElement extends HTMLElement {}
  class HTMLOptionElement extends HTMLElement {}
  class HTMLButtonElement extends HTMLElement {}
  class HTMLFormElement extends HTMLElement {}
  class HTMLImageElement extends HTMLElement {}
  class HTMLAnchorElement extends HTMLElement {}
  class HTMLLinkElement extends HTMLElement {}
  class HTMLStyleElement extends HTMLElement {}
  class HTMLMetaElement extends HTMLElement {}
  class HTMLDivElement extends HTMLElement {}
  class HTMLSpanElement extends HTMLElement {}
  class HTMLBodyElement extends HTMLElement {}
  class HTMLHeadElement extends HTMLElement {}
  class HTMLHtmlElement extends HTMLElement {}
  const htmlInterfaces={script:HTMLScriptElement,input:HTMLInputElement,textarea:HTMLTextAreaElement,
    select:HTMLSelectElement,option:HTMLOptionElement,button:HTMLButtonElement,form:HTMLFormElement,
    img:HTMLImageElement,a:HTMLAnchorElement,link:HTMLLinkElement,style:HTMLStyleElement,meta:HTMLMetaElement,
    div:HTMLDivElement,span:HTMLSpanElement,body:HTMLBodyElement,head:HTMLHeadElement,html:HTMLHtmlElement};
  const domInterfaces={EventTarget,Node,Element,HTMLElement,Document,HTMLDocument,DocumentFragment,CharacterData,Text,
    HTMLScriptElement,HTMLInputElement,HTMLTextAreaElement,HTMLSelectElement,HTMLOptionElement,
    HTMLButtonElement,HTMLFormElement,HTMLImageElement,HTMLAnchorElement,HTMLLinkElement,HTMLStyleElement,
    HTMLMetaElement,HTMLDivElement,HTMLSpanElement,HTMLBodyElement,HTMLHeadElement,HTMLHtmlElement};
  for(const [name,ctor] of Object.entries(domInterfaces))Object.defineProperty(ctor.prototype,Symbol.toStringTag,{value:name,configurable:true});
  for(const [name,value] of Object.entries({ELEMENT_NODE:1,ATTRIBUTE_NODE:2,TEXT_NODE:3,CDATA_SECTION_NODE:4,
    ENTITY_REFERENCE_NODE:5,ENTITY_NODE:6,PROCESSING_INSTRUCTION_NODE:7,COMMENT_NODE:8,DOCUMENT_NODE:9,
    DOCUMENT_TYPE_NODE:10,DOCUMENT_FRAGMENT_NODE:11,NOTATION_NODE:12})){
    Object.defineProperty(Node,name,{value,enumerable:true});Object.defineProperty(Node.prototype,name,{value,enumerable:true});
  }
  for(const name of ['innerHTML','tagName','id','className','classList','getAttribute','setAttribute','removeAttribute','hasAttribute','matches','closest','getBoundingClientRect']){
    const descriptor=Object.getOwnPropertyDescriptor(Node.prototype,name);
    const checked=fn=>function(...args){if(!elementBrand.has(this))throw new TypeError('Illegal invocation');return Reflect.apply(fn,this,args);};
    Object.defineProperty(Element.prototype,name,descriptor.value?{value:checked(descriptor.value),writable:true,enumerable:true,configurable:true}:
      {get:descriptor.get?checked(descriptor.get):undefined,set:descriptor.set?checked(descriptor.set):undefined,enumerable:true,configurable:true});
  }
  // Script loaders often instrument these native-facing property descriptors.
  // Brand checks prevent treating arbitrary elements as scripts.
  for(const name of ['src','type','text','textContent','innerText','async','defer','noModule','integrity','crossOrigin']){
    const descriptor=Object.getOwnPropertyDescriptor(Node.prototype,name);
    const checked=fn=>function(...args){if(!scriptBrand.has(this))throw new TypeError('Illegal invocation');return Reflect.apply(fn,this,args);};
    Object.defineProperty(HTMLScriptElement.prototype,name,{get:descriptor.get?checked(descriptor.get):undefined,
      set:descriptor.set?checked(descriptor.set):undefined,enumerable:true,configurable:true});
  }
  function currentFocus(){const key=bridge('activeElement',0);return key===null?null:wrap(key);}
  function setFocus(target){
    const old=currentFocus();bridge('focus',target?target._key:0);const next=currentFocus();
    if(old===next)return;
    if(old){const blur=new Event('blur');blur.relatedTarget=next;old.dispatchEvent(blur);const out=new Event('focusout',{bubbles:true});out.relatedTarget=next;old.dispatchEvent(out);}
    // A blur handler may already have moved focus elsewhere.
    if(next&&currentFocus()===next){const focus=new Event('focus');focus.relatedTarget=old;next.dispatchEvent(focus);if(currentFocus()===next){const into=new Event('focusin',{bubbles:true});into.relatedTarget=old;next.dispatchEvent(into);}}
  }
  function wrap(key){
    if(!nodes.has(key)){
      const kind=key===0?9:bridge('kind',key);
      const ctor=kind===9?HTMLDocument:kind===3?Text:kind===11?DocumentFragment:htmlInterfaces[bridge('tag',key)]||HTMLElement;
      nodes.set(key,new ctor(key,nodeToken));
    }
    return nodes.get(key);
  }
  const document=wrap(0);
  function documentReceiver(value){if(value!==document)throw new TypeError('Illegal invocation');}
  Document.prototype.createElement=function(tag){documentReceiver(this);return wrap(bridge('create',0,String(tag)));};
  Document.prototype.createTextNode=function(text){documentReceiver(this);return wrap(bridge('createText',0,String(text)));};
  Document.prototype.createDocumentFragment=function(){documentReceiver(this);return wrap(bridge('createFragment',0));};
  Document.prototype.getElementById=function(id){documentReceiver(this);const key=bridge('id',0,String(id));return key?wrap(key):null;};
  Document.prototype.getElementsByClassName=function(s){documentReceiver(this);return query(0,'.'+String(s).trim().split(/\s+/).join('.'));};
  Document.prototype.getElementsByTagName=function(s){documentReceiver(this);return query(0,String(s));};
  Object.defineProperties(document,{
    documentElement:{get:()=>wrap(bridge('root',0))},body:{get:()=>document.querySelector('body')},head:{get:()=>document.querySelector('head')},
    title:{get:()=>document.querySelector('title')?.textContent||'',set:v=>{const t=document.querySelector('title');if(t)t.textContent=v;}},
    currentScript:{get:()=>{const key=bridge('currentScript',0);return key===null?null:wrap(key);}},
    readyState:{get:()=>readyState},activeElement:{get:()=>currentFocus()||document.body},
    cookie:{get:()=>bridge('cookie',0),set:v=>bridge('cookie',0,String(v))},
    referrer:{get:()=>''},
    URL:{get:()=>bridge('url',0)}
  });
  Object.assign(globalThis,eventMethods,domInterfaces,{window:globalThis,self:globalThis,document,Event,CustomEvent});
  const location={get search(){const q=bridge('urlPart',0,bridge('url',0),2);return q?'?'+q:'';},get hostname(){return bridge('urlPart',0,bridge('url',0),0);},get pathname(){return bridge('urlPart',0,bridge('url',0),1)||'/';},get origin(){return bridge('origin',0,bridge('url',0));},get href(){return bridge('url',0);},set href(v){bridge('navigate',0,String(v));},assign(v){this.href=v;},replace(v){this.href=v;},reload(){bridge('reload',0);}};
  Object.defineProperty(globalThis,'location',{get:()=>location,set:v=>location.href=v});
  Object.assign(globalThis,{navigator:{language:'ru-RU',languages:['ru-RU','ru','en'],userAgent:'SwitchVisualBrowser/0.8.0'},console:{log(){},warn(){},error(){}},performance:{now:()=>bridge('now',0)}});
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
  // Mutation records originate from committed native DOM operations, not a shadow tree.
  const observerState=new WeakMap(), activeObservers=new Set(), pendingObservers=new Set();
  let observerQueued=false, observerRecords=0, observerBytes=0, nextObserver=0;
  const recordCosts=new WeakMap();
  function observerError(code){bridge('observerLimit',0,code);}
  function observerMicrotask(){
    observerQueued=false;
    const batch=[...pendingObservers].sort((a,b)=>observerState.get(a).index-observerState.get(b).index);pendingObservers.clear();
    for(const observer of batch){
      const state=observerState.get(observer);state.transient.clear();
      const records=observer.takeRecords();
      if(records.length){try{Reflect.apply(state.callback,observer,[records,observer]);}catch(e){bridge('report',0,String(e));}}
    }
  }
  function scheduleObservers(observer){
    pendingObservers.add(observer);
    if(!observerQueued){observerQueued=true;bridge('microtask',0,observerMicrotask);}
  }
  const mutationRecordKey=Symbol();
  class MutationRecord {
    constructor(type,target,added,removed,previous,next,name,old,key){
      if(key!==mutationRecordKey)throw new TypeError('Illegal constructor');
      Object.defineProperties(this,Object.fromEntries(Object.entries({type,target,addedNodes:added,removedNodes:removed,previousSibling:previous,nextSibling:next,attributeName:name,attributeNamespace:null,oldValue:old}).map(([k,value])=>[k,{value,enumerable:true}])));
    }
  }
  class MutationObserver {
    constructor(callback){if(typeof callback!=='function')throw new TypeError('Observer requires function');observerState.set(this,{callback,index:++nextObserver,targets:new Map(),transient:new Map(),records:[]});}
    observe(target,options){
      const state=observerState.get(this);if(!state)throw new TypeError('Expected observer');
      if(!(target instanceof Node))throw new TypeError('Expected Node');
      if(options==null)throw new TypeError('Observer options required');
      const o=Object(options),has=k=>o[k]!==undefined;
      const attributes=has('attributes')?!!o.attributes:has('attributeOldValue')||has('attributeFilter');
      const characterData=has('characterData')?!!o.characterData:has('characterDataOldValue');
      const normalized={source:target._key,childList:!!o.childList,attributes,characterData,subtree:!!o.subtree,attributeOldValue:!!o.attributeOldValue,characterDataOldValue:!!o.characterDataOldValue,attributeFilter:has('attributeFilter')?Array.from(o.attributeFilter,String):null};
      if(!normalized.childList&&!attributes&&!characterData||!attributes&&(normalized.attributeOldValue||normalized.attributeFilter!==null)||!characterData&&normalized.characterDataOldValue)throw new TypeError('Invalid observer options');
      if(normalized.attributeFilter?.length>64)throw new RangeError('Attribute filter limit');
      if(!activeObservers.has(this)&&activeObservers.size>=64)throw new RangeError('Observer limit');
      if(!state.targets.has(target._key)&&state.targets.size>=32)throw new RangeError('Observer target limit');
      state.targets.set(target._key,normalized);for(const [id,entries] of state.transient){const left=entries.filter(o=>o.source!==target._key);left.length?state.transient.set(id,left):state.transient.delete(id);}activeObservers.add(this);bridge('observerActive',0,true);
    }
    disconnect(){
      const state=observerState.get(this);if(!state)throw new TypeError('Expected observer');
      state.targets.clear();state.transient.clear();this.takeRecords();activeObservers.delete(this);pendingObservers.delete(this);
      bridge('observerActive',0,activeObservers.size>0);
    }
    takeRecords(){const state=observerState.get(this);if(!state)throw new TypeError('Expected observer');const records=state.records;state.records=[];observerRecords-=records.length;for(const record of records)observerBytes-=recordCosts.get(record)||0;return records;}
  }
  globalThis.MutationObserver=MutationObserver;globalThis.MutationRecord=MutationRecord;
  function mutation(type,key,added,removed,previous,next,name,old,ancestors){
    for(const observer of activeObservers){
      const state=observerState.get(observer),matching=[];
      for(const id of ancestors){
        const option=state.targets.get(id);if(option&&(id===key||option.subtree))matching.push(option);
        for(const transient of state.transient.get(id)||[])matching.push(transient);
      }
      // Retain subtree observations until the next notification after a removal.
      if(type==='childList'&&removed.length){
        const inherited=matching.filter(o=>o.subtree);
        if(inherited.length){for(const id of removed){if(state.transient.size>=128&&!state.transient.has(id)){observerError('transient');break;}state.transient.set(id,[...new Set([...(state.transient.get(id)||[]),...inherited])]);}scheduleObservers(observer);}
      }
      let interested=false,wantsOld=false;
      for(const option of matching){
        if(!option[type])continue;
        if(type==='attributes'&&option.attributeFilter&&!option.attributeFilter.includes(name))continue;
        interested=true;wantsOld ||=type==='attributes'?option.attributeOldValue:type==='characterData'?option.characterDataOldValue:false;
      }
      if(!interested)continue;
      const cost=128+(added.length+removed.length)*8+(wantsOld&&old?old.length*2:0);
      if(state.records.length>=256||observerRecords>=1024||observerBytes+cost>1048576){observerError('records');scheduleObservers(observer);continue;}
      const list=ids=>{const a=ids.map(wrap);Object.defineProperty(a,'item',{value:i=>a[i]??null});return Object.freeze(a);};
      const record=new MutationRecord(type,wrap(key),list(added),list(removed),previous<0?null:wrap(previous),next<0?null:wrap(next),name,wantsOld?old:null,mutationRecordKey);
      state.records.push(record);recordCosts.set(record,cost);observerBytes+=cost;++observerRecords;scheduleObservers(observer);
    }
  }
  // Geometry comes from the retained renderer; no parallel JS layout tree.
  const rectData=new WeakMap();
  class DOMRectReadOnly {
    constructor(x=0,y=0,width=0,height=0){rectData.set(this,{x:Number(x),y:Number(y),width:Number(width),height:Number(height)});}
    get x(){return rectData.get(this).x;}get y(){return rectData.get(this).y;}
    get width(){return rectData.get(this).width;}get height(){return rectData.get(this).height;}
    get top(){return Math.min(this.y,this.y+this.height);}get left(){return Math.min(this.x,this.x+this.width);}
    get bottom(){return Math.max(this.y,this.y+this.height);}get right(){return Math.max(this.x,this.x+this.width);}
    toJSON(){return {x:this.x,y:this.y,width:this.width,height:this.height,top:this.top,right:this.right,bottom:this.bottom,left:this.left};}
    static fromRect(r={}){return new DOMRectReadOnly(r.x,r.y,r.width,r.height);}
  }
  class DOMRect extends DOMRectReadOnly {
    get x(){return super.x;}set x(v){rectData.get(this).x=Number(v);}get y(){return super.y;}set y(v){rectData.get(this).y=Number(v);}
    get width(){return super.width;}set width(v){rectData.get(this).width=Number(v);}get height(){return super.height;}set height(v){rectData.get(this).height=Number(v);}
    static fromRect(r={}){return new DOMRect(r.x,r.y,r.width,r.height);}
  }
  const readonlyRect=r=>new DOMRectReadOnly(r.x,r.y,r.width,r.height);
  const geometry=target=>bridge('geometry',target._key);
  const roStates=new WeakMap(),ioStates=new WeakMap(),resizeObservers=new Set(),intersectionObservers=new Set(),intersectionPending=new Set();
  let layoutTargets=0,intersectionRecords=0;
  const layoutLimit=code=>bridge('layoutObserverLimit',0,code);
  const targetElement=target=>{if(!elementBrand.has(target))throw new TypeError('Expected Element');};
  function addLayoutTarget(active,observer,state,target,value){
    targetElement(target);if(!state.targets.has(target)){
      if(layoutTargets>=128||(!active.has(observer)&&resizeObservers.size+intersectionObservers.size>=32)){layoutLimit('targets');throw new RangeError('Layout observer limit');}
      ++layoutTargets;
    }
    state.targets.set(target,value);active.add(observer);
  }
  function removeLayoutTarget(active,observer,state,target){if(state.targets.delete(target))--layoutTargets;if(!state.targets.size)active.delete(observer);}
  function clearLayoutTargets(active,observer,state){layoutTargets-=state.targets.size;state.targets.clear();active.delete(observer);}
  const entryToken=Symbol(),resizeSizes=new WeakMap(),resizeEntries=new WeakMap(),intersectionEntries=new WeakMap();
  function readonlyFields(ctor,state,names){for(const name of names)Object.defineProperty(ctor.prototype,name,{enumerable:true,configurable:true,get(){const values=state.get(this);if(!values)throw new TypeError('Illegal invocation');return values[name];}});}
  class ResizeObserverSize {constructor(w,h,token){if(token!==entryToken)throw new TypeError('Illegal constructor');resizeSizes.set(this,{inlineSize:w,blockSize:h});}}
  readonlyFields(ResizeObserverSize,resizeSizes,['inlineSize','blockSize']);
  class ResizeObserverEntry {
    constructor(target,g,token){if(token!==entryToken)throw new TypeError('Illegal constructor');
      resizeEntries.set(this,{target,contentRect:readonlyRect(g.content),
        contentBoxSize:Object.freeze([new ResizeObserverSize(g.content.width,g.content.height,entryToken)]),
        borderBoxSize:Object.freeze([new ResizeObserverSize(g.border.width,g.border.height,entryToken)])});
    }
  }
  readonlyFields(ResizeObserverEntry,resizeEntries,['target','contentRect','contentBoxSize','borderBoxSize']);
  class ResizeObserver {
    constructor(callback){if(typeof callback!=='function')throw new TypeError('Expected callback');roStates.set(this,{callback,targets:new Map()});}
    observe(target,options={}){options=options??{};targetElement(target);const box=String(options.box??'content-box');if(!['content-box','border-box'].includes(box))throw new TypeError('Unsupported ResizeObserver box');
      const state=roStates.get(this),old=state.targets.get(target);if(old?.box===box)return;
      addLayoutTarget(resizeObservers,this,state,target,{box,last:[0,0]});
    }
    unobserve(target){targetElement(target);removeLayoutTarget(resizeObservers,this,roStates.get(this),target);}
    disconnect(){clearLayoutTargets(resizeObservers,this,roStates.get(this));}
  }
  let resizeQueue=[],resizeDepth=0,resizeNextDepth=0,resizeRounds=0,resizeDone=true,resizeSkipped=false;
  const resized=(record,g)=>{const r=record.box==='border-box'?g.border:g.content;return record.last[0]!==r.width||record.last[1]!==r.height;};
  function resizeLoopError(){layoutLimit('resize-loop');const event=new Event('error',{cancelable:true});event.message='ResizeObserver loop completed with undelivered notifications.';globalThis.dispatchEvent(event);}
  function resizeTask(){
    if(resizeDone)return false;
    if(!resizeQueue.length){
      resizeDepth=resizeNextDepth;resizeNextDepth=Infinity;resizeSkipped=false;
      for(const observer of [...resizeObservers]){const state=roStates.get(observer),entries=[];
        for(const [target,record] of state.targets){const g=geometry(target);if(!resized(record,g))continue;if(g.depth<=resizeDepth){resizeSkipped=true;continue;}
          entries.push({target,record,g});resizeNextDepth=Math.min(resizeNextDepth,g.depth);
        }
        if(entries.length)resizeQueue.push({observer,entries});
      }
      if(!resizeQueue.length||++resizeRounds>8){resizeDone=true;if(resizeSkipped||resizeQueue.length)resizeLoopError();resizeQueue=[];return false;}
    }
    const {observer,entries}=resizeQueue.shift(),state=roStates.get(observer),deliver=[];
    for(const {target,record} of entries){if(state.targets.get(target)!==record)continue;const g=geometry(target),r=record.box==='border-box'?g.border:g.content;record.last=[r.width,r.height];deliver.push(new ResizeObserverEntry(target,g,entryToken));}
    if(deliver.length)try{Reflect.apply(state.callback,observer,[deliver,observer]);}catch(e){bridge('report',0,String(e));}
    return true;
  }
  const marginParts=value=>{
    const text=String(value);if(text.length>512)throw new RangeError('rootMargin length');const tokens=text.trim().split(/\s+/);if(tokens.length<1||tokens.length>4)throw new SyntaxError('Invalid rootMargin');
    const parts=tokens.map(t=>{if(!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:px|%)$/.test(t))throw new SyntaxError('Expected px or % rootMargin');return {n:Number.parseFloat(t),unit:t.endsWith('%')?'%':'px'};});
    if(parts.some(p=>!Number.isFinite(p.n)||Math.abs(p.n)>100000))throw new RangeError('rootMargin limit');
    return [parts[0],parts[1]||parts[0],parts[2]||parts[0],parts[3]||parts[1]||parts[0]];
  };
  class IntersectionObserverEntry {
    constructor(target,time,bounds,root,intersection,intersecting,token){if(token!==entryToken)throw new TypeError('Illegal constructor');const area=bounds.width*bounds.height;
      intersectionEntries.set(this,{target,time,boundingClientRect:readonlyRect(bounds),rootBounds:readonlyRect(root),intersectionRect:readonlyRect(intersection),isIntersecting:intersecting,intersectionRatio:area?intersection.width*intersection.height/area:intersecting?1:0});
    }
  }
  readonlyFields(IntersectionObserverEntry,intersectionEntries,['target','time','boundingClientRect','rootBounds','intersectionRect','isIntersecting','intersectionRatio']);
  class IntersectionObserver {
    constructor(callback,options={}){
      options=options??{};if(typeof callback!=='function')throw new TypeError('Expected callback');const root=options.root??null;if(root!==null&&root!==document)targetElement(root);
      if(options.trackVisibility||(options.delay!==undefined&&Number(options.delay)!==0)||options.scrollMargin!==undefined)throw new TypeError('Unsupported intersection option');
      const margins=marginParts(options.rootMargin??'0px');let thresholds=options.threshold??0;
      if(thresholds!==null&&typeof thresholds==='object'&&typeof thresholds[Symbol.iterator]==='function'){const values=[];for(const value of thresholds){if(values.length>=64)throw new RangeError('Threshold limit');values.push(value);}thresholds=values;}else thresholds=[thresholds];if(thresholds.length>64)throw new RangeError('Threshold limit');
      thresholds=thresholds.map(Number).sort((a,b)=>a-b);if(thresholds.some(v=>!Number.isFinite(v)||v<0||v>1))throw new RangeError('Invalid threshold');if(!thresholds.length)thresholds=[0];
      ioStates.set(this,{callback,root,margins,thresholds,targets:new Map(),records:[]});
    }
    get root(){return ioStates.get(this).root;}get rootMargin(){return ioStates.get(this).margins.map(p=>p.n+p.unit).join(' ');}get thresholds(){return Object.freeze([...ioStates.get(this).thresholds]);}
    observe(target){const state=ioStates.get(this);targetElement(target);if(!state.targets.has(target))addLayoutTarget(intersectionObservers,this,state,target,{index:-1,intersecting:false});}
    unobserve(target){targetElement(target);removeLayoutTarget(intersectionObservers,this,ioStates.get(this),target);}
    disconnect(){clearLayoutTargets(intersectionObservers,this,ioStates.get(this));}
    takeRecords(){return takeIntersections(this);}
  }
  function intersect(a,b){const x=Math.max(a.x,b.x),y=Math.max(a.y,b.y),right=Math.min(a.x+a.width,b.x+b.width),bottom=Math.min(a.y+a.height,b.y+b.height);return {x,y,width:Math.max(0,right-x),height:Math.max(0,bottom-y),touches:right>=x&&bottom>=y};}
  function sampleIntersections(time){
    for(const observer of [...intersectionObservers]){const state=ioStates.get(observer);
      for(const [target,record] of state.targets){const g=bridge('intersection',target._key,state.root&&state.root!==document?state.root._key:-1),r=g.root,m=state.margins.map(p=>p.unit==='%'?p.n*r.width/100:p.n);
        const root={x:r.x-m[3],y:r.y-m[0],width:r.width+m[1]+m[3],height:r.height+m[0]+m[2]};
        let area=intersect(g.border,root),visible=g.rendered&&g.descendant&&root.width>=0&&root.height>=0&&area.touches;
        for(const clip of g.clips){const next=intersect(area,clip);visible=visible&&next.touches;area=next;}
        if(!visible)area={x:0,y:0,width:0,height:0};const entry=new IntersectionObserverEntry(target,time,g.border,root,area,visible,entryToken);
        const index=state.thresholds.findIndex(t=>t>entry.intersectionRatio),thresholdIndex=index<0?state.thresholds.length:index;
        if(record.index===thresholdIndex&&record.intersecting===visible)continue;
        if(state.records.length>=128||intersectionRecords>=512){layoutLimit('intersection-records');continue;}
        record.index=thresholdIndex;record.intersecting=visible;state.records.push(entry);++intersectionRecords;intersectionPending.add(observer);
      }
    }
  }
  function takeIntersections(observer){const state=ioStates.get(observer),records=state.records;state.records=[];intersectionRecords-=records.length;intersectionPending.delete(observer);return records;}
  let preferIntersection=true;
  function intersectionTask(){for(const observer of intersectionPending){const state=ioStates.get(observer),records=takeIntersections(observer);if(records.length)try{Reflect.apply(state.callback,observer,[records,observer]);}catch(e){bridge('report',0,String(e));}return true;}return false;}
  for(const [name,ctor] of Object.entries({DOMRectReadOnly,DOMRect,ResizeObserver,ResizeObserverEntry,ResizeObserverSize,IntersectionObserver,IntersectionObserverEntry})){
    Object.defineProperty(ctor.prototype,Symbol.toStringTag,{value:name,configurable:true});globalThis[name]=ctor;
  }
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
  function scrollWindow(relative,x,y){if(x!==null&&typeof x==='object'){const options=x;if(options.behavior&& !['auto','instant'].includes(options.behavior))throw new TypeError('Smooth scrolling unavailable');x=options.left??(relative?0:globalThis.scrollX);y=options.top??(relative?0:globalThis.scrollY);}else{x=Number(x)||0;y=Number(y)||0;}bridge('scroll',0,Number(x)+(relative?globalThis.scrollX:0),Number(y)+(relative?globalThis.scrollY:0));}
  globalThis.scroll=globalThis.scrollTo=(x,y)=>scrollWindow(false,x,y);globalThis.scrollBy=(x,y)=>scrollWindow(true,x,y);
  Object.defineProperties(globalThis,{pageXOffset:{get:()=>globalThis.scrollX},pageYOffset:{get:()=>globalThis.scrollY}});
  const mediaLists=new Set(),mediaStates=new WeakMap();let lastViewport=bridge('viewport',0);
  class MediaQueryListEvent extends Event {constructor(type,options={}){super(type,options);Object.defineProperties(this,{matches:{value:!!options.matches,enumerable:true},media:{value:String(options.media??''),enumerable:true}});}}
  const mediaToken=Symbol();
  function trackMedia(list){const state=mediaStates.get(list),active=!!state.onchange||!!listeners.get(list)?.get('change')?.length;
    if(active&&!mediaLists.has(list)){if(mediaLists.size>=32)throw new RangeError('Media query listener quota');state.previous=list.matches;mediaLists.add(list);}else if(!active)mediaLists.delete(list);
  }
  class MediaQueryList extends EventTarget {
    constructor(query,token){super();if(token!==mediaToken)throw new TypeError('Illegal constructor');mediaStates.set(this,{query,onchange:null,previous:bridge('media',0,query)});}
    get media(){return mediaStates.get(this).query;}get matches(){return bridge('media',0,this.media);}
    get onchange(){return mediaStates.get(this).onchange;}set onchange(fn){const state=mediaStates.get(this),old=state.onchange;state.onchange=typeof fn==='function'?fn:null;try{trackMedia(this);}catch(e){state.onchange=old;throw e;}}
    addEventListener(type,fn,options){super.addEventListener(type,fn,options);try{trackMedia(this);}catch(e){super.removeEventListener(type,fn,options);throw e;}}
    removeEventListener(type,fn,options){super.removeEventListener(type,fn,options);trackMedia(this);}
    addListener(fn){this.addEventListener('change',fn);}removeListener(fn){this.removeEventListener('change',fn);}
  }
  for(const [name,ctor] of Object.entries({MediaQueryList,MediaQueryListEvent})){Object.defineProperty(ctor.prototype,Symbol.toStringTag,{value:name,configurable:true});globalThis[name]=ctor;}
  globalThis.matchMedia=query=>{query=String(query);if(query.length>512)throw new RangeError('Media query length');return new MediaQueryList(query,mediaToken);};
  function viewportEvents(){const viewport=bridge('viewport',0),previous=lastViewport;lastViewport=viewport;
    if(viewport.width!==previous.width||viewport.height!==previous.height||viewport.ratio!==previous.ratio){globalThis.dispatchEvent(new Event('resize'));
      for(const media of [...mediaLists]){const state=mediaStates.get(media),matches=media.matches;if(matches!==state.previous){state.previous=matches;media.dispatchEvent(new MediaQueryListEvent('change',{matches,media:media.media}));}trackMedia(media);}
    }
    if(viewport.scrollX!==previous.scrollX||viewport.scrollY!==previous.scrollY)document.dispatchEvent(new Event('scroll',{bubbles:true}));
  }
  globalThis.getComputedStyle=el=>new Proxy({getPropertyValue:key=>bridge('computed',el._key,String(key))},{get(o,key){if(key in o)return o[key];return o.getPropertyValue(String(key).replace(/[A-Z]/g,c=>'-'+c.toLowerCase()));}});
  let fetchID=0;const fetches=new Map();
  globalThis.fetch=(url,options={})=>{
    if(options.method&&String(options.method).toUpperCase()!=='GET')return Promise.reject(new TypeError('Only GET fetch is available'));
    if(options.body||options.headers||options.mode==='no-cors')return Promise.reject(new TypeError('Unsupported fetch options'));
    return new Promise((resolve,reject)=>{if(fetches.size>=32)throw new RangeError('Fetch limit');const id=++fetchID;bridge('fetch',0,id,String(url));fetches.set(id,{resolve,reject});});
  };
  // Host hooks are captured by C++, then removed from the page global object.
  globalThis.__visualHooks={
    mutated:mutation,
    focus(id){setFocus(id?wrap(id):null);},
    rejected(promise,reason,handled){const event=new Event(handled?'rejectionhandled':'unhandledrejection',{cancelable:!handled});event.promise=promise;event.reason=reason;return globalThis.dispatchEvent(event);},
    viewport:viewportEvents,
    beginObservers(){resizeQueue=[];resizeNextDepth=0;resizeRounds=0;resizeDone=false;resizeSkipped=false;},
    resize:resizeTask,
    endObservers(){if(!resizeDone){let pending=false;for(const observer of resizeObservers)for(const [target,record] of roStates.get(observer).targets)if(resized(record,geometry(target)))pending=true;if(pending)resizeLoopError();resizeQueue=[];resizeDone=true;}},
    intersections:sampleIntersections,
    task(now){let selected=null,due=Infinity;for(const [id,t] of timers){if(t.due<=now&&t.due<due){selected=id;due=t.due;}}if(intersectionPending.size&&(preferIntersection||selected===null)){preferIntersection=false;return intersectionTask();}if(selected===null)return false;preferIntersection=true;const t=timers.get(selected);if(t.repeat)t.due=now+t.repeat;else timers.delete(selected);try{Reflect.apply(t.fn,globalThis,t.args);}catch(e){bridge('report',0,String(e));}return true;},
    beginFrame(now){frameTime=now;frameIDs=[...frames.keys()];},
    frame(){while(frameIDs.length){const id=frameIDs.shift(),fn=frames.get(id);if(!fn)continue;frames.delete(id);try{Reflect.apply(fn,globalThis,[frameTime]);}catch(e){bridge('report',0,String(e));}return true;}return false;},
    interactive(){readyState='interactive';document.dispatchEvent(new Event('readystatechange'));},
    domReady(){document.dispatchEvent(new Event('DOMContentLoaded',{bubbles:true}));},
    complete(){readyState='complete';document.dispatchEvent(new Event('readystatechange'));},
    loaded(){globalThis.dispatchEvent(new Event('load'));},
    event(id,type,cancelable,submitter){const event=new Event(type,{bubbles:!['focus','blur','load','error'].includes(type),cancelable});if(type==='submit')event.submitter=submitter>=0?wrap(submitter):null;return wrap(id).dispatchEvent(event);},
    inline(id,type,fn){wrap(id)['on'+type]=fn;},
    fetched(id,ok,status,url,body,error){const f=fetches.get(id);if(!f)return;fetches.delete(id);if(error){f.reject(new TypeError(error));return;}f.resolve({ok,status,url,text:()=>Promise.resolve(body),json:()=>Promise.resolve().then(()=>JSON.parse(body))});},
    blockEval(){const blocked=()=>{throw new EvalError('String compilation blocked by CSP');};for(const fn of [function(){},async function(){},function*(){},async function*(){}])defineNativeProperty(nativePrototype(fn),'constructor',{value:blocked,writable:false,configurable:false});globalThis.eval=blocked;globalThis.Function=blocked;}
  };
})();

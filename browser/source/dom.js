// Bounded reader DOM and event loop. No inline event attributes or script eval.
(() => {
  let nodes = 0, revision = 0, lastSnapshot = -1;
  const MAX_NODES = 2048, MAX_LISTENERS = 128;
  let listeners = 0;
  const decode = text => text.replace(/&(#x[0-9a-f]+|#\d+|amp|lt|gt|quot|apos|nbsp);/gi, (_, key) => {
    const named = {amp:'&',lt:'<',gt:'>',quot:'"',apos:"'",nbsp:' '};
    if (key[0] !== '#') return named[key.toLowerCase()];
    const n = key[1].toLowerCase() === 'x' ? parseInt(key.slice(2),16) : Number(key.slice(1));
    return n > 0 && n <= 0x10ffff && !(n >= 0xd800 && n <= 0xdfff) ? String.fromCodePoint(n) : '\ufffd';
  });
  class Event {
    constructor(type, options = {}) { this.type = String(type); this.bubbles = !!options.bubbles; this.cancelable = !!options.cancelable; this.defaultPrevented = false; this._stop = false; }
    preventDefault() { if (this.cancelable) this.defaultPrevented = true; }
    stopPropagation() { this._stop = true; }
  }
  class Node {
    constructor(tag = '', text = '') {
      if (++nodes > MAX_NODES) throw new RangeError('DOM node limit');
      this.tagName = tag.toUpperCase(); this.nodeType = tag ? 1 : 3;
      this._text = text; this.children = []; this.parentNode = null; this.attributes = Object.create(null); this._events = Object.create(null);
    }
    get textContent() { return this.nodeType === 3 ? this._text : this.children.map(n => n.textContent).join(''); }
    set textContent(value) { this.replaceChildren(String(value)); }
    append(...items) {
      for (let item of items) {
        if (!(item instanceof Node)) item = new Node('', String(item));
        for (let p = this; p; p = p.parentNode) if (p === item) throw new Error('DOM cycle');
        item.remove(); item.parentNode = this; this.children.push(item); ++revision;
      }
    }
    appendChild(node) { this.append(node); return node; }
    prepend(...items) { const old = this.children.slice(); this.replaceChildren(...items, ...old); }
    replaceChildren(...items) { for (const child of this.children) child.parentNode = null; this.children = []; ++revision; this.append(...items); }
    remove() { if (this.parentNode) { const a = this.parentNode.children; a.splice(a.indexOf(this),1); this.parentNode = null; ++revision; } }
    setAttribute(key,value) { key = String(key).toLowerCase(); if (key.length > 96 || String(value).length > 2048) throw new RangeError('DOM attribute limit'); this.attributes[key] = String(value); ++revision; }
    getAttribute(key) { return this.attributes[String(key).toLowerCase()] ?? null; }
    hasAttribute(key) { return this.getAttribute(key) !== null; }
    removeAttribute(key) { delete this.attributes[String(key).toLowerCase()]; ++revision; }
    get id() { return this.getAttribute('id') || ''; } set id(v) { this.setAttribute('id',v); }
    get className() { return this.getAttribute('class') || ''; } set className(v) { this.setAttribute('class',v); }
    get href() { const raw = this.getAttribute('href') || ''; try { return new URL(raw,location.href).href; } catch { return raw; } }
    set href(v) { this.setAttribute('href',v); }
    get dateTime() { return this.getAttribute('datetime') || ''; } set dateTime(v) { this.setAttribute('datetime',v); }
    get disabled() { return this.hasAttribute('disabled'); } set disabled(v) { if (v) this.setAttribute('disabled',''); else this.removeAttribute('disabled'); }
    get dataset() {
      const self = this;
      return new Proxy({}, {get(_,key) { return self.getAttribute('data-' + String(key).replace(/[A-Z]/g,c=>'-'+c.toLowerCase())) ?? undefined; },set(_,key,v) { self.setAttribute('data-' + String(key).replace(/[A-Z]/g,c=>'-'+c.toLowerCase()),v); return true; }});
    }
    get classList() {
      const self = this;
      return {add(...names) { self.className = [...new Set([...self.className.split(/\s+/).filter(Boolean),...names])].join(' '); },remove(...names) { self.className = self.className.split(/\s+/).filter(n=>!names.includes(n)).join(' '); },contains(name) { return self.className.split(/\s+/).includes(name); }};
    }
    matches(selector) {
      return String(selector).split(',').some(part => {
        part = part.trim(); if (!part) return false;
        const tag = /^([a-z][\w-]*|\*)/i.exec(part);
        if (tag) { if (tag[1] !== '*' && tag[1].toUpperCase() !== this.tagName) return false; part = part.slice(tag[0].length); }
        while (part) {
          let m = /^([.#])([\w-]+)/.exec(part);
          if (m) { if (m[1] === '#' ? this.id !== m[2] : !this.classList.contains(m[2])) return false; }
          else {
            m = /^\[([\w-]+)(?:(\^?=)["']?([^\]"']*)["']?)?\]/.exec(part);
            if (!m) return false;
            const value = this.getAttribute(m[1]);
            if (value === null || (m[2] === '=' && value !== m[3]) || (m[2] === '^=' && !value.startsWith(m[3]))) return false;
          }
          part = part.slice(m[0].length);
        }
        return !!this.tagName;
      });
    }
    querySelectorAll(selector) { const result = [], stack = [...this.children].reverse(); let visited = 0; while (stack.length) { if (++visited > MAX_NODES) throw new RangeError('DOM tree limit'); const child = stack.pop(); if (child.matches(selector)) result.push(child); for (let i=child.children.length-1;i>=0;--i) stack.push(child.children[i]); } return result; }
    querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
    closest(selector) { for (let node = this; node; node = node.parentNode) if (node.matches(selector)) return node; return null; }
    addEventListener(type,callback,options = {}) {
      if (typeof callback !== 'function') return;
      const list = this._events[type] ||= [];
      if (list.some(entry=>entry.callback===callback)) return;
      if (++listeners > MAX_LISTENERS) { --listeners; throw new RangeError('Event listener limit'); }
      list.push({callback,once:!!options.once});
    }
    removeEventListener(type,callback) { const list = this._events[type] || []; const at = list.findIndex(e=>e.callback===callback); if (at >= 0) { list.splice(at,1); --listeners; } }
    dispatchEvent(event) {
      event.target = this;
      for (let node = this; node; node = event.bubbles && !event._stop ? node.parentNode : null) {
        event.currentTarget = node;
        for (const entry of [...(node._events[event.type] || [])]) {
          if (entry.once) node.removeEventListener(event.type,entry.callback);
          try { entry.callback.call(node,event); } catch (e) { console.log(String(e)); }
        }
      }
      return !event.defaultPrevented;
    }
  }
  const doc = new Node('document'), root = new Node('html'), head = new Node('head'), body = new Node('body');
  doc.append(root); root.append(head,body);
  root.setAttribute('lang',document.documentElement.lang);
  const title = new Node('title'); title.textContent = document.title; head.append(title);
  const html = __sourceHTML;
  let parent = body, raw = '', titleReturn = body;
  const tokens = /<!--[\s\S]*?(?:-->|$)|<![^>]*>|<\/?[a-zA-Z][^>"']*(?:"[^"]*"[^>"']*|'[^']*'[^>"']*)*>|[^<]+|</g;
  let match;
  const voids = new Set(['area','base','br','col','embed','hr','img','input','link','meta','param','source','track','wbr']);
  while ((match = tokens.exec(html))) {
    const token = match[0];
    if (raw) { if (token.toLowerCase().startsWith('</'+raw)) raw = ''; continue; }
    if (token.startsWith('<!--') || token.startsWith('<!')) continue;
    if (token[0] !== '<') { parent.append(decode(token)); continue; }
    const close = /^<\//.test(token), tag = /^<\/?([\w-]+)/.exec(token)?.[1].toLowerCase();
    if (!tag) continue;
    if (tag === 'script' || tag === 'style') { if (!close) raw = tag; continue; }
    if (close && tag === 'title') { parent = titleReturn; continue; }
    if (close) { for (let p = parent; p && p !== root; p = p.parentNode) if (p.tagName.toLowerCase() === tag) { parent = p.parentNode; break; } continue; }
    let node;
    if (tag === 'html') node = root;
    else if (tag === 'head') node = head;
    else if (tag === 'body') node = body;
    else if (tag === 'title') { titleReturn = parent; node = title; node.replaceChildren(); }
    else { node = new Node(tag); parent.append(node); }
    const attributes = token.slice(token.toLowerCase().indexOf(tag)+tag.length,-1);
    const pattern = /([\w:-]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))?/g;
    let attr, count = 0;
    while ((attr = pattern.exec(attributes)) && count++ < 32) node.setAttribute(attr[1],decode(attr[2] ?? attr[3] ?? attr[4] ?? ''));
    if (!voids.has(tag) && !token.endsWith('/>')) parent = node;
  }
  // Keep the reader usable if the bounded source omitted the full document.
  if (!html) body.textContent = document.body.textContent;
  doc.body = body; doc.head = head; doc.documentElement = root;
  Object.defineProperty(doc,'title',{get:()=>title.textContent,set:value=>{title.textContent=value;}});
  Object.defineProperty(root,'lang',{get:()=>root.getAttribute('lang') || '',set:v=>root.setAttribute('lang',v)});
  doc.referrer = ''; doc.cookie = ''; doc.readyState = 'loading';
  doc.createElement = tag => new Node(String(tag));
  doc.createTextNode = text => new Node('',String(text));
  doc.getElementById = id => doc.querySelectorAll('*').find(n=>n.id===String(id)) || null;
  globalThis.document = doc; globalThis.Event = Event;
  globalThis.addEventListener = doc.addEventListener.bind(doc);
  globalThis.removeEventListener = doc.removeEventListener.bind(doc);
  const timers = new Map(); let nextId = 1;
  function schedule(callback,delay,repeat,args,raf = false) {
    if (typeof callback !== 'function') throw new TypeError('Timer callback must be a function');
    if (timers.size >= 64) throw new RangeError('Timer limit');
    delay = Number(delay); delay = Number.isFinite(delay) ? Math.max(repeat ? 4 : 1,Math.min(delay,2147483647)) : 1;
    const id = nextId++; timers.set(id,{callback,due:__clock()+delay,delay,repeat,args,raf}); return id;
  }
  globalThis.setTimeout = (fn,delay,...args)=>schedule(fn,delay,false,args);
  globalThis.setInterval = (fn,delay,...args)=>schedule(fn,delay,true,args);
  globalThis.clearTimeout = globalThis.clearInterval = id=>timers.delete(Number(id));
  globalThis.requestAnimationFrame = fn=>schedule(fn,16,false,[],true);
  globalThis.cancelAnimationFrame = globalThis.clearTimeout;
  globalThis.__pump = now => {
    nodes = 0;
    let count = 0;
    for (const [id,timer] of [...timers]) {
      if (++count > 64) break;
      if (timer.due > now || !timers.has(id)) continue;
      if (timer.repeat) timer.due = now + timer.delay; else timers.delete(id);
      try { timer.callback(...(timer.raf ? [now-__startedAt] : timer.args)); } catch (e) { console.log(String(e)); timers.delete(id); }
    }
  };
  globalThis.__ready = () => { doc.readyState = 'interactive'; doc.dispatchEvent(new Event('DOMContentLoaded')); doc.readyState = 'complete'; doc.dispatchEvent(new Event('load')); };
  const render = node => {
    if (node.nodeType === 3) return node.textContent;
    if (['HEAD','SCRIPT','STYLE'].includes(node.tagName) || node.hasAttribute('hidden')) return '';
    return node.children.map(render).join('') + (/^(P|DIV|LI|H[1-6]|BR|TIME|BUTTON)$/.test(node.tagName) ? '\n' : '');
  };
  globalThis.__cancelAll = () => timers.clear();
  globalThis.__snapshot = () => {
    if (lastSnapshot === revision) return null;
    const interactive = doc.querySelectorAll('a,button').filter(n=>!n.disabled).slice(0,64);
    globalThis.__interactive = interactive;
    lastSnapshot = revision;
    return {title:doc.title,text:render(body).replace(/[ \t\r]+/g,' ').replace(/\n\s*\n/g,'\n').trim(),links:interactive.map(n=>({href:n.tagName==='A'?n.href:'',label:n.textContent || n.href || 'Кнопка'}))};
  };
  globalThis.__click = index => { const target = __interactive[index]; const follow = target && !target.disabled ? target.dispatchEvent(new Event('click',{bubbles:true,cancelable:true})) : false; return {follow:!!follow,href:target?.tagName==='A'?target.href:''}; };
})();

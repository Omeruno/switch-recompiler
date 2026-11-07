(()=>{
 const canvas=document.getElementById('game-screen'),status=document.getElementById('game-status'),start=document.getElementById('game-start'),pause=document.getElementById('game-pause'),reset=document.getElementById('game-reset');
 const context=canvas.getContext('2d'),keys=new Set();let instance,image,request=0,paused=false,last=0,accumulator=0,lastScore=-1,touch=null,previousStart=false,previousReset=false;
 function message(text){status.textContent=text;}
 function draw(){const e=instance.exports;e.step(0,0,0,0,0,0);context.putImageData(image,0,0);}
 function frame(time){request=0;if(!instance)return;
  const e=instance.exports,pad=navigator.getGamepads()[0],buttons=pad?pad.buttons:[],pressed=i=>!!buttons[i]&&buttons[i].pressed;
  const toggle=pressed(9),restart=pressed(8);if(restart&&!previousReset){e.reset();lastScore=-1;}previousReset=restart;
  if(toggle&&!previousStart){previousStart=toggle;setPaused(!paused);return;}previousStart=toggle;
  if(paused){request=requestAnimationFrame(frame);return;}
  const axis=i=>pad&&Math.abs(pad.axes[i])>.2?Math.sign(pad.axes[i]):0;
  const dx=(keys.has('ArrowRight')||keys.has('KeyD')||pressed(15)?1:0)-(keys.has('ArrowLeft')||keys.has('KeyA')||pressed(14)?1:0)||axis(0);
  const dy=(keys.has('ArrowDown')||keys.has('KeyS')||pressed(13)?1:0)-(keys.has('ArrowUp')||keys.has('KeyW')||pressed(12)?1:0)||axis(1);
  accumulator+=Math.min(64,last?time-last:16);last=time;
  for(let n=0;accumulator>=16&&n<4;n++,accumulator-=16)e.step(dx,dy,keys.has('Space')||pressed(0)||pressed(1)?1:0,touch?touch.x:0,touch?touch.y:0,touch?1:0);
  context.putImageData(image,0,0);
  if(lastScore!==e.score.value){lastScore=e.score.value;message('Собрано огоньков: '+lastScore+' · WASM → canvas');}
  request=requestAnimationFrame(frame);
 }
 function setPaused(value){paused=value;pause.textContent=paused?'Продолжить':'Пауза';keys.clear();touch=null;if(request)cancelAnimationFrame(request);request=0;last=0;accumulator=0;if(instance)request=requestAnimationFrame(frame);}
 start.addEventListener('click',async()=>{start.disabled=true;try{if(!instance){message('Загрузка локального WASM…');const result=await WebAssembly.instantiateStreaming(fetch('game-core.wasm'));instance=result.instance;image=new ImageData(new Uint8ClampedArray(instance.exports.memory.buffer,0,128*80*4),128,80);}instance.exports.reset();lastScore=-1;setPaused(false);setTimeout(()=>{canvas.focus();scrollTo(0,scrollY+canvas.getBoundingClientRect().top-12);},0);}catch(e){message('Не удалось запустить: '+e.name+': '+e.message);}finally{start.disabled=false;}});
 pause.addEventListener('click',()=>{if(instance){setPaused(!paused);setTimeout(()=>canvas.focus(),0);}});
 reset.addEventListener('click',()=>{if(instance){instance.exports.reset();lastScore=-1;if(paused)draw();}});
 canvas.addEventListener('keydown',e=>{if(['ArrowUp','ArrowDown','ArrowLeft','ArrowRight','KeyW','KeyA','KeyS','KeyD','Space'].includes(e.code)){e.preventDefault();keys.add(e.code);}});
 canvas.addEventListener('keyup',e=>keys.delete(e.code));canvas.addEventListener('blur',()=>{keys.clear();touch=null;});
 function point(e){const r=canvas.getBoundingClientRect();touch={x:Math.max(0,Math.min(127,Math.floor((e.clientX-r.left)*128/r.width))),y:Math.max(0,Math.min(79,Math.floor((e.clientY-r.top)*80/r.height)))};}
 canvas.addEventListener('pointerdown',e=>{e.preventDefault();canvas.focus();canvas.setPointerCapture(e.pointerId);point(e);});
 canvas.addEventListener('pointermove',e=>{if(canvas.hasPointerCapture(e.pointerId)){e.preventDefault();point(e);}});
 for(const name of ['pointerup','pointercancel'])canvas.addEventListener(name,()=>{touch=null;});
 context.fillStyle='#0c1520';context.fillRect(0,0,128,80);
})();

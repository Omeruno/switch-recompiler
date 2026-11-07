(()=>{
 const check=(ok,label)=>{if(!ok)throw Error(label);};const throws=(fn,type)=>{let error;try{fn();}catch(e){error=e;}check(error instanceof type,'expected '+type.name);};
 const canvas=document.createElement('canvas');canvas.id='canvas';canvas.width=4;canvas.height=4;canvas.style.display='block';document.body.appendChild(canvas);
 check(canvas instanceof HTMLCanvasElement,'Canvas interface');const ctx=canvas.getContext('2d');check(ctx instanceof CanvasRenderingContext2D&&ctx===canvas.getContext('2d')&&ctx.canvas===canvas&&canvas.getContext('webgl')===null,'context/unsupported GPU');
 ctx.fillStyle='red';ctx.fillRect(0,0,4,4);check([...ctx.getImageData(0,0,1,1).data].join()==='255,0,0,255','fill');ctx.fillStyle='not-a-color';check(ctx.fillStyle==='#ff0000','invalid color ignored');
 ctx.save();ctx.globalAlpha=.5;ctx.fillStyle='blue';ctx.fillRect(0,0,1,1);const mix=ctx.getImageData(0,0,1,1).data;check(Math.abs(mix[0]-127)<=1&&Math.abs(mix[2]-128)<=1&&mix[3]===255,'source-over alpha');ctx.restore();check(ctx.globalAlpha===1&&ctx.fillStyle==='#ff0000','state restore');
 ctx.clearRect(0,0,1,1);check([...ctx.getImageData(0,0,1,1).data].join()==='0,0,0,0','clear');check([...ctx.getImageData(-1,-1,1,1).data].join()==='0,0,0,0','out of bounds transparent');
 const storage=new Uint8ClampedArray([9,9,9,9,10,20,30,255,40,50,60,255,8,8,8,8]),view=storage.subarray(4,12),image=new ImageData(view,2,1);check(image.data===view&&image.width===2&&image.height===1&&image.colorSpace==='srgb','ImageData view');image.data[0]=11;ctx.globalAlpha=0;ctx.putImageData(image,1,1);check([...ctx.getImageData(1,1,2,1).data].join()==='11,20,30,255,40,50,60,255','put ignores alpha/copies view range');
 ctx.putImageData(image,-1,2);check([...ctx.getImageData(0,2,1,1).data].join()==='40,50,60,255','put clips');ctx.putImageData(image,0,3,1,0,1,1);check([...ctx.getImageData(1,3,1,1).data].join()==='40,50,60,255','dirty rect');
 const width=canvas.width;throws(()=>canvas.width=2049,RangeError);check(canvas.width===width,'quota resize atomic');canvas.width=canvas.width;check(ctx.globalAlpha===1&&ctx.fillStyle==='#000000'&&[...ctx.getImageData(1,1,1,1).data].join()==='0,0,0,0','resize resets pixels/state');
 const source=document.createElement('canvas');source.width=2;source.height=1;const sc=source.getContext('2d');sc.putImageData(new ImageData(new Uint8ClampedArray([255,0,0,255,0,0,255,255]),2,1),0,0);ctx.imageSmoothingEnabled=false;ctx.drawImage(source,0,0,4,2);check([...ctx.getImageData(0,0,4,1).data].join()==='255,0,0,255,255,0,0,255,0,0,255,255,0,0,255,255','nearest scaling');
 ctx.drawImage(canvas,0,0,2,1,1,0,2,1);check([...ctx.getImageData(0,0,4,1).data].join()==='255,0,0,255,255,0,0,255,255,0,0,255,0,0,255,255','self-copy snapshot');
 canvas.style.width='200px';canvas.style.height='auto';check(canvas.getBoundingClientRect().width===200&&canvas.getBoundingClientRect().height===200,'intrinsic canvas aspect');
 globalThis.canvasFixtureDone=true;
})();

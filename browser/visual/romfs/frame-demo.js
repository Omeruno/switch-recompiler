(() => {
  let count=0;
  const notify=()=>{document.getElementById('count').textContent=String(count);document.getElementById('size').textContent=innerWidth+' × '+innerHeight;parent.postMessage({kind:'frame-demo',count,width:innerWidth,height:innerHeight},'*');};
  document.getElementById('inside').onclick=()=>{count++;notify();};
  addEventListener('resize',notify);
  notify();
})();

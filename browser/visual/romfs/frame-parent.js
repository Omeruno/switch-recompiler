(() => {
  const frame=document.getElementById('native-frame'),status=document.getElementById('frame-status');
  addEventListener('message',event=>{if(event.source!==frame.contentWindow||event.origin!=='demo:'||event.data?.kind!=='frame-demo')return;status.textContent='Ответ из фрейма: '+event.data.count+' · размер '+event.data.width+' × '+event.data.height;});
  let wide=true;
  document.getElementById('frame-size').onclick=()=>{wide=!wide;frame.style.width=wide?'600px':'400px';frame.style.height=wide?'190px':'240px';};
})();

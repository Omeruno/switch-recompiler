document.addEventListener('DOMContentLoaded',()=>{
  let count=0;
  const result=document.getElementById('js-result');
  result.textContent='JavaScript работает';
  document.getElementById('js-action').addEventListener('click',()=>{
    result.textContent='Нажатий: '+(++count);
    result.style.color='#125d79';
  });
  document.getElementById('search').addEventListener('input',event=>{
    document.getElementById('input-status').textContent='Введено: '+event.target.value;
  });
  document.getElementById('demo-check').addEventListener('click',event=>{
    document.getElementById('forms-state').textContent=event.target.checked?'Включено':'Выключено';
  });
  document.getElementById('demo-select').addEventListener('change',event=>{
    document.getElementById('forms-state').textContent='Выбрано: '+event.target.value;
  });
  document.getElementById('controls-demo').addEventListener('submit',event=>{
    event.preventDefault();
    document.getElementById('forms-result').textContent='Готово: '+document.getElementById('demo-select').value+' / '+event.submitter.id;
  });
  let mutationCount=0;
  const dynamicTarget=document.getElementById('dynamic-target');
  const dynamicObserver=new MutationObserver(records=>{
    mutationCount+=records.length;
    document.getElementById('dynamic-observed').textContent='Изменений: '+mutationCount;
  });
  dynamicObserver.observe(dynamicTarget,{childList:true,attributes:true});
  document.getElementById('dynamic-run').addEventListener('click',()=>{
    const script=document.createElement('script');script.src='dynamic-demo.js';
    script.onload=()=>{document.getElementById('dynamic-status').textContent='Готово';script.remove();};
    script.onerror=()=>{document.getElementById('dynamic-status').textContent='Ошибка загрузки';script.remove();};
    document.head.appendChild(script);
  });
  const layoutTarget=document.getElementById('layout-target');let wide=false;
  new ResizeObserver(entries=>{
    const box=entries[0].contentRect;
    document.getElementById('layout-size').textContent='Размер содержимого: '+Math.round(box.width)+' × '+Math.round(box.height);
  }).observe(layoutTarget);
  new IntersectionObserver(entries=>{
    document.getElementById('layout-visible').textContent=entries[0].isIntersecting?'Блок на экране':'Блок за экраном';
  }).observe(layoutTarget);
  document.getElementById('layout-resize').addEventListener('click',()=>{wide=!wide;layoutTarget.style.width=wide?'320px':'200px';});
  const layoutMedia=matchMedia('(min-width: 900px)');
  function updateLayoutMedia(){document.getElementById('layout-media').textContent=layoutMedia.matches?'Широкий режим':'Узкий режим';}
  layoutMedia.addEventListener('change',updateLayoutMedia);updateLayoutMedia();
  document.getElementById('network-xhr').addEventListener('click',()=>{
    const request=new XMLHttpRequest();request.open('GET','state.json');request.responseType='json';
    request.onload=()=>{document.getElementById('network-status').textContent='Код '+request.status+': '+request.response.message;};
    request.onerror=()=>{document.getElementById('network-status').textContent='Ошибка загрузки';};request.send();
  });
  document.getElementById('network-abort').addEventListener('click',()=>{
    const controller=new AbortController();
    fetch('state.json',{signal:controller.signal}).catch(error=>{document.getElementById('network-status').textContent=error.name==='AbortError'?'Загрузка отменена':'Ошибка загрузки';});controller.abort();
  });
  document.getElementById('js-fetch').addEventListener('click',()=>{
    document.getElementById('fetch-result').textContent='Загрузка…';
    fetch('state.json').then(response=>response.json()).then(data=>{
      document.getElementById('fetch-result').textContent=data.message;
    }).catch(()=>document.getElementById('fetch-result').textContent='Не удалось загрузить данные');
  });
});
document.addEventListener('DOMContentLoaded',()=>{
  let socket=null,received=0;
  const state=document.getElementById('websocket-status');
  document.getElementById('websocket-connect').onclick=()=>{
    if(socket&&socket.readyState<2){state.textContent='Сначала закрой текущее соединение.';return;}
    try{
      socket=new WebSocket(document.getElementById('websocket-url').value.trim());received=0;
      state.textContent='Соединение…';
      socket.onopen=()=>{state.textContent='Соединение открыто. Можно отправить проверку.';};
      socket.onmessage=event=>{received++;state.textContent='Получено сообщений: '+received+' · '+(typeof event.data==='string'?'текст':'бинарные данные');};
      socket.onerror=()=>{state.textContent='Не удалось обменяться данными. Проверь адрес и сеть.';};
      socket.onclose=event=>{state.textContent='Соединение закрыто: '+event.code+(event.wasClean?' · корректно':' · обрыв');};
    }catch(error){state.textContent='Не удалось открыть соединение. Нужен адрес wss://.';}
  };
  document.getElementById('websocket-send').onclick=()=>{
    if(!socket||socket.readyState!==WebSocket.OPEN){state.textContent='Сначала открой соединение.';return;}
    socket.send('Привет со Switch!');state.textContent='Проверочное сообщение отправлено.';
  };
  document.getElementById('websocket-close').onclick=()=>{if(socket)socket.close(1000);};
});

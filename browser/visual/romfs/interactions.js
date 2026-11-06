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
  document.getElementById('js-fetch').addEventListener('click',()=>{
    document.getElementById('fetch-result').textContent='Загрузка…';
    fetch('state.json').then(response=>response.json()).then(data=>{
      document.getElementById('fetch-result').textContent=data.message;
    }).catch(()=>document.getElementById('fetch-result').textContent='Не удалось загрузить данные');
  });
});

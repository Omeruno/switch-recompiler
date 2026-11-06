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
  document.getElementById('js-fetch').addEventListener('click',()=>{
    document.getElementById('fetch-result').textContent='Загрузка…';
    fetch('state.json').then(response=>response.json()).then(data=>{
      document.getElementById('fetch-result').textContent=data.message;
    }).catch(()=>document.getElementById('fetch-result').textContent='Не удалось загрузить данные');
  });
});

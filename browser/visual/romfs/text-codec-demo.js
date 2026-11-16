(() => {
  const status=document.getElementById('text-codec-status');
  function run(){
    try{
      const decoder=new TextDecoder('utf-8',{fatal:true});
      const text=decoder.decode(new Uint8Array([0xd0]),{stream:true})+decoder.decode(new Uint8Array([0x81]));
      const russian=new TextDecoder('windows-1251',{fatal:true}).decode(new Uint8Array([207,240,232,226,229,242]));
      const destination=new Uint8Array(12),encoded=new TextEncoder().encodeInto(russian,destination);
      let rejected=false;try{new TextDecoder('utf-8',{fatal:true}).decode(new Uint8Array([255]));}catch(error){rejected=error.name==='TypeError';}
      if(text!=='Ё'||russian!=='Привет'||encoded.read!==6||encoded.written!==12||!rejected)throw Error('Текст не совпал');
      status.textContent='Текст передан без искажений: '+russian+' · '+text+'. Повреждённые данные распознаны.';
    }catch(error){status.textContent='Ошибка проверки текста: '+String(error);}
  }
  document.getElementById('text-codec-run').addEventListener('click',run);run();
})();

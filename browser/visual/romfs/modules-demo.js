import {runs} from './module-state.js';
await Promise.resolve();
const status=document.getElementById('module-status');
status.textContent='Модули готовы';
document.getElementById('module-run').addEventListener('click',async()=>{
  try{const {run}=await import('./module-step.js');const count=await run();if(count!==runs)throw Error('live binding');status.textContent='Запуск модуля: '+count;}
  catch(error){status.textContent='Ошибка модуля: '+error.message;}
});

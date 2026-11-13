(() => {
  const status = document.getElementById('messaging-status');
  const button = document.getElementById('messaging-run');
  let channel, round = 0, pending = false;
  function failed(error) {
    pending = false;
    status.textContent = 'Ошибка обмена: ' + String(error);
  }
  try {
    channel = new MessageChannel();
    channel.port2.onmessage = event => {
      try {
        const data = event.data;
        if (data.self !== data || data.map.get(data) !== 'Русский текст' ||
            data.bytes[0] !== 73 || data.bytes.buffer.byteLength !== 8)
          throw Error('Данные повреждены');
        channel.port2.postMessage({ round: data.round, answer: 'Принято' });
      } catch (error) { failed(error); }
    };
    channel.port1.onmessage = event => {
      pending = false;
      status.textContent = 'Каналы работают. Проверка №' + event.data.round +
        ': данные переданы и ответ получен.';
    };
    function run() {
      if (pending) return;
      try {
        pending = true;
        status.textContent = 'Ожидание ответа…';
        const data = { round: ++round, bytes: new Uint8Array(8) };
        data.bytes[0] = 73;
        data.self = data;
        data.map = new Map([[data, 'Русский текст']]);
        channel.port1.postMessage(data, [data.bytes.buffer]);
        if (data.bytes.byteLength !== 0) throw Error('Буфер не передан');
      } catch (error) { failed(error); }
    }
    button.addEventListener('click', run);
    run();
  } catch (error) { failed(error); }
})();

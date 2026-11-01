#!/usr/bin/env python3
"""Prepare an isolated SD-card CPU test bundle from exact-SHA CI outputs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

p=argparse.ArgumentParser()
p.add_argument('--nro',type=Path,required=True)
p.add_argument('--fixtures',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
nro=a.nro.read_bytes()
assert nro[16:20]==b'NRO0' and sha.encode() in nro
files={'switch/cpu-batch-validation/cpu-batch-validation.nro':nro,
       'switch-recompiler/cpu-validation/native_dispatch.exe':(a.fixtures/'native_dispatch.exe').read_bytes(),
       'switch-recompiler/cpu-validation/native_dispatch.dll':(a.fixtures/'native_dispatch.dll').read_bytes(),
       'switch-recompiler/cpu-validation.flag':b'1\n'}
for path in ['switch-recompiler/cpu-validation/native_dispatch.exe','switch-recompiler/cpu-validation/native_dispatch.dll']:
    assert files[path][:2]==b'MZ'
def fnv(data):
    value=14695981039346656037
    for byte in data:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return f'{value:016x}'
manifest={'schema':1,'head_sha':sha,'hardware_status':'PENDING_USER_SWITCH_RUN',
          'files':{path:{'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'fnv1a64':fnv(data)} for path,data in files.items()},
          'report':'sd:/switch-recompiler-cpu-validation.txt'}
files['CPU_VALIDATION_MANIFEST.json']=(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode()
readme=f'''ПРОВЕРКА CPU НА НАСТОЯЩЕМ SWITCH — {sha}

1. Распакуй содержимое ZIP в корень SD,сохраняя структуру папок.
   Новый файл: /switch/cpu-batch-validation/cpu-batch-validation.nro.
   Основной NRO и игровые файлы этот набор не заменяет.
2. Запусти именно cpu-batch-validation.nro через hbmenu в обычном для тебя
   режиме. Никакого разгона/изменения частот тест не делает.
3. Должен открыться текстовый экран CPU batch validation,Build {sha}.
   Прогон автоматический:19 сеансов,reference/G5/native-step/native-batch,
   проверка реальных PE/DLL/callback/APC/worker,результатов и квантов.
   PLUS отменяет. Дождись итогового PASS/FAIL/UNAVAILABLE.
4. Пришли файл /switch-recompiler-cpu-validation.txt из корня SD.
   Напиши также:модель Switch,режим запуска hbmenu (applet/через игру),
   был ли разгон или сторонний профиль частот. Не меняй их между пробами.

Файл cpu-validation.flag автоматически сбрасывается в0,чтобы следующий
запуск не повторял тест. Для повторения верни в него1 или скопируй его из ZIP.
cpu-native.flag/cpu-reference.flag и обычные настройки тест не изменяет:
все три движка выбираются внутри проверки на одинаковом публичном EXE.

Если открылась обычная оболочка,проверь выбор NRO и расположение тестового
флага из ZIP. При зависании/ошибке пришли уже записанный отчёт и фото экрана.
Не переустанавливай SD и не повторяй старый JIT42 — здесь проверяется другой,
полный CPU-путь. PASS относится к этому публичному CPU-тесту,а не ко всем
играм/GPU/FPS. Аппаратная приёмка до твоего отчёта остаётся PENDING.
'''
files['CPU_VALIDATION_README_RU.txt']=readme.encode()
a.output.parent.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(a.output,'w',zipfile.ZIP_DEFLATED) as z:
    for path,data in files.items():z.writestr(path,data)
with zipfile.ZipFile(a.output) as z:
    assert z.testzip() is None
    for path,item in manifest['files'].items():assert hashlib.sha256(z.read(path)).hexdigest()==item['sha256']
print(f'CPU hardware bundle PASS: {a.output.name};exact SHA {sha};hardware run PENDING')

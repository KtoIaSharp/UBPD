// Страница LOCAL WEB UI вынесена в отдельный заголовок не случайно:
// генератор прототипов Arduino (ctags 5.8) не понимает C++ raw-строки в .ino
// и после такой строки перестаёт находить функции файла - сборка падает
// с "was not declared in this scope". В .h raw-строка безопасна, потому что
// содержимое заголовков не попадает в склеенный .cpp как текст.
// Правишь HTML/JS - правь здесь, это и есть страница UBPD.
#pragma once

static const char PAGE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>UBPD</title>
<style>
body{background:#0f0f0f;color:#ddd;font-family:ui-monospace,Consolas,monospace;margin:0;padding:12px}
h1{font-size:18px;margin:0 0 2px;letter-spacing:2px}h2{font-size:13px;margin:18px 0 4px;color:#8fd;letter-spacing:1px}
.s{color:#777;font-size:12px;line-height:1.4}
table{width:100%;border-collapse:collapse;font-size:12px}
td,th{border-bottom:1px solid #2a2a2a;padding:3px 4px;text-align:left;vertical-align:top}
th{color:#888;font-weight:normal}
tr.w td{color:#ff9}
button{background:#1d1d1d;border:1px solid #3a3a3a;color:#ddd;padding:3px 7px;border-radius:3px;
font-family:inherit;font-size:11px;margin:2px 2px 2px 0;cursor:pointer}
button.on{border-color:#6d6;color:#9f9}
input{background:#1d1d1d;border:1px solid #3a3a3a;color:#ddd;padding:3px 6px;width:110px;font-family:inherit;font-size:12px}
.ok{color:#7f7}.no{color:#f77}.dim{color:#666}
</style></head><body>
<h1>UBPD <span class="s" id="ver"></span></h1>
<div class="s">Unknown Bluetooth Pocket Device &mdash; локальная страница (без интернета)</div>
<div id="diag" class="s"></div>

<h2>УСТРОЙСТВА В ЭФИРЕ</h2>
<div id="devs" class="s">...</div>

<h2>КЛАССИЧЕСКИЙ BLUETOOTH (BR/EDR)</h2>
<div class="s">Так находят телефон, который по BLE молчит: включи на нём «виден всем».
<button onclick="cmd('/api/classic?on=1')">старт</button>
<button onclick="cmd('/api/classic?on=0')">стоп</button>
<span class="dim">BLE-скан и классический поиск делят радио — работают по очереди</span></div>
<div id="cls" class="s">...</div>

<h2>WATCH — СВОИ УСТРОЙСТВА</h2>
<div id="watch" class="s">...</div>

<h2>IDENTITY — ИМЯ В ЭФИРЕ</h2>
<div id="ident" class="s">...</div>

<h2>BLE FUN</h2>
<div id="fun" class="s">...</div>

<h2>НАСТРОЙКИ</h2>
<div id="set" class="s">...</div>

<h2>PC REMOTE — ТЕКСТ НА ЭКРАН</h2>
<div class="s">Введи текст — он покажется на экране UBPD несколько секунд.
<input id="oledt" value="ПРИВЕТ UBPD" style="width:180px">
<button onclick="sendOled()">показать</button>
<button onclick="cmd('/api/oled?clear=1')">стереть</button></div>

<h2>ДАННЫЕ</h2>
<button onclick="location='/api/log.csv'">Скачать RSSI-лог (CSV)</button>
<button onclick="cmd('/api/nvs/reset')">Сбросить настройки</button>
<button onclick="cmd('/api/reboot')">Перезагрузить UBPD</button>

<script>
function esc(s){return (s||'').replace(/[<>&"]/g,c=>({'<':'&lt;','>':'&gt;','&':'&amp;','"':'&quot;'}[c]));}
function cmd(u){fetch(u,{method:'POST'}).then(refresh);}
function sendOled(){fetch('/api/oled?secs=5',{method:'POST',body:document.getElementById('oledt').value}).then(refresh);}
function refresh(){fetch('/api/state').then(r=>r.json()).then(render).catch(e=>{});}
function render(s){
 document.getElementById('ver').textContent='v'+s.ver;
 document.getElementById('diag').innerHTML=
  'RAM '+s.heap+' КБ &middot; uptime '+s.uptime+' c &middot; BLE-скан: '+(s.ble?'<span class="ok">идёт</span>':'<span class="dim">стоп</span>')
  +' &middot; CLASSIC: '+(s.cls?'<span class="ok">идёт</span>':'<span class="dim">стоп</span>')+' (n'+s.clsCount+')'
  +' &middot; клиентов Wi-Fi: '+s.clients+'<br>своё BLE-имя: <b>'+esc(s.adv||'—')+'</b>'
  +' <button onclick="cmd(\'/api/scan?on='+(s.ble?0:1)+'\')">'+(s.ble?'стоп скан':'старт скан')+'</button>';
 var d='<table><tr><th>имя</th><th>тип</th><th>RSSI</th><th>MAC</th><th></th></tr>';
 if(!s.devices.length) d+='<tr><td colspan="5" class="dim">пока никого — включи скан</td></tr>';
 s.devices.forEach(function(x){
   d+='<tr'+(x.watch?' class="w"':'')+'><td>'+(x.name?esc(x.name):'<span class="dim">без имени</span>')+'</td><td>'+esc(x.type)+' '+x.conf+'%</td><td>'+x.rssi+'</td><td class="dim">'+x.mac+'</td>'
     +'<td>'+(x.watch?'<span class="ok">WATCH</span>':'')+'</td></tr>';
 });
 d+='</table>';
 document.getElementById('devs').innerHTML=d;

 var c='<table><tr><th>имя</th><th>тип</th><th>RSSI</th><th>MAC</th><th></th></tr>';
 if(!s.classic.length) c+='<tr><td colspan="5" class="dim">пока никого — устройство должно быть «видно всем»</td></tr>';
 s.classic.forEach(function(x){
   c+='<tr'+(x.watch?' class="w"':'')+'><td>'+(x.name?esc(x.name):'<span class="dim">без имени</span>')+'</td><td>'+esc(x.type)+'</td>'
     +'<td>'+(x.rssi==-128?'<span class="dim">--</span>':x.rssi)+'</td><td class="dim">'+x.mac+'</td>'
     +'<td>'+(x.watch?'<span class="ok">WATCH</span>':'<button onclick="cmd(\'/api/watch?mac='+x.mac+'\')">следить</button>')+'</td></tr>';
 });
 c+='</table>';
 document.getElementById('cls').innerHTML=c;

 var w='<table><tr><th>устройство</th><th>статус</th><th>MAC</th><th></th></tr>';
 if(!s.watch.length) w+='<tr><td colspan="4" class="dim">список пуст — отметь устройство галочкой выше</td></tr>';
 s.watch.forEach(function(x){
   w+='<tr class="w"><td>'+esc(x.name)+'</td><td>'+(x.found?'<span class="ok">НАЙДЕНО '+x.rssi+'</span>':'<span class="no">нет в эфире</span>')+'</td><td class="dim">'+x.mac+'</td>'
    +'<td><button onclick="cmd(\'/api/watch/del?mac='+x.mac+'\')">убрать</button></td></tr>';
 });
 w+='</table>';
 document.getElementById('watch').innerHTML=w;

 var i='<table><tr><th>выбрать имя</th></tr><tr><td>';
 s.presets.forEach(function(p,k){
   i+='<button class="'+(k==s.identIdx?'on':'')+'" onclick="cmd(\'/api/identity?idx='+k+'\')">'+esc(p)+'</button>';
 });
 i+='</td></tr><tr><td><input id="cn" value="'+esc(s.custom)+'">'
   +'<button onclick="cmd(\'/api/identity?name=\'+encodeURIComponent(document.getElementById(\'cn\').value))">сохранить CUSTOM</button></td></tr></table>';
 document.getElementById('ident').innerHTML=i;

 var f='<table><tr><th>имя в эфир</th><th>режим</th></tr><tr><td>';
 s.funs.forEach(function(p,k){
   f+='<button class="'+(k==s.funIdx&&s.funRun?'on':'')+'" onclick="cmd(\'/api/fun?idx='+k+'\')">'+esc(p)+'</button>';
 });
 f+='</td><td>';
 for(var m=0;m<3;m++) f+='<button class="'+(m==s.funMode?'on':'')+'" onclick="cmd(\'/api/fun?mode='+m+'\')">'+s.funModes[m]+'</button>';
 f+='</td></tr><tr><td colspan="2"><span class="dim">реклама идёт только когда на OLED открыт экран BLE FUN</span> '
  +'<button onclick="cmd(\'/api/fun?run='+(s.funRun?0:1)+'\')">'+(s.funRun?'стоп':'пуск')+'</button></td></tr></table>';
 document.getElementById('fun').innerHTML=f;

 var st='<table>'
  +'<tr><td>пищалка</td><td><button class="'+(s.buzzer?'on':'')+'" onclick="cmd(\'/api/settings?buzzer='+(s.buzzer?0:1)+'\')">'+(s.buzzer?'ВКЛ':'ВЫКЛ')+'</button>'
  +' <span class="dim">'+s.buzzerHw+'</span>'
  +' <button onclick="cmd(\'/api/beep\')">тест</button></td></tr>'
  +'<tr><td>порог «сильный сигнал»</td><td>'+s.strong+' dBm <button onclick="cmd(\'/api/settings?strong='+(s.strong-5)+'\')">-5</button>'
  +'<button onclick="cmd(\'/api/settings?strong='+(s.strong+5)+'\')">+5</button></td></tr>'
  +'<tr><td>интервал SLEEP WATCH</td><td>'+s.sleep+' c <button onclick="cmd(\'/api/settings?sleep='+(s.sleep-15)+'\')">-15</button>'
  +'<button onclick="cmd(\'/api/settings?sleep='+(s.sleep+15)+'\')">+15</button></td></tr>'
  +'<tr><td>яркость OLED</td><td>'+s.bright+' <button onclick="cmd(\'/api/settings?bright='+(s.bright-40)+'\')">-40</button>'
  +'<button onclick="cmd(\'/api/settings?bright='+(s.bright+40)+'\')">+40</button></td></tr>'
  +'<tr><td>точка доступа</td><td>'+esc(s.ap)+' <button class="'+(s.apOpen?'on':'')+'" onclick="cmd(\'/api/settings?apOpen='+(s.apOpen?0:1)+'\')">'
  +(s.apOpen?'открытая':'пароль '+s.apPass)+'</button></td></tr></table>';
 document.getElementById('set').innerHTML=st;
}
refresh();setInterval(refresh,2000);
</script></body></html>)HTML";

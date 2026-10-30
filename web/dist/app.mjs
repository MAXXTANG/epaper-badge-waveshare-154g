import {SIZE,layout,quantize,toneChannel,rgbaToRgb,rgbToRgba} from './image.mjs?v=3.0';
const $=id=>document.getElementById(id);
const source=document.createElement('canvas');source.width=source.height=SIZE;
const ctx=source.getContext('2d',{willReadFrequently:true}),preview=$('preview').getContext('2d');
let image=null,url=null,mode='fit',rotation=0,zoom=1,panX=0,panY=0,rgbFrame=null;
let busy=false,online=false,polling=false,loadingSource=false,hasSource=false,toneDirty=false,brightness=0;
let imageTag=0,deviceComposition=null,loadedSourceId=null,isRemote=false,lastDevice=null;
const say=(text,error=false)=>{$('status').textContent=text;document.querySelector('.status').classList.toggle('error',error);};
const composition=()=>[imageTag,mode,rotation,zoom,panX,panY].join('|');
const toneLabel=n=>n===0?'標準':(n>0?'+':'')+n;
function buttons(){
  $('send').disabled=!rgbFrame||busy||!online;$('send').firstChild.textContent=busy?'電子紙處理中… ':'傳送到電子紙 ';
  ['file','fit','fill','zoom','rotate','reset','fourColor','dither','brightness','darker','normal','lighter'].forEach(id=>{$(id).disabled=busy;});
  $('applyBrightness').disabled=busy||!online||!hasSource||composition()!==deviceComposition;
}
function render(){
  $('brightnessValue').textContent=toneLabel(brightness);
  if(!image){buttons();return;}
  const w=image.naturalWidth||image.width,h=image.naturalHeight||image.height;
  const t=layout(w,h,mode,zoom,rotation,panX,panY);panX=t.panX;panY=t.panY;
  ctx.setTransform(1,0,0,1,0,0);ctx.fillStyle='#fff';ctx.fillRect(0,0,SIZE,SIZE);
  ctx.save();ctx.translate(SIZE/2+panX,SIZE/2+panY);ctx.rotate(rotation*Math.PI/180);ctx.scale(t.scale,t.scale);ctx.drawImage(image,-w/2,-h/2);ctx.restore();
  const rgba=ctx.getImageData(0,0,SIZE,SIZE);rgbFrame=rgbaToRgb(rgba.data);
  const out=quantize(rgba.data,$('dither').checked,brightness);
  if($('fourColor').checked)preview.putImageData(new ImageData(out.preview,SIZE,SIZE),0,0);
  else{for(let i=0;i<rgba.data.length;i++)if(i%4!==3)rgba.data[i]=toneChannel(rgba.data[i],brightness);preview.putImageData(rgba,0,0);}
  $('zoomValue').textContent=Math.round(zoom*100)+'%';buttons();
}
function resetPosition(){zoom=1;panX=panY=0;$('zoom').value=100;render();}
function setMode(value){mode=value;['fit','fill'].forEach(id=>{$(id).classList.toggle('selected',id===mode);$(id).setAttribute('aria-pressed',String(id===mode));});$('modeHint').textContent=mode==='fit'?'保留整張照片；非正方形照片會留白。':'填滿螢幕，超出邊緣的部分會被裁切。';resetPosition();}
function setBrightness(value,dirty=true){brightness=Math.max(-40,Math.min(40,value));$('brightness').value=brightness;if(dirty)toneDirty=true;render();}
$('fit').onclick=()=>setMode('fit');$('fill').onclick=()=>setMode('fill');
$('zoom').oninput=()=>{zoom=Number($('zoom').value)/100;render();};
$('reset').onclick=()=>{rotation=0;setMode('fit');};$('rotate').onclick=()=>{rotation=(rotation+270)%360;resetPosition();};
$('fourColor').onchange=render;$('dither').onchange=()=>{toneDirty=true;render();};
$('brightness').oninput=()=>setBrightness(Number($('brightness').value));
$('darker').onclick=()=>setBrightness(brightness-20);$('normal').onclick=()=>setBrightness(0);$('lighter').onclick=()=>setBrightness(brightness+20);
$('file').onchange=async()=>{
  const file=$('file').files[0];if(!file)return;const previous=url;url=URL.createObjectURL(file);const next=new Image();
  try{next.src=url;await next.decode();image=next;imageTag++;isRemote=false;rotation=0;setMode(mode);$('empty').hidden=true;$('filename').textContent=file.name+' · '+image.naturalWidth+' × '+image.naturalHeight;say('可調整裁切與明暗，確認預覽後按傳送。');}
  catch{say('無法讀取這張照片，請改選 JPEG 或 PNG 圖片。',true);}finally{if(previous)URL.revokeObjectURL(previous);}
};
let drag=null;
$('preview').onpointerdown=e=>{if(!image||busy)return;$('preview').setPointerCapture(e.pointerId);drag={x:e.clientX,y:e.clientY,panX,panY};};
$('preview').onpointermove=e=>{if(!drag)return;const scale=SIZE/$('preview').getBoundingClientRect().width;panX=drag.panX+(e.clientX-drag.x)*scale;panY=drag.panY+(e.clientY-drag.y)*scale;render();};
$('preview').onpointerup=$('preview').onpointercancel=()=>{drag=null;};
async function status(){const response=await fetch('/api/status',{cache:'no-store',signal:AbortSignal.timeout(6000)});if(!response.ok)throw new Error('裝置暫時無法回覆');return response.json();}
function deviceStatus(s){
  lastDevice=s;hasSource=Boolean(s.has_source);online=true;$('device').textContent=s.busy||s.pending?'刷新中':'電子紙已連線';$('device').classList.add('online');
  $('deviceTone').textContent=hasSource?'目前電子紙：'+toneLabel(s.applied_brightness??0)+(s.pending?'，已排程 '+toneLabel(s.brightness):'')+(s.source_saved===false&&!s.busy?'（本次原圖尚未保存，請保持供電）':''):'請先重新傳一張圖片，以啟用機身明暗調整。';
  if(!toneDirty){$('dither').checked=s.dither!==false;setBrightness(s.brightness??0,false);}
}
async function loadDeviceSource(s){
  if(loadingSource||!s.has_source||s.busy||s.source_id===loadedSourceId)return;
  if(image&&!isRemote&&composition()!==deviceComposition){deviceComposition=null;return;}loadingSource=true;const initialTag=imageTag;
  try{
    const response=await fetch('/api/source',{cache:'no-store',signal:AbortSignal.timeout(10000)});if(!response.ok)return;
    const rgb=new Uint8Array(await response.arrayBuffer());if(polling||imageTag!==initialTag)return;
    const canvas=document.createElement('canvas');canvas.width=canvas.height=SIZE;
    canvas.getContext('2d').putImageData(new ImageData(rgbToRgba(rgb),SIZE,SIZE),0,0);image=canvas;imageTag++;isRemote=true;rotation=0;setMode('fit');
    $('empty').hidden=true;$('filename').textContent='目前電子紙圖片 · 200 × 200';loadedSourceId=s.source_id;deviceComposition=composition();say('已載入目前圖片，可調整明暗後套用。');buttons();
  }catch{say('目前圖片暫時無法載入，請稍後重新整理。',true);}finally{loadingSource=false;}
}
async function connect(){
  if(polling)return;
  try{const s=await status();if(polling)return;deviceStatus(s);busy=Boolean(s.busy||s.pending);await loadDeviceSource(s);if(polling)return;if(s.busy||s.pending)say('電子紙正在調整或刷新，完成後即可繼續操作。');}
  catch{online=false;$('device').textContent='未連上電子紙';$('device').classList.remove('online');if(!image)say('請連上 Paper154G Wi-Fi，再用 Safari 開啟 http://192.168.4.1。');}
  buttons();
}
async function waitForJob(job){
  const started=Date.now();let failures=0;
  while(Date.now()-started<100000){
    await new Promise(resolve=>setTimeout(resolve,1200));let s;
    try{s=await status();failures=0;}catch{if(++failures<5)continue;throw new Error('連線中斷，請確認仍連著 Paper154G Wi-Fi。');}
    if(s.job<job)throw new Error('裝置已重新啟動，請重新傳送。');
    if(!s.busy&&!s.pending){if(!s.ok)throw new Error(s.error||'電子紙刷新失敗，請重試。');return s;}
  }
  throw new Error('刷新尚未完成，請確認供電，稍候重新整理查看。');
}
async function submit(endpoint,body,successText){
  busy=true;polling=true;buttons();$('progress').hidden=false;$('progress').removeAttribute('value');say('正在傳送…');
  try{
    const response=await fetch(endpoint,{method:'POST',body,signal:AbortSignal.timeout(20000)});const result=await response.json();if(!response.ok)throw new Error(result.error||'傳送失敗');
    say('已收到，正在套用明暗並刷新，約需 20 秒…');const s=await waitForJob(result.job);
    toneDirty=false;deviceStatus(s);loadedSourceId=s.source_id;deviceComposition=composition();
    say(s.source_saved===false?'畫面已更新；原圖尚未保存，請保持供電並重新傳送。':successText);
  }catch(error){const network=['TypeError','SyntaxError','TimeoutError','AbortError'].includes(error.name);say(network?'連線暫時中斷，請確認 Wi-Fi 後重試。':error.message,true);}
  finally{busy=false;polling=false;$('progress').hidden=true;buttons();connect();}
}
$('send').onclick=async()=>{
  if(!rgbFrame||busy)return;
  const data=new FormData();data.append('brightness',String(brightness));data.append('dither',$('dither').checked?'1':'0');
  data.append('frame',new Blob([new Uint8Array(rgbFrame)],{type:'application/octet-stream'}),'source.rgb');
  await submit('/api/frame',data,'完成！可用 BOOT 調亮、PWR 調暗。');
};
$('applyBrightness').onclick=async()=>{
  if(busy||!hasSource)return;
  await submit('/api/brightness',new URLSearchParams({value:String(brightness),dither:$('dither').checked?'1':'0'}),'明暗已更新，可繼續用網頁或機身按鈕調整。');
};
connect();setInterval(()=>{if(!polling)connect();},3000);

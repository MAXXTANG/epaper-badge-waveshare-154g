import {SIZE,COLORS,layout,quantize} from './image.mjs';
const $=id=>document.getElementById(id);
const source=document.createElement('canvas');source.width=source.height=SIZE;
const ctx=source.getContext('2d',{willReadFrequently:true});
const preview=$('preview').getContext('2d');
let image=null,url=null,mode='fit',rotation=0,zoom=1,panX=0,panY=0,packed=null,busy=false,online=false,polling=false,job=null;
const say=(text,error=false)=>{$('status').textContent=text;document.querySelector('.status').classList.toggle('error',error);};
function buttons(){ $('send').disabled=!packed||busy||!online; $('send').firstChild.textContent=busy?'電子紙處理中… ':'傳送到電子紙 '; ['file','fit','fill','zoom','rotate','reset','fourColor','dither'].forEach(id=>{$(id).disabled=busy;}); }
function render(){
  if(!image)return;
  const t=layout(image.naturalWidth,image.naturalHeight,mode,zoom,rotation,panX,panY);panX=t.panX;panY=t.panY;
  ctx.setTransform(1,0,0,1,0,0);ctx.fillStyle='#fff';ctx.fillRect(0,0,SIZE,SIZE);
  ctx.save();ctx.translate(SIZE/2+panX,SIZE/2+panY);ctx.rotate(rotation*Math.PI/180);ctx.scale(t.scale,t.scale);
  ctx.drawImage(image,-image.naturalWidth/2,-image.naturalHeight/2);ctx.restore();
  const rgba=ctx.getImageData(0,0,SIZE,SIZE),out=quantize(rgba.data,$('dither').checked);packed=out.packed;
  preview.putImageData($('fourColor').checked?new ImageData(out.preview,SIZE,SIZE):rgba,0,0);
  $('zoomValue').textContent=Math.round(zoom*100)+'%';buttons();
}
function reset(){zoom=1;panX=panY=0;$('zoom').value=100;render();}
function setMode(value){mode=value;['fit','fill'].forEach(id=>{$(id).classList.toggle('selected',id===mode);$(id).setAttribute('aria-pressed',String(id===mode));});$('modeHint').textContent=mode==='fit'?'保留整張照片；非正方形照片會留白。':'填滿螢幕，超出邊緣的部分會被裁切。';reset();}
$('fit').onclick=()=>setMode('fit');$('fill').onclick=()=>setMode('fill');
$('zoom').oninput=()=>{zoom=Number($('zoom').value)/100;render();};
$('reset').onclick=()=>{rotation=0;setMode('fit');};
$('rotate').onclick=()=>{rotation=(rotation+270)%360;reset();};
$('fourColor').onchange=render;$('dither').onchange=render;
$('file').onchange=async()=>{
  const file=$('file').files[0];if(!file)return;
  const previous=url;url=URL.createObjectURL(file);
  const next=new Image();
  try{next.src=url;await next.decode();image=next;rotation=0;setMode(mode);$('empty').hidden=true;$('filename').textContent=file.name+' · '+image.naturalWidth+' × '+image.naturalHeight;say('拖曳預覽可調整位置，確認後按傳送。');}
  catch{say('無法讀取這張照片，請改選 JPEG 或 PNG 圖片。',true);}
  finally{if(previous)URL.revokeObjectURL(previous);}
};
let drag=null;
$('preview').onpointerdown=e=>{if(!image||busy)return;$('preview').setPointerCapture(e.pointerId);drag={x:e.clientX,y:e.clientY,panX,panY};};
$('preview').onpointermove=e=>{if(!drag)return;const scale=SIZE/$('preview').getBoundingClientRect().width;panX=drag.panX+(e.clientX-drag.x)*scale;panY=drag.panY+(e.clientY-drag.y)*scale;render();};
$('preview').onpointerup=$('preview').onpointercancel=()=>{drag=null;};
async function status(){
  const response=await fetch('/api/status',{cache:'no-store',signal:AbortSignal.timeout(6000)});
  if(!response.ok)throw new Error('裝置暫時無法回覆');
  return response.json();
}
async function connect(){
  try{const s=await status();online=true;$('device').textContent=s.busy?'刷新中':'電子紙已連線';$('device').classList.add('online');busy=s.busy;
    if(s.busy)say('電子紙正在處理，完成後即可傳送下一張。');
  }catch{online=false;$('device').textContent='未連上電子紙';$('device').classList.remove('online');if(!image)say('請連上 Paper154G Wi-Fi，再用 Safari 開啟 http://192.168.4.1。');}
  buttons();
}
$('send').onclick=async()=>{
  if(!packed||busy)return;busy=true;polling=true;buttons();$('progress').hidden=false;$('progress').removeAttribute('value');say('正在傳送圖片…');
  // Freeze the exact frame corresponding to the displayed preview.
  const frame=new Uint8Array(packed);const data=new FormData();data.append('frame',new Blob([frame],{type:'application/octet-stream'}),'frame.bin');
  try{
    const response=await fetch('/api/frame',{method:'POST',body:data,signal:AbortSignal.timeout(15000)});
    const result=await response.json();if(!response.ok)throw new Error(result.error||'圖片傳送失敗');job=result.job;
    say('圖片已收到，電子紙刷新中，約需 20 秒…');
    const started=Date.now();let failures=0;
    while(Date.now()-started<90000){
      await new Promise(resolve=>setTimeout(resolve,1200));
      let s;try{s=await status();failures=0;}catch{if(++failures<5)continue;throw new Error('連線中斷，請確認仍連著 Paper154G Wi-Fi，再重新整理查看。');}
      if(s.job!==job)throw new Error('裝置已重新啟動，請重新傳送圖片。');
      if(!s.busy){if(!s.ok)throw new Error(s.error||'電子紙未完成刷新，請稍候重試。');say('完成！圖片已顯示在電子紙上。');return;}
    }
    throw new Error('刷新尚未完成，請確認板子供電，稍候重新整理查看狀態。');
  }catch(error){say(error.message||'傳送失敗，請確認 Wi-Fi 連線後重試。',true);}
  finally{busy=false;polling=false;$('progress').hidden=true;buttons();connect();}
};
connect();setInterval(()=>{if(!polling)connect();},6000);

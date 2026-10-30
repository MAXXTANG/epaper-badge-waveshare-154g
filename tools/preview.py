"""Local UI QA server. API responses are simulated, never sent to the badge."""
from http.server import ThreadingHTTPServer,SimpleHTTPRequestHandler
from pathlib import Path
from email.parser import BytesParser
from email.policy import default
from urllib.parse import parse_qs
import json
root=Path(__file__).resolve().parents[1]/'web/dist'
state={'busy':False,'ok':True,'job':0,'status':'READY','version':'preview','has_source':False,'source_saved':False,'source_id':0,'brightness':0,'applied_brightness':0,'dither':True,'pending':False}
source=None
class Handler(SimpleHTTPRequestHandler):
 def __init__(self,*args,**kwargs):super().__init__(*args,directory=str(root),**kwargs)
 def reply(self,code,body,mime='application/json'):
  body=body if isinstance(body,bytes) else json.dumps(body).encode();self.send_response(code);self.send_header('Content-Type',mime);self.send_header('Content-Length',str(len(body)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(body)
 def do_GET(self):
  if self.path=='/api/status':self.reply(200,state)
  elif self.path=='/api/source':self.reply(200,source,'application/octet-stream') if source is not None else self.reply(404,{'error':'No source'})
  elif self.path in ('/','/index.html'):
   html=(root/'index.html').read_text().replace('<main>','<main><p style="padding:12px;background:#fff3cf;border-radius:8px;font-size:12px">本機介面預覽：此頁不會更新實體電子紙。實機操作請開啟 http://192.168.4.1。</p>',1)
   self.reply(200,html.encode(),'text/html; charset=utf-8')
  else:super().do_GET()
 def do_POST(self):
  global source
  body=self.rfile.read(int(self.headers.get('Content-Length',0)))
  if self.path=='/api/frame':
   message=BytesParser(policy=default).parsebytes(('Content-Type: '+self.headers.get('Content-Type','')+'\r\n\r\n').encode()+body)
   fields={part.get_param('name',header='content-disposition'):part.get_payload(decode=True) for part in message.iter_parts()}
   if len(fields.get('frame',b''))!=120000:self.reply(400,{'error':'Image must contain 120000 RGB bytes'});return
   source=fields['frame'];state.update(has_source=True,source_saved=True,source_id=state['source_id']+1,brightness=int(fields['brightness']),dither=fields.get('dither',b'1')!=b'0')
  elif self.path=='/api/brightness':
   fields=parse_qs(body.decode());value=int(fields.get('value',['100'])[0])
   if value not in [-40,-20,0,20,40] or source is None:self.reply(400,{'error':'Invalid brightness or missing source'});return
   state.update(brightness=value,dither=fields.get('dither',['1'])[0]!='0')
  else:self.reply(404,{'error':'No endpoint'});return
  state.update(job=state['job']+1,applied_brightness=state['brightness']);self.reply(202,{'job':state['job']})
print('Local UI preview: http://127.0.0.1:8787',flush=True)
ThreadingHTTPServer(('127.0.0.1',8787),Handler).serve_forever()

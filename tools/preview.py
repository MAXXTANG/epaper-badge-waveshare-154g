"""Local UI QA server with a simulated device API. Never embedded in the ESP32."""
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
import json
root=Path(__file__).resolve().parents[1]/'web/dist'
class Handler(SimpleHTTPRequestHandler):
    def __init__(self,*args,**kwargs): super().__init__(*args,directory=str(root),**kwargs)
    def do_GET(self):
        if self.path=='/api/status':
            body=json.dumps({'busy':False,'ok':True,'job':1,'status':'READY','version':'preview'}).encode()
            self.send_response(200);self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(body)
        else: super().do_GET()
    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length',0)))
        self.send_response(202);self.send_header('Content-Type','application/json');self.end_headers();self.wfile.write(b'{"job":1}')
print('Local UI preview: http://127.0.0.1:8787',flush=True)
ThreadingHTTPServer(('127.0.0.1',8787),Handler).serve_forever()

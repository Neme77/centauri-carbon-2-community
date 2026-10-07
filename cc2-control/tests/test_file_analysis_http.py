"""Large-file analysis must not monopolize the real HTTP event loop."""
import signal
import concurrent.futures, http.client, json, pathlib, socket, subprocess, sys, tempfile, time
binary=pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='cc2-analysis-http-') as tmp:
 root=pathlib.Path(tmp)
 with (root/'large.gcode').open('wb') as f:
  block=b'G1 X1 Y1 E0.1\n'*8192
  for _ in range(1050):f.write(block)
  f.write(b'; total layer number: 227\nT3\n')
 with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
 proc=subprocess.Popen([str(binary),'--port',str(port),'--panda-port','0','--gcode-internal',str(root),'--config',str(root/'config')],stdout=subprocess.DEVNULL,stderr=None)
 def request(path,body=None):
  conn=http.client.HTTPConnection('127.0.0.1',port,timeout=60)
  try:
   conn.request('POST' if body else 'GET',path,body,{'X-CC2-Request':'1'})
   r=conn.getresponse();data=r.read();assert r.status==200,(r.status,data);return json.loads(data)
  finally:conn.close()
 try:
  for _ in range(100):
   if proc.poll() is not None:raise AssertionError('backend exited')
   try:request('/api/health');break
   except OSError:time.sleep(.02)
  with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
   a=pool.submit(request,'/api/gcode-files/metadata','internal\nlarge.gcode')
   b=pool.submit(request,'/api/gcode-files/inspect','internal\nlarge.gcode')
   samples=0
   while not a.done() or not b.done():
    start=time.monotonic();request('/api/health');assert time.monotonic()-start<3
    request('/api/uds');samples+=1;time.sleep(.01)
   assert a.result()['layers']==227
   assert b.result()['tools']==[3]
   assert samples>0
  # A fully queued request survives a scheduling stall; incomplete ones do not.
  for complete in (True,False):
   client=socket.create_connection(('127.0.0.1',port));client.settimeout(5)
   time.sleep(.3) # let the server accept this socket before pausing it
   proc.send_signal(signal.SIGSTOP)
   try:
    client.sendall(b'GET /api/uds HTTP/1.1\r\nHost: 127.0.0.1\r\n'+(b'\r\n' if complete else b''))
    time.sleep(2.3)
   finally:proc.send_signal(signal.SIGCONT)
   try:result=client.recv(4096)
   except ConnectionResetError:result=b''
   client.close()
   if complete:assert b'200 OK' in result,result
   else:assert result==b'',result
  print('PASS: queued complete HTTP request survives scheduling stall; incomplete request still expires')
  print('PASS: actual HTTP health/UDS respond during concurrent 106+ MiB metadata/inspect requests')
 finally:
  proc.terminate();proc.wait(timeout=5)

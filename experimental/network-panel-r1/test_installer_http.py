import os,pathlib,subprocess,tempfile,time
root=pathlib.Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='cc2-http-bound-') as temp:
 p=pathlib.Path(temp);(p/'lock').mkdir()
 wget=p/'wget';wget.write_text('#!/bin/sh\ncase "$FIXTURE" in slow) exec sleep 30;; bad) exit 1;; *) printf \'{"service":"cc2-control"}\\n\';; esac\n');wget.chmod(0o700)
 script=root/'installer-http.sh'
 for mode,code in [('ok',0),('bad',1),('slow',124)]:
  env=dict(os.environ,PATH=str(p)+':'+os.environ['PATH'],FIXTURE=mode,LOCK=str(p/'lock'))
  start=time.monotonic()
  r=subprocess.run(['sh','-c','. "$1"; installer_http_get http://127.0.0.1:8081/api/health','fixture',str(script)],env=env,capture_output=True,timeout=8)
  elapsed=time.monotonic()-start
  assert r.returncode==code,(mode,r.returncode,r.stderr)
  assert elapsed<7,(mode,elapsed)
  assert not list((p/'lock').iterdir()),'temporary response left behind'
  if mode=='ok':assert r.stdout==b'{"service":"cc2-control"}\n'
  else:assert not r.stdout
print('PASS: actual shell bounded health reader: success, failure, hung wget, private-file cleanup')

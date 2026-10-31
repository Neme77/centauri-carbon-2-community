"""Execute generated startup with a constructor that rejects the wrong program.
Uses native fixture executables only, never any vendor binary or real service.
"""
from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile,sys
root=Path(__file__).resolve().parent
original=root.parents[1]/'experimental/firmware-port/launcher.sh'
raw=original.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='a260865964759898cf47ed8e30e20eb8981506c1a22ada32611b9911b36b47fc'
def run(args,**kw):return subprocess.run([str(x) for x in args],check=True,timeout=30,**kw)
with tempfile.TemporaryDirectory(prefix='cc2-startup-') as tmp:
 base=Path(tmp);runtime=base/'runtime';runtime.mkdir()
 guard=base/'guard.c';guard.write_text(r'''
#define _GNU_SOURCE
#include <unistd.h>
#include <string.h>
__attribute__((constructor)) static void qualify(void) {
 char path[1024];ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);
 if(n<0)_exit(125);path[n]=0;
 const char *name=strrchr(path,'/');name=name?name+1:path;
 if(strcmp(name,"elegoo_printer")) {
  static const char message[]="REACTOR FIXTURE STOP: unsupported executable path\n";
  write(2,message,sizeof(message)-1);_exit(125);
 }
}
''')
 run(['gcc','-fPIC','-shared',guard,'-o',runtime/'libcc2-reactor-v2.so'])
 empty=base/'empty.c';empty.write_text('int fixture_network_library;\n')
 run(['gcc','-fPIC','-shared',empty,'-o',base/'libcc2-network-compat-r1.so'])
 native=base/'native.c';native.write_text(r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
 const char *enabled=getenv("FIXTURE_ENABLED"),*network=getenv("CC2_NETWORK_COMPAT_TRIAL");
 if(!getenv("CC2_REACTOR_RUNTIME_V2")||!getenv("LD_PRELOAD"))return 4;
 if(enabled&&*enabled=='1'&&(!network||strcmp(network,"1")))return 9;
 if(enabled&&*enabled=='0'&&network)return 10;
 puts("NATIVE_FIXTURE_STARTED");return 0;
}
''')
 run(['gcc',native,'-o',runtime/'elegoo_printer'])
 helper=base/'helper.c';helper.write_text(r'''
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
 if(getenv("LD_PRELOAD"))return 78;
 if(argc!=2)return 2;
 FILE *log=fopen(getenv("FIXTURE_LOG"),"a");if(!log)return 3;
 fprintf(log,"%s\n",argv[1]);fclose(log);
 if(!strcmp(argv[1],"--ensure"))return 0;
 return strcmp(argv[1],"--enabled")||strcmp(getenv("FIXTURE_ENABLED"),"1");
}
''')
 run(['gcc',helper,'-o',base/'control'])
 (runtime/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in (runtime/'elegoo_printer',runtime/'libcc2-reactor-v2.so')))
 def bind(text):
  text=text.replace('BASE=/opt/usr/cc2-reactor-runtime-v2','BASE='+str(runtime))
  text=re.sub(r'(?m)^EXPECTED=[a-f0-9]{64}$','EXPECTED='+hashlib.sha256((runtime/'elegoo_printer').read_bytes()).hexdigest(),text)
  return text.replace('/opt/usr/cc2-network-manager/',str(base)+'/')
 env=dict(os.environ,FIXTURE_LOG=str(base/'calls'));env.pop('LD_PRELOAD',None);env.pop('CC2_NETWORK_COMPAT_TRIAL',None)
 # First reproduce the exact previously shipped transform against its source.
 old_hooks=Path(sys.argv[1])
 old=run(['awk','-v','mode=reactor-entry','-f',old_hooks,original],capture_output=True,text=True).stdout
 old_script=base/'old.sh';old_script.write_text(bind(old))
 env['FIXTURE_ENABLED']='1'
 failed=subprocess.run(['dash',old_script],env=env,capture_output=True,text=True,timeout=10)
 assert failed.returncode!=0 and 'unsupported executable path' in failed.stderr
 print('REPRODUCED: shipped transform loads reactor into the control helper',flush=True)
 cases=0
 for awk in (['awk'],['busybox','awk']):
  generated=run([*awk,'-v','mode=reactor-entry','-f',root/'prepare-hooks.awk',original],capture_output=True,text=True).stdout
  fixed=base/'fixed.sh';fixed.write_text(bind(generated))
  for shell in (['dash'],['busybox','sh']):
   for enabled in ('0','1'):
    env['FIXTURE_ENABLED']=enabled
    (base/'calls').write_text('')
    result=subprocess.run([*shell,fixed],env=env,capture_output=True,text=True,timeout=10)
    assert result.returncode==0,(awk,shell,enabled,result.stdout,result.stderr)
    assert result.stdout.strip()=='NATIVE_FIXTURE_STARTED' and not result.stderr,result
    assert (base/'calls').read_text().splitlines()==(['--enabled','--ensure'] if enabled=='1' else ['--enabled'])
    cases+=1
 print(f'PASS: {cases} actual startup paths, networking enabled/disabled, awk/BusyBox and dash/BusyBox; constructor guard preserved',flush=True)
 (Path(os.environ.get('CC2_TEST_RESULT_DIR', tempfile.gettempdir())) / 'reactor-startup-result.json').write_text(json.dumps({'legacy_failure_reproduced':True,'executed_startup_cases':cases,'guard_preserved':True,'physical_boot_validation':False},indent=2)+'\n')

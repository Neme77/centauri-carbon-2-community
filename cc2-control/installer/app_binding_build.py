"""Build a closed same-native-profile binding bundle, without installing it.

Requires an exact previous qualified runtime and a separately qualified new app.
No stock firmware image, key, serial operation or release publication is used.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from app_binding_bundle import read_runtime, plan_components, descriptor, EXECUTABLES, TRANSFER
from app_binding_component import functions

ROOT=Path(__file__).resolve().parents[2]
LIFE=ROOT/'experimental/canvas-rfid/lifecycle'
sys.path.insert(0,str(LIFE))
from publish_bundle import configuration, runtime

sha=lambda data:hashlib.sha256(data).hexdigest()
ARM=['-O2','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-D_POSIX_C_SOURCE=200809L',
     '-D_TIME_BITS=32','-D_FILE_OFFSET_BITS=32','-marm','-mcpu=cortex-a7','-mfpu=neon-vfpv4',
     '-mfloat-abi=hard','-static']


def compile_arm(source,target,defines,extra=()):
    command=['arm-linux-gnueabihf-gcc',*ARM,'-I'+str(LIFE.parent/'host'),*extra,
        *['-D'+k+'='+json.dumps(v) for k,v in defines.items()],source,'-lm','-o',target]
    subprocess.run(list(map(str,command)),check=True,timeout=180)
    dynamic=subprocess.check_output(['arm-linux-gnueabihf-readelf','-d',target],text=True,timeout=30)
    if 'NEEDED' in dynamic:raise ValueError('Binding helper must be self-contained')
    return target.read_bytes()


def build(previous,new_app,output,*,native_restore=False,preserve_lifecycle=False):
    old,modes,old_metadata=read_runtime(previous)
    app_bytes=new_app.read_bytes();new_sha=sha(app_bytes)
    if len(app_bytes)<20 or app_bytes[:6]!=b'\x7fELF\x01\x01' or app_bytes[18:20]!=b'\x28\x00':
        raise ValueError('New application must be a qualified ARM32 ELF')
    # Closed source-qualified predecessors. The active upgrade below was also
    # accepted on the printer; an arbitrary same-version runtime is not admitted.
    allowed={
        '8bfe407fe0ac61dd30f6485c4a45b408882807453ce46f92926845047196b4b3':
            '15537ed3765f76cef1d02f41d9cf099c41bbe177b670a17b1c523847878cf07b',
        '5262efc92724ff2b1cba5d9c1e0b5c8daecee9e2b35b50ae258cebb3dac5d054':
            '0f2be5fa958ec81b73d70ff5238a99a108bcb35a3a418b5a16c0ff07db77847b',
        '85af6b14faccdc2e3dfc2f679339c8ef4db5ce82d2c42a4591cdf10fc8081c96':
            'efa503d59478be6f15386eb58698dc1bd7555ce7122643ea7b894484254a6c06',
    }
    staged_predecessor = '08718f176e4942a788130db566498862edb506ac441c36baaaeef1e6b94b4c04'
    resume_staged = native_restore and old_metadata['app_sha256'] == staged_predecessor
    if native_restore:
        allowed[staged_predecessor] = 'a62f67e80fd216914db13fa92514b8323e85a82c81f5a11501739ff80860afa9'
        allowed['437ee3417ff7908da569e8ab010ab3c316d06e7ab11fbe63ec59f7f99fc1ec78'] = (
            '270025e28b7a39d68166e35b325bc3b2de13db32b2d9e3a65288cb1db4639313')
    if (allowed.get(old_metadata['app_sha256'])!=sha(previous.read_bytes()) or
            old_metadata['profile_sha256']!='9efc21a14f3da203a0fc8b0cf710602ab4bbf02bef6a3517c827a52b89f67766'):
        raise ValueError('Previous runtime is not an admitted qualified predecessor')
    pins={'fresh-reader':'3dd6d8699169c800689e5ed2c047a2d3cc43386d3fdad59adc41b929111b045a',
          'transfer-worker':'dd4a3be0fcee2e5290e177091dbfda5b1f402b87bd9e00727e1687661610206a',
          'libcc2-rfid.so':'fe6210eebfde592e4b0d276d2097abb0cfa3d53264da7ee9d83d8f24d7597ce2'}
    if resume_staged:
        pins['transfer-worker']=sha(old['transfer-worker'])  # Entire predecessor archive already matched its exact pin.
    for name,digest in pins.items():
        if sha(old[name])!=digest:raise ValueError('Qualified native artifact changed: '+name)
    output.mkdir(mode=0o700,parents=True,exist_ok=False)
    defines={'SC_APP_SHA':new_sha,'SC_PROFILE_SHA':old_metadata['profile_sha256'],
        'SC_FRESH_PATH':'/opt/usr/cc2-rfid/fresh-reader','SC_FRESH_SHA':pins['fresh-reader'],
        'SC_STRICT_SHA':pins['libcc2-rfid.so'],'SC_TRANSFER_SHA':pins['transfer-worker']}
    baseline=['-DSC_STOCK_BOOTSTRAP=1','-DSC_WITH_CAVITY=1',
        '-DSC_CAVITY_BOOTSTRAP_OPTIONAL=1','-DSC_VERIFY_CAVITY_CHANNEL=1']
    new=dict(old)
    if native_restore:
        new[TRANSFER]=compile_arm(LIFE.parent/'transfer/canvas_worker.c',output/TRANSFER,{
            'CV_BASE':'/opt/usr/cc2-rfid',
            'CV_MODULE':'/opt/usr/cc2-rfid/libcc2-rfid.so',
            'CV_MODULE_SHA':pins['libcc2-rfid.so'],
            'CV_GUARD':'/opt/usr/cc2-rfid/fresh-reader',
            'CV_GUARD_SHA':pins['fresh-reader'],
        },['-DCV_COORDINATED=1','-DCV_NATIVE_RESTORE_GUARD=1'])
        defines['SC_TRANSFER_SHA']=sha(new[TRANSFER])
        baseline.append('-DSC_NATIVE_RESTORE_GUARD=1')
    new['coordinator']=compile_arm(LIFE/'coordinator.c',output/'coordinator',defines,baseline)
    new['capture']=compile_arm(LIFE.parent/'capture.c',output/'capture',{
        'CT_MODULE':'/opt/usr/cc2-rfid/libcc2-rfid.so','CT_MODULE_SHA':pins['libcc2-rfid.so'],
        'CT_GUARD':'/opt/usr/cc2-rfid/coordinator','CT_GUARD_SHA':sha(new['coordinator']),
        'CT_LOGROOT':'/opt/usr/cc2-rfid'},['-DCT_GUARD_NATIVE=1'])
    metadata={'protocol':1,'app_sha256':new_sha,'coordinator_sha256':sha(new['coordinator']),
        'capture_sha256':sha(new['capture']),'profile_sha256':old_metadata['profile_sha256']}
    new['app-integration.json']=(json.dumps(metadata,separators=(',',':'))+'\n').encode()
    new['activate-on-ready']=f'CC2-RFID-ACTIVATE-1\n{new_sha}\n{old_metadata["profile_sha256"]}\n'.encode()
    binding_flags = ['-DAU_BIND_TRANSFER_WORKER=1'] if native_restore else []
    if preserve_lifecycle:binding_flags.append('-DAU_PRESERVE_LIFECYCLE=1')
    if resume_staged:binding_flags.append('-DAU_RESUME_STAGED_HOST=1')
    return build_binding(old,modes,old_metadata,new,metadata,output,defines,
                         baseline,binding_flags,sha(previous.read_bytes()))


def build_binding(old,modes,old_metadata,new,metadata,output,defines,baseline,binding_flags,previous_sha):
    new_sha=metadata['app_sha256']
    planned=plan_components(old,modes,old_metadata,new,modes,metadata)
    config=output/'binding-plan.h';config.write_bytes(planned['configuration'])
    worker_defines=dict(defines,SC_APP_SHA=old_metadata['app_sha256'],AU_WORKER_CONFIG=str(config),
        AU_DIRECTORY='/opt/usr/cc2-rfid-app-updates/'+new_sha)
    worker=compile_arm(LIFE/'app_binding_worker.c',output/'worker',worker_defines,
        [*baseline,*binding_flags,'-ffunction-sections','-fdata-sections','-Wl,--gc-sections'])
    files={'worker':worker,'recovery.json':descriptor(worker,planned)}
    files.update({'incoming/'+n:data for n,data in planned['incoming'].items()})
    permissions={n:0o700 if n == 'worker' or n.removeprefix('incoming/') in EXECUTABLES else 0o600 for n in files}
    # Only a complete exact old native inventory qualifies for publication.
    dependencies=[('/opt/usr/cc2-rfid/'+n,sha(data),modes[n]) for n,data in sorted(old.items())]
    dependencies.append(('/opt/usr/cc2-control/cc2-control',old_metadata['app_sha256'],0))
    publisher_config=output/'publisher-plan.h'
    publisher_config.write_bytes(configuration(files,permissions,dependencies,app_binding=True))
    publisher=compile_arm(LIFE/'publish_worker.c',output/'publisher',{
        'CP_PARENT':'/opt/usr/cc2-rfid-app-updates','CP_TARGET':new_sha,'CP_CONFIG':str(publisher_config)},
        ['-DCP_APP_BINDING=1'])
    bundle=output/'app-binding-runtime.tar.gz';bundle.write_bytes(runtime(files,permissions,publisher))
    functions(bundle,new_sha)  # Validate the exact installer-side contract.
    report={'protocol':1,'old_app_sha256':old_metadata['app_sha256'],'new_app_sha256':new_sha,
        'profile_sha256':old_metadata['profile_sha256'],'plan_sha256':planned['plan_sha256'],
        'bundle_sha256':sha(bundle.read_bytes()),'previous_runtime_sha256':previous_sha,
        'target_files':list(planned['incoming']),'native_files_unchanged':planned['native_component_unchanged'],'Canvas_write':False,
        'printer_accessed':False,'public_update':False,'installable_application_package_built':False}
    (output/'binding-build.json').write_text(json.dumps(report,indent=2)+'\n')
    print('BOUND ARTIFACTS BUILT: exact old profile preserved; pinned targets only. No application installer, printer access or Canvas write.',flush=True)
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previous-runtime',type=Path,required=True)
    parser.add_argument('--app',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    build(args.previous_runtime,args.app,args.output)

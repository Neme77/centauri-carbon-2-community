"""Stage a locally supplied exact runtime for the full firmware builder."""
from pathlib import Path
import argparse
import importlib.util
import json

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('connections_component', HERE/'core/connections_component.py')
component=importlib.util.module_from_spec(spec);spec.loader.exec_module(component)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,default=HERE/'components/connections/runtime.tar.gz')
    parser.add_argument('--verify-only',action='store_true')
    parser.add_argument('--output',type=Path,default=HERE/'components/connections/prepared')
    args=parser.parse_args()
    if args.verify_only:
        files=component.read_runtime(args.runtime)
        print('Connections input verified:',len(files),'files; kernel/modules unchanged.')
        return
    report=component.stage_input(args.runtime,args.output)
    (args.output.parent/'prepared-manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
    print('Qualified Connections input staged; existing kernel and modules retained byte-for-byte.')
    print('No firmware built and no printer accessed. Boot integration still requires qualification.')

if __name__=='__main__':main()

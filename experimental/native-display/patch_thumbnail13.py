"""Port of beanbo PR #411 to the reviewed .13 multilingual native GUI.
Original thumbnail implementation: beanbo, commit 883f96bd978d74dcb3a11a89ecc20262de57d5a2.
No installer or device access.
"""
import hashlib,importlib.util,json,struct
from pathlib import Path
HERE=Path(__file__).resolve().parent
INPUT_SHA256="9a31c54518a290d99340e02c89b2b21e103a0fb7085003cbd9498322f8de5799"
IMPORTS={
"free":0x12908,"fopen":0x12b84,"__isoc99_sscanf":0x12c74,
"fgets":0x12eb4,"memcpy":0x12ef0,"malloc":0x12f20,"strlen":0x12f2c,
"fclose":0x12f80,"strcmp":0x13004,"memcmp":0x13088,"strchr":0x13130,
"lv_obj_get_self_width":0xf6fcc,"lv_obj_get_self_height":0xf7000,
"lv_img_set_src":0x1211a8,"lv_img_set_pivot":0x122084,
"lv_img_set_zoom":0x1221e0,"lv_img_set_size_mode":0x122370,"lv_img_get_zoom":0x1223b8}
HOOKS={
0x4c938:(0xe92d47f0,0xea000000,"cc2_extract_thumbnail"),
0x50460:(0xeb034350,0xeb000000,"cc2_set_thumbnail_src"),
0x5051c:(0xeb034321,0xeb000000,"cc2_set_thumbnail_src"),
0x50680:(0xeb0346d6,0xeb000000,"cc2_set_thumbnail_zoom"),
0x50764:(0xeb03428f,0xeb000000,"cc2_set_thumbnail_src"),
0x50ad8:(0xeb0341b2,0xeb000000,"cc2_set_thumbnail_src"),
0x51d78:(0xeb033d0a,0xeb000000,"cc2_set_thumbnail_src")}
RX=(1,0,65536,65536,2187188,2187188,5,65536)
RW=(1,2234864,2300400,2300400,150332,17851072,6,65536)
def sha(data):return hashlib.sha256(data).hexdigest()
def load_patch():
 spec=importlib.util.spec_from_file_location("beanbo_thumbnail",HERE/"patch_thumbnail.py")
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.INPUT_SHA256=INPUT_SHA256;module.IMPORTS=IMPORTS;module.HOOKS=HOOKS
 module.RX=RX;module.RW=RW;module.PAYLOAD_ADDR=RX[2]+RX[4]
 return module
def verify_input(raw):
 if sha(raw)!=INPUT_SHA256:raise ValueError("Only the reviewed .13 multilingual GUI is supported")
 if struct.unpack_from("<8I",raw,148)!=RX or struct.unpack_from("<8I",raw,180)!=RW:
  raise ValueError("Unexpected .13 ELF layout")
 for site,(word,opcode,name) in HOOKS.items():
  if struct.unpack_from("<I",raw,site-65536)[0]!=word:raise ValueError("Changed thumbnail hook")
  if opcode==0xeb000000:
   delta=word&0xffffff
   if delta&0x800000:delta-=1<<24
   target=site+8+4*delta
   expected=IMPORTS["lv_img_set_zoom" if name.endswith("_zoom") else "lv_img_set_src"]
   if target!=expected:raise ValueError("Unexpected native call target")
def insert(raw,payload,entries):
 verify_input(raw)
 module=load_patch()
 if len(payload)>RW[1]-RX[4]:raise ValueError("File gap overflow")
 result=module.insert(raw,payload,entries)
 # Preserve all qualified language code, resources, headers and sections.
 allowed=set(range(164,172))|set(range(RX[4],RX[4]+len(payload)))
 for site in HOOKS:allowed.update(range(site-65536,site-65536+4))
 if len(result)!=len(raw) or any(x!=y and i not in allowed for i,(x,y) in enumerate(zip(raw,result))):
  raise ValueError("Change outside reviewed thumbnail areas")
 return result
def build_plan(raw,cross):
 verify_input(raw);module=load_patch()
 plan=module.build_plan(raw,cross)
 result=insert(raw,bytes.fromhex(plan["payload"]),plan["entries"])
 assert sha(result)==plan["output_sha256"]
 plan.update({"input_sha256":INPUT_SHA256,"upstream_author":"beanbo",
 "upstream_commit":"883f96bd978d74dcb3a11a89ecc20262de57d5a2",
 "native_version":"02.01.01.13","languages_preserved":True})
 return plan
def apply(raw,plan):
 verify_input(raw)
 if plan["input_sha256"]!=INPUT_SHA256 or sha((HERE/"thumbnail.c").read_bytes())!=plan["source_sha256"]:
  raise ValueError("Changed source or input")
 output=insert(raw,bytes.fromhex(plan["payload"]),plan["entries"])
 if sha(output)!=plan["output_sha256"]:raise ValueError("Changed output")
 return output


if __name__ == "__main__":
 import argparse
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument("gui",type=Path,help="Exact reviewed .13 multilingual GUI")
 parser.add_argument("output",type=Path,help="New output ELF, or plan JSON with --build-plan")
 parser.add_argument("--build-plan",action="store_true")
 parser.add_argument("--cross",default="arm-linux-gnueabihf-")
 args=parser.parse_args()
 raw=args.gui.read_bytes()
 if args.build_plan:
  data=(json.dumps(build_plan(raw,args.cross),indent=2)+"\n").encode("ascii")
 else:
  plan_bytes=(HERE/"thumbnail13.json").read_bytes()
  if sha(plan_bytes)!="dcd2f30ae9ff410d3f659d5ed123cd09bc83cbf2ba27e5d1e2d31cc740378e62":
   raise ValueError("Committed .13 plan identity changed")
  data=apply(raw,json.loads(plan_bytes))
 with args.output.open("xb") as target:target.write(data)
 print("VERIFIED .13 native thumbnail",sha(data))

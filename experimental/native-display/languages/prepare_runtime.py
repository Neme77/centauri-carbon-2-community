"""Prepare exact-build language prototype inputs. Not a firmware packager."""
import hashlib
import json
import struct
from native_model import Native, HERE, GUI_SHA
from literal_catalog import LABEL_ENTRIES, literal_rows, header as literal_header, load_catalogs

LIBRARY='/opt/inst/cc2-native-display/libcc2-native-display.so'

def cstr(value):
    return '"'+''.join(chr(b) if 32<=b<127 and b not in (34,92) else '\\%03o'%b for b in value.encode('utf-8'))+'"'

def inject_dependency(n):
    raw=n.data
    phoff,shoff=struct.unpack_from('<II',raw,28)
    phsize,phnum,shsize,shnum=struct.unpack_from('<HHHH',raw,42)
    if phsize!=32 or shsize!=40:raise ValueError('Unexpected ELF tables')
    headers=[struct.unpack_from('<8I',raw,phoff+i*32) for i in range(phnum)]
    notes=[i for i,p in enumerate(headers) if p[0]==4]
    dynamic=[i for i,p in enumerate(headers) if p[0]==2]
    if len(notes)!=1 or len(dynamic)!=1:raise ValueError('Unexpected native ELF layout')
    dyn=headers[dynamic[0]]
    entries=[]
    for offset in range(dyn[1],dyn[1]+dyn[4],8):
        tag,value=struct.unpack_from('<II',raw,offset)
        if not tag:break
        entries.append((tag,value))
    tags=dict(entries)
    strings=n.read(tags[5],tags[10])
    if LIBRARY.encode() in strings:raise ValueError('Already extended')
    base=0x1400000
    for seg in n.loads:
        if seg['p_vaddr']+seg['p_memsz']>base:raise ValueError('Extension address overlaps native map')
    start=(len(raw)+65535)&~65535
    added_strings=strings+LIBRARY.encode()+b'\0'
    dyn_offset=(len(added_strings)+7)&~7
    new_entries=[(tag,base if tag==5 else len(added_strings) if tag==10 else value) for tag,value in entries]
    new_entries += [(1,len(strings)),(0,0)]
    new_dynamic=b''.join(struct.pack('<II',*entry) for entry in new_entries)
    payload=added_strings+b'\0'*(dyn_offset-len(added_strings))+new_dynamic
    result=bytearray(raw+b'\0'*(start-len(raw))+payload)
    # The loader writes DT_DEBUG: metadata is RW, never executable.
    struct.pack_into('<8I',result,phoff+notes[0]*32,1,start,base,base,len(payload),len(payload),6,65536)
    struct.pack_into('<8I',result,phoff+dynamic[0]*32,2,start+dyn_offset,base+dyn_offset,base+dyn_offset,len(new_dynamic),len(new_dynamic),6,4)
    for i in range(shnum):
        off=shoff+i*shsize
        section=struct.unpack_from('<10I',raw,off)
        if section[1]==6:
            struct.pack_into('<III',result,off+12,base+dyn_offset,start+dyn_offset,len(new_dynamic))
        elif section[1]==3 and section[3]==tags[5]:
            struct.pack_into('<III',result,off+12,base,start,len(added_strings))
    return bytes(result)

def main():
    n=Native()
    functions={s.name:s['st_value'] for s in n.elf.get_section_by_name('.symtab').iter_symbols() if s['st_info']['type']=='STT_FUNC' and s['st_value']}
    for name,(address,expected) in LABEL_ENTRIES.items():
        if functions.get(name)!=address or n.read(address,8)!=expected:
            raise ValueError('Unqualified label trampoline: '+name)
    literals=literal_rows(n,load_catalogs(HERE/'locales'))
    dest=HERE/'runtime-inputs';dest.mkdir(exist_ok=True)
    patched=inject_dependency(n)
    (dest/'ec-eeb001-gui.prototype').write_bytes(patched)
    names=[n.string(n.word(0x12c9fc+i*4)) for i in range(11)]+['Magyar','Čeština','Polski']
    functions={s.name:s['st_value'] for s in n.elf.get_section_by_name('.symtab').iter_symbols() if s['st_info']['type']=='STT_FUNC' and s['st_value']}
    required=['lv_obj_create','lv_btn_create','lv_label_create','lv_obj_del','lv_obj_set_pos','lv_obj_set_size',
              'lv_obj_set_style_bg_color','lv_obj_set_style_bg_opa','lv_obj_set_style_border_width','lv_obj_set_style_text_color',
              'lv_label_set_text','lv_obj_add_style','lv_obj_align','lv_event_get_user_data','lv_obj_add_event_cb',
              'lv_obj_set_style_pad_left','lv_obj_set_style_pad_right','lv_obj_set_style_pad_top','lv_obj_set_style_pad_bottom',
              'get_ui_image_path','create_btn_center_img',
              'lv_obj_add_flag','lv_obj_clear_flag',
              'data_set_language','daemon_request_set_language','setting_menu_update_text','ui_view_refresh_all_while_page_change','ui_view_jump']
    header=['/* Generated from pinned private inputs. Do not publish this extracted data. */']
    for name in required:header.append(f'#define ND_{name.upper()} 0x{functions[name]:x}u')
    header.append('#ifndef ND_API_ONLY')
    header.append('static const char *const nd_language_names[14]={'+','.join(map(cstr,names))+'};')
    header.append('static const char *const nd_new_text[3][648]={')
    for lang in ('hu','cs','pl'):
        catalog=json.loads((HERE/'locales'/f'{lang}.json').read_text(encoding='utf-8-sig'))
        header.append('{'+',\n'.join(cstr(catalog.get(n.text(1,i),n.text(1,i))) for i in range(648))+'},')
    header.append('};')
    header.append('#endif')
    (dest/'native_language_data.h').write_text('\n'.join(header)+'\n',encoding='ascii')
    (dest/'native_literal_data.h').write_text(literal_header(literals,cstr),encoding='ascii')
    hooks=[('nd_display_get_text',0x25300),('nd_display_language_name',0x25340),
           ('nd_display_set_native_language',0x252ec),('nd_display_language_menu',0x76ea4),
           ('nd_display_label_text',LABEL_ENTRIES['lv_label_set_text'][0]),
           ('nd_display_label_static',LABEL_ENTRIES['lv_label_set_text_static'][0])]
    record={'input_gui_sha256':GUI_SHA,'prototype_gui_sha256':hashlib.sha256(patched).hexdigest(),
            'library':LIBRARY,'status':'UNQUALIFIED_PROTOTYPE_DO_NOT_INSTALL',
            'hooks':[{'function':name,'address':address,'expected':n.read(address,8).hex()} for name,address in hooks],
            'network_implemented':False,'network_runtime_qualified':False}
    (dest/'prototype.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    contract=['/* Exact-image hook contract; private prototype, not installation approval. */',
              '#define ND_GUI_SHA "'+record['prototype_gui_sha256']+'"',
              '#define ND_GUI_SIZE '+str(len(patched)),
              '#define ND_HOOK_COUNT 6',
              'static const uintptr_t nd_hook_addresses[ND_HOOK_COUNT]={'+','.join(hex(address)+'u' for _,address in hooks)+'};',
              'static const unsigned char nd_hook_expected[ND_HOOK_COUNT][8]={']
    contract += ['{'+','.join(hex(b) for b in n.read(address,8))+'},' for _,address in hooks]
    contract.append('};')
    (dest/'native_hook_contract.h').write_text('\n'.join(contract)+'\n',encoding='ascii')
    print('Prepared private language runtime inputs. UNQUALIFIED: do not install or execute on printer.')

if __name__=='__main__':main()

#ifndef CC2_RFID_PREVIEW_H
#define CC2_RFID_PREVIEW_H
/* Anycubic field layout adapted from suchmememanyskill/OpenRFID (GPL-3.0),
 * src/tag/anycubic/processor.py, upstream blob
 * 4823abc89b4f0b5e7b82fe485e480cbf98857b7a (see the experiment README).
 * OpenRFID credits DnG-Crafts/ACE-RFID. No inferred spool weight is applied. */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
typedef struct {
    char uid[17],material[17],sku[17],color[7];
    unsigned nozzle_min,nozzle_max,bed_min,bed_max,diameter,length;
} rfid_preview;
static unsigned rfid_u16(const unsigned char *data,unsigned offset){return data[offset]|((unsigned)data[offset+1]<<8);}
static int rfid_text(char *out,const unsigned char *data,size_t n){
    size_t i=0;while(i<n&&data[i]){unsigned c=data[i];if(!isalnum(c)&&c!='-'&&c!='+'&&c!='_'&&c!=' ')return 0;out[i]=(char)c;i++;}
    out[i]=0;return i>0;
}
static int rfid_decode(const char *line,rfid_preview *out){
    char kind[16],uid[17],hex[361];int end=-1;
    int n=sscanf(line,"RFID_RESULT={\"kind\":\"%15[a-z_]\",\"uid_hex\":\"%16[0-9a-f]\",\"pages_hex\":\"%360[0-9a-f]\"}%n",kind,uid,hex,&end);
    if(n!=3||end<0||line[end]||strcmp(kind,"foreign")||strlen(uid)!=16||strlen(hex)!=360||strspn(uid,"0")==16)return 0;
    unsigned char data[180];
    for(unsigned i=0;i<180;i++){unsigned value;if(sscanf(hex+i*2,"%2x",&value)!=1)return 0;data[i]=(unsigned char)value;}
    if(memcmp(data+16,"\x7b\0\x65\0",4)||memcmp(data+40,"AC\0",3))return 0;
    rfid_preview result={0};memcpy(result.uid,uid,17);
    if(!rfid_text(result.material,data+60,16)||!rfid_text(result.sku,data+20,16))return 0;
    result.nozzle_min=rfid_u16(data,96);result.nozzle_max=rfid_u16(data,98);
    result.bed_min=rfid_u16(data,116);result.bed_max=rfid_u16(data,118);
    result.diameter=rfid_u16(data,120);result.length=rfid_u16(data,122);
    if(result.nozzle_min<120||result.nozzle_max>350||result.nozzle_min>=result.nozzle_max||
       result.bed_min>result.bed_max||result.bed_max>120||result.diameter!=175||data[80]!=255)return 0;
    snprintf(result.color,sizeof(result.color),"%02X%02X%02X",data[83],data[82],data[81]);
    *out=result;return 1;
}
#endif

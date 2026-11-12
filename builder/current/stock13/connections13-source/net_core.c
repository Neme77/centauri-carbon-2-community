#include "net_core.h"
#include <string.h>
int cc2_net_string_set(cc2_net_string *out,const char *value) {
    size_t length=strlen(value);
    if(length>=sizeof(out->storage))return 0;
    memset(out,0,sizeof(*out));
    out->data=out->storage;out->length=length;
    memcpy(out->storage,value,length+1);return 1;
}
int cc2_net_string_equal(const cc2_net_string *value,const char *literal) {
    size_t length=strlen(literal);
    return value&&value->data&&value->length==length&&!memcmp(value->data,literal,length);
}
int cc2_net_parse_mode(const char *data,size_t length) {
    if(length==5&&!memcmp(data,"wifi\n",5))return 1;
    if(length==9&&!memcmp(data,"ethernet\n",9))return 2;
    return 0;
}
const cc2_net_string *cc2_net_map_name(const cc2_net_string *input,
                                     cc2_net_string *temporary,int mode) {
    if(mode==2&&cc2_net_string_equal(input,"wlan0")) {
        if(!cc2_net_string_set(temporary,"cc2lan0"))return input;
        return temporary;
    }
    return input;
}
int cc2_net_entry_words_valid(const unsigned words[2]) {
    /* Only these exact PC-independent prologues may be copied into a trampoline. */
    return (words[0]==0xe92d4800u&&words[1]==0xe28db004u)||
           (words[0]==0xe92d4870u&&words[1]==0xe28db010u)||
           (words[0]==0xe92d4810u&&words[1]==0xe28db008u);
}

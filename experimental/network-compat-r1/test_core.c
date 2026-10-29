#include "net_core.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    cc2_net_string input,mapped;
    assert(cc2_net_string_set(&input,"wlan0"));
    assert(cc2_net_string_equal(&input,"wlan0"));
    assert(cc2_net_map_name(&input,&mapped,0)==&input);
    assert(cc2_net_map_name(&input,&mapped,1)==&input);
    assert(cc2_net_string_equal(cc2_net_map_name(&input,&mapped,2),"cc2lan0"));
    assert(cc2_net_string_equal(&input,"wlan0"));
    assert(cc2_net_string_set(&input,"other0"));
    assert(cc2_net_map_name(&input,&mapped,2)==&input);
    assert(cc2_net_string_set(&input,"255.255.255.255"));
    assert(input.length==15&&input.data==input.storage);
    assert(!cc2_net_string_set(&input,"1234567890123456"));
    assert(cc2_net_parse_mode("wifi\n",5)==1);
    assert(cc2_net_parse_mode("ethernet\n",9)==2);
    assert(!cc2_net_parse_mode("ethernet",8));
    assert(!cc2_net_parse_mode("auto\n",5));
    assert(!cc2_net_parse_mode("ethernet\nextra",14));
    const unsigned valid[]={0xe92d4810u,0xe28db008u};
    const unsigned invalid[]={0xe59ff000u,0xe28db008u};
    assert(cc2_net_entry_words_valid(valid));
    assert(!cc2_net_entry_words_valid(invalid));
    puts("PASS strict mode parsing, bounded SSO, scoped interface mapping and relocatable prologues");
    return 0;
}

#include "net_core.h"
#include <assert.h>
cc2_net_string *cc2_test_return(cc2_net_string *out,unsigned kind) {
    const char *value=kind?"255.255.255.255":"";
    assert(cc2_net_string_set(out,value));return out;
}
int cc2_test_inspect(const cc2_net_string *input,const char *expected) {
    return cc2_net_string_equal(input,expected);
}

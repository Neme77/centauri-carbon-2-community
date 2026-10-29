#ifndef CC2_NET_CORE_H
#define CC2_NET_CORE_H
#include <stddef.h>
/* GCC/libstdc++ C++11 SSO ABI, tested against real host and ARM std::string. */
typedef struct { char *data; size_t length; char storage[16]; } cc2_net_string;
int cc2_net_string_set(cc2_net_string *out, const char *value);
int cc2_net_string_equal(const cc2_net_string *value, const char *literal);
/* 0: preserve native behavior; 1: explicit Wi-Fi; 2: explicit Ethernet.
 * An eventual automatic selector publishes its selected real interface here;
 * signal/quality policy is deliberately outside these native ABI wrappers. */
int cc2_net_parse_mode(const char *data, size_t length);
const cc2_net_string *cc2_net_map_name(const cc2_net_string *input,
                                     cc2_net_string *temporary, int mode);
int cc2_net_entry_words_valid(const unsigned words[2]);
#endif

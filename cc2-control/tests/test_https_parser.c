#define mbedtls_net_connect fixture_net_connect
#define mbedtls_ssl_handshake fixture_handshake
#define mbedtls_ssl_get_verify_result fixture_verify_result
#define mbedtls_ssl_read fixture_read
#define mbedtls_ssl_write fixture_write
#define main unused_downloader_main
#include "../updater/download.c"
#undef main
#include <assert.h>

static const unsigned char *response;
static size_t remaining;
int fixture_net_connect(mbedtls_net_context *ctx,const char *host,const char *port,int proto) {
    (void)ctx;(void)host;(void)port;(void)proto;return 0;
}
int fixture_handshake(mbedtls_ssl_context *ctx) {(void)ctx;return 0;}
uint32_t fixture_verify_result(const mbedtls_ssl_context *ctx) {(void)ctx;return 0;}
int fixture_read(mbedtls_ssl_context *ctx,unsigned char *buffer,size_t size) {
    (void)ctx;if(size>remaining)size=remaining;
    memcpy(buffer,response,size);response+=size;remaining-=size;return (int)size;
}
int fixture_write(mbedtls_ssl_context *ctx,const unsigned char *buffer,size_t size) {
    (void)ctx;(void)buffer;return (int)size;
}
static int parse(const char *headers,const char *ca) {
    char redirect[4196];response=(const unsigned char *)headers;remaining=strlen(headers);
    return get("https://github.com/Neme77/centauri-carbon-2-community/releases/download/test/test.cc2update",ca,33555540,redirect);
}
int main(int argc,char **argv) {
    assert(argc==2);
    const char *start="HTTP/1.1 302 Found\r\nContent-Length: 0\r\nContent-Security-Policy: ";
    const char *end="\r\nLocation: https://release-assets.githubusercontent.com/test\r\n\r\n";
    char headers[20000];size_t prefix=strlen(start);
    for(size_t length=9217;length<=15500;length+=6283) {
        memcpy(headers,start,prefix);memset(headers+prefix,'a',length);strcpy(headers+prefix+length,end);
        assert(parse(headers,argv[1])==2);
    }
    memcpy(headers,start,prefix);memset(headers+prefix,'a',17000);strcpy(headers+prefix+17000,end);
    assert(parse(headers,argv[1])==0);
    assert(parse("HTTP/1.1 302 Found\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\nLocation: https://release-assets.githubusercontent.com/test\r\n\r\n",argv[1])==0);
    assert(parse("HTTP/1.1 302 Found\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n",argv[1])==0);
    assert(parse("HTTP/1.1 302 Found\r\nLocation: https://example.org/untrusted\r\n\r\n",argv[1])==0);
    assert(parse("HTTP/1.1 302 Found\n\n",argv[1])==0);
    puts("PASS: real response parser accepts bounded 9-15 KiB CSP headers; total budget, framing and redirect guards preserved");
    return 0;
}

/* Minimal SHA-256 file fingerprint. Tested against hashlib, including block and
 * padding boundaries. No vendor source, crypto library or key material. */
#include "sha256_file.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t rotate(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
static const uint32_t k[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static void block(uint32_t h[8],const unsigned char b[64]){
 uint32_t w[64];
 for(unsigned i=0;i<16;i++)w[i]=(uint32_t)b[i*4]<<24|(uint32_t)b[i*4+1]<<16|(uint32_t)b[i*4+2]<<8|b[i*4+3];
 for(unsigned i=16;i<64;i++)w[i]=w[i-16]+(rotate(w[i-15],7)^rotate(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(rotate(w[i-2],17)^rotate(w[i-2],19)^(w[i-2]>>10));
 uint32_t a=h[0],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7],bb=h[1];
 for(unsigned i=0;i<64;i++){
  uint32_t t1=z+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+k[i]+w[i];
  uint32_t t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&bb)^(a&c)^(bb&c));
  z=g;g=f;f=e;e=d+t1;d=c;c=bb;bb=a;a=t1+t2;
 }
 h[0]+=a;h[1]+=bb;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
}
int cc2_sha256_file(const char *path,char out[65]){
 FILE *f=fopen(path,"rb");if(!f)return -1;
 uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 unsigned char b[64];uint64_t size=0;size_t n;
 while((n=fread(b,1,64,f))==64){block(h,b);size+=64;}
 if(ferror(f)){fclose(f);return -1;}size+=n;
 if(fclose(f))return -1;
 b[n++]=0x80;
 if(n>56){memset(b+n,0,64-n);block(h,b);n=0;}
 memset(b+n,0,56-n);
 uint64_t bits=size*8;for(unsigned i=0;i<8;i++)b[63-i]=(unsigned char)(bits>>(8*i));
 block(h,b);
 static const char digits[]="0123456789abcdef";
 for(unsigned i=0;i<32;i++){unsigned v=(h[i/4]>>(24-8*(i%4)))&255;out[i*2]=digits[v>>4];out[i*2+1]=digits[v&15];}
 out[64]=0;return 0;
}

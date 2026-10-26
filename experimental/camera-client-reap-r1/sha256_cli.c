#include "sha256_file.h"
#include <stdio.h>
int main(int argc,char **argv){char value[65];if(argc!=2||cc2_sha256_file(argv[1],value))return 1;puts(value);return 0;}

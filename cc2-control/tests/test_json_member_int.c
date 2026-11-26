#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
#include <limits.h>

/* json_member_int reads a JSON integer member, wherever the service reads one (queue, plates, spools, history,
 * Canvas status). A number outside the range of int, or a sign without digits, is "not present" (returns 0 and
 * leaves the target alone), as a missing member is. */
static int read_member(const char *value,int *out){
    char object[1024];
    int n=snprintf(object,sizeof(object),"{\"k\":%s}",value);
    assert(n>0&&(size_t)n<sizeof(object));
    return json_member_int(object,object+n,"k",out);
}
static void expect_value(const char *value,int expected){
    int out=12345;
    if(!read_member(value,&out)||out!=expected){fprintf(stderr,"FAIL: \"%s\" must read %d (got %d)\n",value,expected,out);exit(1);}
}
static void expect_absent(const char *value){
    int out=12345;
    if(read_member(value,&out)||out!=12345){fprintf(stderr,"FAIL: \"%s\" must be absent (read %d)\n",value,out);exit(1);}
}
int main(void){
    expect_value("0",0);expect_value("7",7);expect_value("-5",-5);expect_value("007",7);
    expect_value("2147483647",INT_MAX);expect_value("-2147483648",INT_MIN);
    expect_value(" 7",7); /* json_member skips the blank after the colon */
    expect_value("12abc",12); /* as before: the digits are read, what follows is not this function's business */
    expect_value("1.9",1);
    expect_absent("2147483648");expect_absent("-2147483649");expect_absent("4294967296");expect_absent("4294967297");
    expect_absent("9223372036854775807");expect_absent("9223372036854775808");expect_absent("-9223372036854775809");
    char longer[600];memset(longer,'9',500);longer[500]=0;expect_absent(longer);
    longer[0]='-';expect_absent(longer);
    expect_absent("-");expect_absent("-x");expect_absent("");expect_absent(" ");expect_absent("null");expect_absent("\"5\"");
    int out=1;assert(!json_member_int("{}","{}"+2,"k",&out)&&out==1);
    puts("PASS: json_member_int range");
    return 0;
}

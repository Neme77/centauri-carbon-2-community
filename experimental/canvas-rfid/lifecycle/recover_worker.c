#define _GNU_SOURCE
#include <signal.h>
static volatile sig_atomic_t recovery_cancelled;
#define CV_CANCELLED recovery_cancelled
#include "../transfer/app_transfer.h"
#include "profile.h"
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <termios.h>

#ifndef CL_BASE
#define CL_BASE "/opt/usr/cc2-rfid"
#endif
#ifndef CL_TTY
#define CL_TTY "/dev/ttyS1"
#endif
static int stop(const char *reason) {fprintf(stderr,"STOP: %s\n",reason);return 1;}
static void interrupted(int signum) {(void)signum;recovery_cancelled=1;}
static int digits(const char *text) {
    if(!*text)return 0;
    for(;*text;text++)if(*text<'0' || *text>'9')return 0;
    return 1;
}
/* Read process metadata only. No native controller or second UART owner may
 * coexist with a boot recovery; no process is stopped by this program. */
static int exclusive_native_port(dev_t device) {
    DIR *proc=opendir("/proc");if(!proc)return 0;
    struct dirent *entry;int okay=1;char self[32];
    snprintf(self,sizeof(self),"%ld",(long)getpid());
    while(okay && (entry=readdir(proc))) {
        if(!digits(entry->d_name) || !strcmp(entry->d_name,self))continue;
        char path[320],target[2048];int n=snprintf(path,sizeof(path),"/proc/%s/exe",entry->d_name);
        if(n<1 || (size_t)n>=sizeof(path)){okay=0;break;}
        ssize_t got=readlink(path,target,sizeof(target)-1);
        if(got<0){if(errno==ENOENT || errno==ESRCH)continue;okay=0;break;}
        if((size_t)got>=sizeof(target)-1){okay=0;break;}
        target[got]=0;char *name=strrchr(target,'/');name=name?name+1:target;
        if(!strcmp(name,"elegoo_printer") || !strcmp(name,"elegoo_printer (deleted)")){okay=0;break;}
        n=snprintf(path,sizeof(path),"/proc/%s/fd",entry->d_name);
        if(n<1 || (size_t)n>=sizeof(path)){okay=0;break;}
        DIR *fds=opendir(path);
        if(!fds){if(errno==ENOENT || errno==ESRCH)continue;okay=0;break;}
        struct dirent *item;
        while((item=readdir(fds))) {
            if(!digits(item->d_name))continue;
            struct stat st;
            if(fstatat(dirfd(fds),item->d_name,&st,0)) {
                if(errno==ENOENT || errno==ESRCH)continue;
                okay=0;break;
            }
            if(S_ISCHR(st.st_mode) && st.st_rdev==device){okay=0;break;}
        }
        closedir(fds);
    }
    closedir(proc);return okay;
}
static int current_boot(char boot[37]) {
    int fd=open("/proc/sys/kernel/random/boot_id",O_RDONLY|O_CLOEXEC);
    if(fd<0)return 0;
    unsigned char data[37];int okay=cl_io(fd,data,sizeof(data),0);close(fd);
    if(!okay || data[36]!='\n')return 0;
    memcpy(boot,data,36);boot[36]=0;return cl_boot_id_valid(boot);
}
static int challenge(int fd) {
    unsigned char bytes[12];int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if(random<0)return 0;
    int okay=cl_io(random,bytes,sizeof(bytes),0);close(random);if(!okay)return 0;
    for(unsigned i=0;i<3;i++)if(!cv_ping(fd,bytes+4*i,1000))return 0;
    return 1;
}
static void progress(unsigned offset,void *unused) {
    (void)unused;printf("STOCK_RECOVERY_ACK=%u/%u\n",offset,AP_SIZE);fflush(stdout);
}
int main(int argc,char **argv) {
    if(geteuid()!=0)return stop("root_required");
    if(argc!=2 || (strcmp(argv[1],"--check") && strcmp(argv[1],"--recover-original") && strcmp(argv[1],"--boot-policy")))return stop("unsupported_recovery_action");
    int check=!strcmp(argv[1],"--check");umask(077);
    signal(SIGINT,interrupted);signal(SIGTERM,interrupted);signal(SIGHUP,interrupted);signal(SIGPIPE,SIG_IGN);
    struct cl_store store;
    if(!cl_store_open(&store,CL_BASE))return stop("private_recovery_directory_or_lock_unavailable");
    struct statfs fs;
    if(fstatfs(store.directory,&fs) || (unsigned long)fs.f_type==0x01021994ul || (unsigned long)fs.f_type==0x858458f6ul)
        return stop("persistent_recovery_storage_required");
    struct cl_record record;
    if(!cl_store_read(&store,&record) || !cl_profile(&store,&record))return stop("durable_authorized_recovery_plan_required");
    unsigned char *stock=malloc(AP_SIZE);if(!stock)return stop("memory_unavailable");
    int backup=openat(store.directory,"original-stock-44.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    int valid=cl_private(backup,0,AP_SIZE) && cl_io(backup,stock,AP_SIZE,0) && ap_image(stock,AP_SIZE,0);
    if(backup>=0)close(backup);
    if(!valid)return stop("exact_private_original_backup_required");
    if(!strcmp(argv[1],"--boot-policy")) {
        enum cl_boot action=cl_boot_action(record.phase,1,1,1,1);
        free(stock);cl_store_close(&store);
        if(action==CL_BOOT_STOCK){puts("BOOT_POLICY=stock");return 0;}
        if(action==CL_BOOT_GUARDED){puts("BOOT_POLICY=guarded");return 10;}
        if(action==CL_BOOT_RECOVER_STOCK){puts("BOOT_POLICY=recover-original");return 20;}
        return stop("native_boot_blocked");
    }
    if(record.phase!=CL_FILTER_INTENT && record.phase!=CL_STOCK_INTENT)return stop("incomplete_transfer_intent_required");
    char boot[37];if(!current_boot(boot))return stop("boot_identity_unavailable");
    if(check) {
        free(stock);cl_store_close(&store);
        puts("BOOT RECOVERY CHECK PASS: authorized durable intent/profile and exact private stock backup. No UART opened or process stopped.");
        return 0;
    }
    struct stat before;
    if(lstat(CL_TTY,&before) || !S_ISCHR(before.st_mode) || !exclusive_native_port(before.st_rdev))
        return stop("native_or_serial_owner_present_no_UART_opened");
    int fd=open(CL_TTY,O_RDWR|O_NOCTTY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return stop("recovery_serial_unavailable");
    struct stat st;struct termios saved,raw;int exclusive=0,have_saved=0,attempted=0,acked=0,committed=0,jump=0,cleanup=1;
    if(fstat(fd,&st) || !S_ISCHR(st.st_mode) || st.st_rdev!=before.st_rdev || ioctl(fd,TIOCEXCL))goto done;
    exclusive=1;
    if(!exclusive_native_port(st.st_rdev) || tcgetattr(fd,&saved))goto done;
    have_saved=1;raw=saved;cfmakeraw(&raw);
    if(cfsetispeed(&raw,B115200) || cfsetospeed(&raw,B115200))goto done;
    raw.c_cflag|=CLOCAL|CREAD;raw.c_cflag&=~(CSTOPB|CRTSCTS);raw.c_cc[VMIN]=0;raw.c_cc[VTIME]=0;
    if(tcsetattr(fd,TCSANOW,&raw) || tcflush(fd,TCIFLUSH) || recovery_cancelled)goto done;
    /* After a power interruption the MCU may already be in its bootloader or
     * still have a complete app. The native request is non-writing in either
     * case; do not erase until three fresh challenge replies are received. */
    if(!challenge(fd)) {
        static const unsigned char magic[]=" \x1c Request Serial Bootloader!! ~";
        if(recovery_cancelled || !cv_write(fd,magic,sizeof(magic)-1,cv_milliseconds()+1000))goto done;
        struct timespec pause={0,50000000};nanosleep(&pause,NULL);
        if(!challenge(fd))goto done;
    }
    /* Intent was durable before this program was invoked. Only the exact stock
     * bytes are transferable here; there is no filter-install command. */
    acked=ap_transfer(fd,stock,0,cv_milliseconds()+60000,&recovery_cancelled,&attempted,progress,NULL);
    if(!acked)goto done;
    struct cl_facts facts={1,1,1,0,0,0,0,0,1,0,0};
    committed=cl_advance(&store,CL_ACK_STOCK,&facts,boot);
    if(!committed)goto done;
    unsigned char packet[AP_PACKET_MAX];size_t n=ap_packet(packet,2,NULL,0);
    jump=n && cv_write(fd,packet,n,cv_milliseconds()+1000);
done:
    if(have_saved && tcsetattr(fd,TCSANOW,&saved))cleanup=0;
    if(exclusive && ioctl(fd,TIOCNXCL))cleanup=0;
    close(fd);free(stock);cl_store_close(&store);
    if(!acked || !committed || !jump || !cleanup)
        return stop(attempted && !acked ? "stock_recovery_incomplete_native_boot_blocked" : "stock_recovery_unverified_native_boot_blocked");
    puts("STOCK APPLICATION TRANSFER ACKNOWLEDGED AND JOURNALED; application jump sent. Physical flash readback is unavailable.");
    puts("Guarded boot and later Canvas verification still required; no native process started and no automatic reboot.");
    return 0;
}

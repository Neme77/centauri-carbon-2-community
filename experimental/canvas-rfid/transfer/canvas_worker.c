#define _GNU_SOURCE
#include <signal.h>
static volatile sig_atomic_t cv_cancelled, cv_recovering;
#define CV_CANCELLED cv_cancelled
#include "app_transfer.h"
#include "stat_compat.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <termios.h>
/* The firmware toolchain predates the ARM EABI pidfd header definitions. */
#if defined(__arm__) && defined(__ARM_EABI__)
#ifndef SYS_pidfd_send_signal
#define SYS_pidfd_send_signal 424
#endif
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#endif
#if defined(CV_COORDINATED) && defined(CV_NATIVE_RESTORE_GUARD)
#define sc_now cv_milliseconds
#define sc_cancelled cv_cancelled
#include "../lifecycle/canvas_observation.h"
#undef sc_cancelled
#undef sc_now
#endif
#ifdef CV_COORDINATED
#include "../lifecycle/profile.h"
#endif

#ifndef CV_CHANNEL
#define CV_CHANNEL "/tmp/cc2-rfid-native-r2"
#endif
#ifndef CV_MODULE
#define CV_MODULE "/opt/usr/cc2-rfid-native-r2/libcc2-rfid-native-r2.so"
#endif
#ifndef CV_EXE_SHA
#define CV_EXE_SHA "19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745"
#endif
#ifndef CV_MODULE_SHA
#define CV_MODULE_SHA "3ab8811b4a7e7ca1d3e4f6f32d2708227fb9c6e09a82650e3d8d14ae85d98ea7"
#endif
#ifndef CV_GUARD
#define CV_GUARD "/opt/usr/cc2-rfid-capture-r1/cc2-rfid-capture-r1"
#endif
#ifndef CV_GUARD_SHA
#define CV_GUARD_SHA "ef355600449c0010a32f7d66b6496141a8e66cac68cbab842f83cb2fa3b48780"
#endif
#ifndef CV_TTY
#define CV_TTY "/dev/ttyS1"
#endif
#ifndef CV_BOOT_ID
#define CV_BOOT_ID "/proc/sys/kernel/random/boot_id"
#endif
struct cv_ready {char magic[8];uint64_t nonce,token,deadline;uint32_t pid,phase;};
_Static_assert(sizeof(struct cv_ready)==40,"RFID capability ABI");
static void interrupted(int signum) {(void)signum;if(!cv_recovering)cv_cancelled=1;}
static int fail(const char *reason) {fprintf(stderr,"STOP: %s\n",reason);return 1;}
static int digest_matches(const char *path,const char *expected) {
    char digest[65];int fd=open(path,O_RDONLY|O_CLOEXEC);
    if(fd<0)return 0;
    int valid=sha256_fd(fd,digest);close(fd);
    return valid && !strcmp(digest,expected);
}
static int load_ready(struct cv_ready *ready) {
    struct stat st;
    int dir=open(CV_CHANNEL,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(dir<0)return 0;
    int okay=!rf_fstat(dir,&st) && st.st_uid==0 && !(st.st_mode&077);
    int fd=okay?openat(dir,"ready",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK):-1;
    close(dir);if(fd<0)return 0;
    okay=!rf_fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_uid==0 && st.st_nlink==1 && !(st.st_mode&077) && st.st_size==(off_t)sizeof(*ready);
    unsigned char *p=(unsigned char*)ready;size_t count=0;
    while(okay && count<sizeof(*ready)) {
        ssize_t n=read(fd,p+count,sizeof(*ready)-count);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){okay=0;break;}count+=(size_t)n;
    }
    close(fd);
    return okay && count==sizeof(*ready) && !memcmp(ready->magic,"CC2RF1\0\0",8) && ready->nonce && ready->pid>1 && ready->phase<=2;
}
static int native_matches(const struct cv_ready *ready) {
    char path[96],line[2048];
    int n=snprintf(path,sizeof(path),"/proc/%u/exe",ready->pid);
    if(n<1 || (size_t)n>=sizeof(path) || !digest_matches(path,CV_EXE_SHA) || !digest_matches(CV_MODULE,CV_MODULE_SHA))return 0;
    n=snprintf(path,sizeof(path),"/proc/%u/maps",ready->pid);
    if(n<1 || (size_t)n>=sizeof(path))return 0;
    FILE *file=fopen(path,"r");if(!file)return 0;
    int mapped=0,bad=0;
    while(fgets(line,sizeof(line),file)) {
        if(strstr(line,CV_MODULE)){mapped=1;if(strstr(line,"(deleted)"))bad=1;}
    }
    if(ferror(file))bad=1;
    fclose(file);return mapped && !bad;
}
#if !defined(CV_COORDINATED) || !defined(CV_NATIVE_RESTORE_GUARD)
static int idle_guard(void) {
    if(!digest_matches(CV_GUARD,CV_GUARD_SHA))return 0;
    pid_t child=fork();if(child<0)return 0;
    if(!child){if(setsid()<0)_exit(126);execl(CV_GUARD,CV_GUARD,"--check",(char*)NULL);_exit(127);}
    uint64_t deadline=cv_milliseconds()+160000;
    int status=0;
    for(;;) {
        pid_t result=waitpid(child,&status,WNOHANG);
        if(result==child)return WIFEXITED(status) && WEXITSTATUS(status)==0;
        if(result<0 && errno!=EINTR)return 0;
        if(cv_cancelled || cv_milliseconds()>=deadline) {
            if(kill(-child,SIGKILL) && errno!=ESRCH)return 0;
            while(waitpid(child,&status,0)<0 && errno==EINTR){}
            return 0;
        }
        struct timespec pause={0,20000000};nanosleep(&pause,NULL);
    }
}
#endif
static int numeric(const char *text) {
    if(!*text)return 0;
    for(;*text;text++)if(*text<'0'||*text>'9')return 0;
    return 1;
}
/* Metadata inspection only; never open another process's serial descriptor. */
static int port_unowned(dev_t device) {
    DIR *proc=opendir("/proc");if(!proc)return 0;
    struct dirent *entry;int okay=1;
    char self[32];snprintf(self,sizeof(self),"%ld",(long)getpid());
    while(okay && (entry=readdir(proc))) {
        if(!numeric(entry->d_name) || !strcmp(entry->d_name,self))continue;
        char path[320];int n=snprintf(path,sizeof(path),"/proc/%s/fd",entry->d_name);
        if(n<1 || (size_t)n>=sizeof(path)){okay=0;break;}
        DIR *fds=opendir(path);
        if(!fds){if(errno==ENOENT||errno==ESRCH)continue;okay=0;break;}
        struct dirent *fd;
        while((fd=readdir(fds))) {
            if(!numeric(fd->d_name))continue;
            struct stat st;
            if(rf_fstatat(dirfd(fds),fd->d_name,&st,0)) {
                if(errno==ENOENT||errno==ESRCH)continue;
                okay=0;break;
            }
            if(S_ISCHR(st.st_mode) && st.st_rdev==device){okay=0;break;}
        }
        closedir(fds);
    }
    closedir(proc);return okay;
}

#include <sys/file.h>
#include <sys/vfs.h>
#ifndef CV_BASE
#define CV_BASE "/opt/usr/cc2-canvas-rfid-trial-r1a"
#endif
#ifndef CV_ORIGINAL
#define CV_ORIGINAL "/opt/inst/ota/canvas_gd32-20251029_184624-00.00.00.44-app.bin"
#endif
#define CV_TICKET_PREPARED 1u
#define CV_TICKET_STOPPED 2u
#define CV_TICKET_ERASE_INTENT 3u
#define CV_TICKET_PATCH_ACKED 4u
#define CV_TICKET_STOCK_ACKED 5u
#define CV_TICKET_FAILED 6u
struct cv_ticket {char magic[16],boot[40];uint64_t issued;uint32_t pid,stage;uint64_t nonce;};
_Static_assert(sizeof(struct cv_ticket)==80,"durable recovery ABI");

static int safe_file(int fd,size_t size,int private_file) {
    struct stat st;
    return fd>=0 && !rf_fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_uid==0 &&
           st.st_nlink==1 && !(st.st_mode&(private_file?077:022)) && st.st_size==(off_t)size;
}
/* The pinned ELEGOO squashfs source is root:root 0664. It is only read into
 * memory, and its complete SHA-256 must match before creating a private backup.
 * Do not apply this source-file rule to backups, tickets or locks. */
static int safe_original_source(int fd) {
    struct stat st;
    return fd>=0 && !rf_fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_uid==0 && st.st_gid==0 &&
           st.st_nlink==1 && !(st.st_mode&0002) && !(st.st_mode&0111) && st.st_size==(off_t)AP_SIZE;
}
static int read_full(int fd,void *data,size_t length) {
    size_t used=0;
    while(used<length) {
        ssize_t n=read(fd,(unsigned char*)data+used,length-used);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return 0;
        used+=(size_t)n;
    }
    return 1;
}
static int write_full(int fd,const void *data,size_t length) {
    size_t used=0;
    while(used<length) {
        ssize_t n=write(fd,(const unsigned char*)data+used,length-used);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return 0;
        used+=(size_t)n;
    }
    return 1;
}
static int boot_id(char out[40]) {
    int fd=open(CV_BOOT_ID,O_RDONLY|O_CLOEXEC);
    if(fd<0)return 0;
    char data[37];int valid=read_full(fd,data,sizeof(data));close(fd);
    if(!valid || data[36]!='\n')return 0;
    for(unsigned i=0;i<36;i++) {
        int dash=i==8 || i==13 || i==18 || i==23;
        if(dash?data[i]!='-':!((data[i]>='0' && data[i]<='9')||(data[i]>='a' && data[i]<='f')))return 0;
    }
    memset(out,0,40);memcpy(out,data,36);return 1;
}
static inline int save_ticket(int dir,struct cv_ticket *ticket,unsigned stage) {
    char name[64];int n=snprintf(name,sizeof(name),"ticket-%ld.next",(long)getpid());
    if(n<1 || (size_t)n>=sizeof(name))return 0;
    ticket->stage=stage;
    int fd=openat(dir,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return 0;
    int okay=write_full(fd,ticket,sizeof(*ticket)) && !fsync(fd);
    if(close(fd))okay=0;
    if(okay && renameat(dir,name,dir,"recovery-ticket"))okay=0;
    if(okay && fsync(dir))okay=0;
    if(!okay)unlinkat(dir,name,0);
    return okay;
}
static int record_ticket(int dir,struct cv_ticket *ticket,unsigned stage) {
#ifdef CV_COORDINATED
    /* The lifecycle journal is the sole durable authority in coordinated mode.
     * The old PID/boot-limited ticket is retained only by the legacy CLI. */
    (void)dir;ticket->stage=stage;return 1;
#else
    return save_ticket(dir,ticket,stage);
#endif
}
static int load_ticket(int dir,struct cv_ticket *ticket) {
    int fd=openat(dir,"recovery-ticket",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return 0;
    int okay=safe_file(fd,sizeof(*ticket),1) && read_full(fd,ticket,sizeof(*ticket));close(fd);
    char boot[40];uint64_t now=cv_milliseconds();
    return okay && boot_id(boot) && !memcmp(ticket->magic,"CC2-CANVAS-R1\0\0\0",16) &&
           !memcmp(boot,ticket->boot,40) && ticket->issued && now>=ticket->issued &&
           now-ticket->issued<=900000 && ticket->pid>1 && ticket->nonce &&
           ticket->stage>=CV_TICKET_STOPPED && ticket->stage<=CV_TICKET_FAILED;
}
static int read_image(int dir,unsigned char *stock,int recovery) {
    int fd;
    if(recovery)fd=openat(dir,"original-stock-44.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    else fd=open(CV_ORIGINAL,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return 0;
    int okay=(recovery?safe_file(fd,AP_SIZE,1):safe_original_source(fd)) &&
             read_full(fd,stock,AP_SIZE) && ap_image(stock,AP_SIZE,0);
    close(fd);if(!okay || recovery)return okay;
    fd=openat(dir,"original-stock-44.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd>=0) {
        unsigned char *copy=malloc(AP_SIZE);
        okay=copy && safe_file(fd,AP_SIZE,1) && read_full(fd,copy,AP_SIZE) && !memcmp(copy,stock,AP_SIZE) && !fsync(fd);
        free(copy);close(fd);return okay && !fsync(dir);
    }
    if(errno!=ENOENT)return 0;
    fd=openat(dir,"original-stock-44.bin",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return 0;
    okay=write_full(fd,stock,AP_SIZE) && !fsync(fd);
    if(close(fd))okay=0;
    return okay && !fsync(dir);
}
static int no_capture_active(void) {
    int dir=open(CV_CHANNEL,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(dir<0)return 0;
    struct stat st;int okay=1;
    const char *names[]={"scan-lock","request"};
    for(unsigned i=0;i<2;i++) {
        if(!rf_fstatat(dir,names[i],&st,AT_SYMLINK_NOFOLLOW) || errno!=ENOENT)okay=0;
    }
    close(dir);return okay;
}
/* Fail closed if another native controller exists or /proc cannot be inspected.
 * This is metadata only, not an attempt to open another process's UART. */
static int native_absent(void) {
    DIR *proc=opendir("/proc");if(!proc)return 0;
    struct dirent *entry;int absent=1;
    while((entry=readdir(proc))) {
        if(!numeric(entry->d_name))continue;
        char path[320],target[2048];
        int n=snprintf(path,sizeof(path),"/proc/%s/exe",entry->d_name);
        if(n<1 || (size_t)n>=sizeof(path)){absent=0;break;}
        ssize_t got=readlink(path,target,sizeof(target)-1);
        if(got<0){if(errno==ENOENT || errno==ESRCH)continue;absent=0;break;}
        if((size_t)got>=sizeof(target)-1){absent=0;break;}
        target[got]=0;char *name=strrchr(target,'/');name=name?name+1:target;
        if(!strcmp(name,"elegoo_printer") || !strcmp(name,"elegoo_printer (deleted)")){absent=0;break;}
    }
    closedir(proc);return absent;
}
static int challenges(int fd) {
    unsigned char bytes[12];int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if(random<0)return 0;
    int okay=read_full(random,bytes,sizeof(bytes));close(random);if(!okay)return 0;
    for(unsigned i=0;i<3;i++)if(!cv_ping(fd,bytes+i*4,1000))return 0;
    return 1;
}
static void progress(unsigned offset,void *unused) {
    (void)unused;printf("TRANSFER_ACK=%u/%u\n",offset,AP_SIZE);fflush(stdout);
}
#ifdef CV_COORDINATED
static struct cl_store cv_coord_store={-1,-1};
static int cv_coord_uncertain;
static int cv_coord_open(void) {
    struct cl_record record;
    return cl_store_open(&cv_coord_store,CV_BASE) && cl_store_read(&cv_coord_store,&record) &&
           cl_profile(&cv_coord_store,&record);
}
static int cv_coord_begin(int install,const char *boot) {
    /* The live native identity, owned idle capability, exact backup and fresh
     * cold guard have already been checked twice immediately before this call. */
    struct cl_facts facts={1,1,1,1,1,1,0,0,0,0,0};
    return cl_advance(&cv_coord_store,install?CL_BEGIN_FILTER:CL_BEGIN_STOCK,&facts,boot);
}
static int cv_coord_ack(int stock) {
    char boot[40];struct cl_facts facts={1,1,1,0,0,0,0,0,1,0,0};
    return boot_id(boot) && cl_advance(&cv_coord_store,stock?CL_ACK_STOCK:CL_ACK_FILTER,&facts,boot);
}
#endif
int main(int argc,char **argv) {
    if(geteuid()!=0)return fail("root_required");
    if(argc!=2)return fail("expected_check_install_filter_restore_original_or_recover_original");
    int check=!strcmp(argv[1],"--check"),install=!strcmp(argv[1],"--install-filter");
    int restore=!strcmp(argv[1],"--restore-original"),recover=!strcmp(argv[1],"--recover-original");
    if(!check && !install && !restore && !recover)return fail("unsupported_action");
#ifdef CV_COORDINATED
    if(recover)return fail("use_coordinated_stock_boot_recovery_worker");
#endif
    umask(077);signal(SIGINT,interrupted);signal(SIGTERM,interrupted);signal(SIGHUP,interrupted);signal(SIGPIPE,SIG_IGN);
    int base=open(CV_BASE,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(base<0)return fail("private_disk_directory_required");
    struct stat st;struct statfs fs;
    if(rf_fstat(base,&st) || st.st_uid!=0 || (st.st_mode&077) || fstatfs(base,&fs) ||
       (unsigned long)fs.f_type==0x01021994ul || (unsigned long)fs.f_type==0x858458f6ul){close(base);return fail("private_persistent_disk_required");}
    int lock=openat(base,"worker.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
    if(lock<0 || !safe_file(lock,0,1) || flock(lock,LOCK_EX|LOCK_NB))return fail("worker_lock_unavailable");
#ifdef CV_COORDINATED
    if(!cv_coord_open())return fail("qualified_coordinated_lifecycle_profile_required");
#endif
    unsigned char *stock=malloc(AP_SIZE),*patched=malloc(AP_SIZE);
    if(!stock || !patched || !read_image(base,stock,recover) || !ap_patch(stock,AP_SIZE,patched))return fail("original_image_or_durable_backup_invalid");
    struct cv_ticket ticket;memset(&ticket,0,sizeof(ticket));
    struct cv_ready ready,second;
    if(recover) {
        if(!load_ticket(base,&ticket) || !native_absent())return fail("same_boot_stopped_native_recovery_ticket_required");
    } else {
        if(!load_ready(&ready) || ready.phase!=1 || !native_matches(&ready) || !no_capture_active())return fail("qualified_idle_R2_no_capture_required");
#if defined(CV_COORDINATED) && defined(CV_NATIVE_RESTORE_GUARD)
        if(!vc_native_ready(!restore))return fail("native_transfer_readiness_failed");
#else
        if(!idle_guard())return fail("fresh_idle_cold_guard_failed");
#endif
        if(!load_ready(&second) || memcmp(&ready,&second,sizeof(ready)) || !native_matches(&ready) ||
           !no_capture_active() || cv_cancelled)return fail("native_changed_no_process_stopped");
        if(check) {
            puts("CANVAS CHECK PASS: exact original and durable backup, qualified idle R2, fresh cold guard. No process stopped or UART opened.");
            free(stock);free(patched);close(lock);close(base);return 0;
        }
        memcpy(ticket.magic,"CC2-CANVAS-R1\0\0\0",16);
        ticket.issued=cv_milliseconds();ticket.pid=ready.pid;ticket.nonce=ready.nonce;
        if(!boot_id(ticket.boot) || !ticket.issued || !record_ticket(base,&ticket,CV_TICKET_PREPARED))return fail("recovery_ticket_not_durable_no_process_stopped");
        int pidfd=(int)syscall(SYS_pidfd_open,(pid_t)ready.pid,0);
        if(pidfd<0)return fail("pidfd_unavailable_no_process_stopped");
        if(!load_ready(&second) || memcmp(&ready,&second,sizeof(ready)) || !native_matches(&ready) ||
           !no_capture_active() || cv_cancelled){close(pidfd);return fail("native_changed_no_process_stopped");}
#ifdef CV_COORDINATED
        if(syscall(SYS_pidfd_send_signal,pidfd,0,NULL,0)){
            close(pidfd);return fail("pidfd_signal_unavailable_no_process_stopped");
        }
        if(!cv_coord_begin(install,ticket.boot)){
            close(pidfd);return fail("lifecycle_transfer_intent_not_durable_no_process_stopped");
        }
#endif
        fflush(stdout);
        if(syscall(SYS_pidfd_send_signal,pidfd,SIGTERM,NULL,0)){close(pidfd);return fail("native_stop_failed");}
        int stopped=cv_wait(pidfd,POLLIN,cv_milliseconds()+15000);close(pidfd);
        if(!stopped || !native_absent())return fail("native_stop_unconfirmed_no_flash_attempted");
        if(!record_ticket(base,&ticket,CV_TICKET_STOPPED))return fail("stop_ticket_failed_no_flash_attempted_manual_reboot_required");
        puts("NATIVE_STOPPED: complete manual printer reboot required after this operation.");fflush(stdout);
    }
    int fd=open(CV_TTY,O_RDWR|O_NOCTTY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return fail("serial_open_failed_no_flash_attempted_manual_reboot_required");
    struct termios saved,raw;int exclusive=0,have_saved=0,recognized=0,attempted=0;
    int acknowledged=0,stock_restored=0,jump_sent=0,cleanup_ok=1;
    if(rf_fstat(fd,&st) || !S_ISCHR(st.st_mode) || ioctl(fd,TIOCEXCL))goto done;
    exclusive=1;
    if(!port_unowned(st.st_rdev) || tcgetattr(fd,&saved))goto done;
    have_saved=1;raw=saved;cfmakeraw(&raw);
    if(cfsetispeed(&raw,B115200) || cfsetospeed(&raw,B115200))goto done;
    raw.c_cflag|=CLOCAL|CREAD;raw.c_cflag&=~(CSTOPB|CRTSCTS);raw.c_cc[VMIN]=0;raw.c_cc[VTIME]=0;
    if(tcsetattr(fd,TCSANOW,&raw) || tcflush(fd,TCIFLUSH))goto done;
    if(!recover) {
        static const unsigned char magic[]=" \x1c Request Serial Bootloader!! ~";
        if(cv_cancelled || !cv_write(fd,magic,sizeof(magic)-1,cv_milliseconds()+1000))goto done;
        struct timespec pause={0,50000000};nanosleep(&pause,NULL);
    }
    if(!challenges(fd))goto done;
    recognized=1;
    if(cv_cancelled || !record_ticket(base,&ticket,CV_TICKET_ERASE_INTENT))goto done;
    acknowledged=ap_transfer(fd,install?patched:stock,install,cv_milliseconds()+60000,
                              &cv_cancelled,&attempted,progress,NULL);
    if(acknowledged) {
        stock_restored=!install;
        if(!record_ticket(base,&ticket,install?CV_TICKET_PATCH_ACKED:CV_TICKET_STOCK_ACKED)) {
            /* A modified image without durable completion state is not accepted. */
            if(install)acknowledged=0;
            else fprintf(stderr,"STOCK_TRANSFER_ACKED_BUT_COMPLETION_TICKET_WRITE_FAILED\n");
#ifdef CV_COORDINATED
            cv_coord_uncertain=1;acknowledged=0;
#endif
        }
#ifdef CV_COORDINATED
        if(acknowledged && !cv_coord_ack(!install)) {
            cv_coord_uncertain=1;acknowledged=0;
        }
#endif
    }
    if(!acknowledged && attempted
#ifdef CV_COORDINATED
       && !cv_coord_uncertain
#endif
    ) {
        puts("TRANSFER_FAILED: attempting original application restoration.");fflush(stdout);
        cv_recovering=1;cv_cancelled=0;
        /* Drain/resynchronize with fresh challenge replies before a new erase.
         * Do not enter the application while restoration is incomplete. */
        if(challenges(fd) && ap_transfer(fd,stock,0,cv_milliseconds()+60000,NULL,&attempted,progress,NULL)) {
            stock_restored=1;
            if(!record_ticket(base,&ticket,CV_TICKET_STOCK_ACKED))fprintf(stderr,"STOCK_TRANSFER_ACKED_BUT_COMPLETION_TICKET_WRITE_FAILED\n");
#ifdef CV_COORDINATED
            if(!cv_coord_ack(1))cv_coord_uncertain=1;
#endif
        } else {
            (void)record_ticket(base,&ticket,CV_TICKET_FAILED);
            fprintf(stderr,"RESTORE_INCOMPLETE: keep power and connection; retain backup/log/ticket. Do not assume an application is runnable.\n");
        }
    }
done:
    /* Before any erase, jumping is non-writing recovery. After any erase,
     * permit jump only after every expected program ACK has matched. */
    if(recognized && (!attempted || acknowledged || stock_restored)
#ifdef CV_COORDINATED
       && !cv_coord_uncertain
#endif
    ) {
        unsigned char packet[AP_PACKET_MAX];size_t n=ap_packet(packet,2,NULL,0);
        jump_sent=n && cv_write(fd,packet,n,cv_milliseconds()+1000);
    }
    if(have_saved && tcsetattr(fd,TCSANOW,&saved))cleanup_ok=0;
    if(exclusive && ioctl(fd,TIOCNXCL))cleanup_ok=0;
    close(fd);free(stock);free(patched);close(lock);close(base);
#ifdef CV_COORDINATED
    cl_store_close(&cv_coord_store);
    if(cv_coord_uncertain)return fail("lifecycle_completion_not_durable_native_boot_blocked");
#endif
    if(!acknowledged || !jump_sent || !cleanup_ok) {
        if(stock_restored)fprintf(stderr,"ORIGINAL_TRANSFER_ACKNOWLEDGED: original application sent; physical flash readback unavailable.\n");
        return fail(attempted && !stock_restored && !acknowledged?"application_incomplete_recovery_required_do_not_reboot":"trial_incomplete_keep_log_manual_reboot_required");
    }
    puts(install?"FILTER APPLICATION TRANSFER ACKNOWLEDGED; application jump sent.":"ORIGINAL APPLICATION TRANSFER ACKNOWLEDGED; application jump sent.");
    puts("No physical flash readback exists in this protocol. Reboot the complete idle printer manually and verify Canvas, LAN, EXHAUST and R2.");
    if(install)puts("PRIVATE TRIAL: restore original Canvas before disabling the R2 host guard or updating application/base firmware.");
    return 0;
}

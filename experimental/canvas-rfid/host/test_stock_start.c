/* Test harness only: run newly compiled fixtures with the extracted stock
 * glibc, using its original startup symbol. Never linked into the module. */
typedef int (*rf_main_fn)(int, char **, char **);
typedef void (*rf_void_fn)(void);
extern int rf_stock_start(rf_main_fn, int, char **, rf_void_fn, rf_void_fn, rf_void_fn, void *);
__asm__(".symver rf_stock_start,__libc_start_main@GLIBC_2.4");
int __wrap___libc_start_main(rf_main_fn main, int argc, char **argv,
                           rf_void_fn init, rf_void_fn fini, rf_void_fn loader_fini, void *stack) {
    return rf_stock_start(main, argc, argv, init, fini, loader_fini, stack);
}

#define RF_NATIVE_TEST
#include "native_module.c"
void test_setup(rf_native_fn parse, rf_native_fn state, rf_read_fn read,rf_reset_fn reset) {
    rf_original_parse = parse; rf_original_state = state; rf_original_read = read;
    rf_original_reset=reset;
    rf_channel = rf_directory(); rf_epoch = 100;
}
int test_phase(void) {return rf_phase;}
int test_lock_available(void) {
    if (pthread_mutex_trylock(&rf_lock)) return 0;
    pthread_mutex_unlock(&rf_lock); return 1;
}
void test_cleanup(void) {
    unlinkat(rf_channel,"ready",0); close(rf_channel); rmdir(RF_CHANNEL);
}

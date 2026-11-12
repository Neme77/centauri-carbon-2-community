#ifndef CC2_NETWORK_PROFILE_PINS_H
#define CC2_NETWORK_PROFILE_PINS_H
#define NG_CC2_UE300_ABI_PROBE_KO_SHA \
  "24c757788815abe5bbabb59d64b4768efedf7a65b1a8d93f3a6ce4d1e979a3f8"
#define NG_CC2_UE300_ABI_PROBE_3000_KO_SHA \
  "4027162b4a5a45fc136b932f4a33ee2a2f252270a0f7c29c6c03afc2d84e180a"
#define NG_CC2_UE300_MII_KO_SHA                                                \
  "9051751ccefa1c1795b644764fd4c2de31b54d871e863927ef17b6d65978a7be"
#define NG_CC2_UE300_R8152_KO_SHA                                              \
  "5118667293fe05753f38c31bafe654d110104c6d7239ba8c2cfa1d3151b83ebd"
#ifdef CC2_NATIVE_STOCK13
#ifndef NG_LIBCC2_NETWORK_COMPAT_R1_SO_SHA
#error "The .13 Connections build requires the qualified network shim hash"
#endif
#ifndef NG_PRINTER_INIT_SHA
#error "The .13 Connections build requires the combined printer startup hash"
#endif
#define NG_CAMERA_RECOVERY_SO                                                  \
  "/opt/inst/cc2-camera-recovery13/libcc2-camera-recovery-r2.so"
#else
#define NG_LIBCC2_NETWORK_COMPAT_R1_SO_SHA                                     \
  "bde512195dd6bf3846f3103b5e1acbd89a62e5ee819e405389c97f5fc188ff3a"
#define NG_PRINTER_INIT_SHA                                                    \
  "e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd255d1bc4"
#define NG_CAMERA_RECOVERY_SO                                                  \
  "/opt/usr/cc2-control/camera/libcc2-camera-recovery-r2.so"
#define NG_CAMERA_REAP_SO "/opt/usr/cc2-control/camera/libcc2-camera-reap-r2.so"
#endif
#define NG_PRINTER_CONFIG_SHA                                                  \
  "8fa0a87f203d69cb9c4e027e8db11a9a0d92c5d2ea8ae83f87b34714aae7ec4a"
#endif

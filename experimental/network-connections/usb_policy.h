#ifndef CC2_CONNECTIONS_USB_POLICY_H
#define CC2_CONNECTIONS_USB_POLICY_H
#include "io.h"
#ifndef NG_USB_SYSFS
#define NG_USB_SYSFS "/sys/bus/usb/devices"
#endif
#define NG_USB_QUALIFIED_REVISION "31fd"
/* Separate consent record: never changes the on-disk config/journal ABI. */
typedef struct {
  uint32_t version;
  char revision[5];
} ng_usb_approval;
typedef struct {
  int readable, count, valid, qualified, approved;
  char revision[5], port[64];
} ng_usb_profile;
static inline int ng_usb_revision_valid(const char *revision) {
  if (!revision || strlen(revision) != 4)
    return 0;
  for (int i = 0; i < 4; i++)
    if (!((revision[i] >= '0' && revision[i] <= '9') ||
          (revision[i] >= 'a' && revision[i] <= 'f')))
      return 0;
  return strcmp(revision, "0000") && strcmp(revision, "ffff");
}
static inline int ng_usb_approval_load(ng_usb_approval *approval) {
  memset(approval, 0, sizeof(*approval));
  return ng_blob_read(NG_STATE, "usb-approval.bin", approval,
                      sizeof(*approval)) &&
         approval->version == 1 &&
         memchr(approval->revision, 0, sizeof(approval->revision)) &&
         ng_usb_revision_valid(approval->revision) &&
         !strcmp(approval->revision, "3000");
}
static inline int ng_usb_attribute(const char *port, const char *name,
                                    char *out, size_t capacity) {
  char path[512];
  size_t size;
  int n = snprintf(path, sizeof(path), "%s/%s/%s", NG_USB_SYSFS, port, name);
  return n > 0 && (size_t)n < sizeof(path) &&
         ng_read_file(path, out, capacity, &size, 0) && !memchr(out, 0, size);
}
static inline ng_usb_profile ng_usb_observe(void) {
  ng_usb_profile result = {0};
  DIR *directory = opendir(NG_USB_SYSFS);
  if (!directory)
    return result;
  result.readable = 1;
  struct dirent *entry;
  while ((entry = readdir(directory))) {
    if (entry->d_name[0] == '.')
      continue;
    char vendor[16], product[16], revision[16], speed[16];
    if (!ng_usb_attribute(entry->d_name, "idVendor", vendor, sizeof(vendor)) ||
        strcmp(vendor, "0bda\n") ||
        !ng_usb_attribute(entry->d_name, "idProduct", product, sizeof(product)) ||
        strcmp(product, "8153\n"))
      continue;
    result.count++;
    /* Any second RTL8153, including a malformed/unapproved one, is ambiguous. */
    if (result.count != 1)
      continue;
    if (!ng_usb_attribute(entry->d_name, "bcdDevice", revision,
                          sizeof(revision)) ||
        strlen(revision) != 5 || revision[4] != '\n' ||
        !ng_usb_attribute(entry->d_name, "speed", speed, sizeof(speed)) ||
        strcmp(speed, "480\n"))
      continue;
    revision[4] = 0;
    if (!ng_usb_revision_valid(revision) ||
        !ng_copy(result.port, sizeof(result.port), entry->d_name))
      continue;
    memcpy(result.revision, revision, sizeof(result.revision));
    result.valid = 1;
  }
  closedir(directory);
  result.valid = result.valid && result.count == 1;
  result.qualified = result.valid &&
                     !strcmp(result.revision, NG_USB_QUALIFIED_REVISION);
  ng_usb_approval approval;
  result.approved = result.valid && !result.qualified &&
                    ng_usb_approval_load(&approval) &&
                    !strcmp(approval.revision, result.revision);
  return result;
}
static inline int ng_usb_allowed(const ng_usb_profile *profile) {
  return profile->valid && (profile->qualified || profile->approved);
}
static inline int ng_usb_approve(const char *revision, int enabled) {
  if (!ng_usb_revision_valid(revision) ||
      !strcmp(revision, NG_USB_QUALIFIED_REVISION))
    return 0;
  if (enabled) {
    ng_usb_profile current = ng_usb_observe();
    if (strcmp(revision, "3000") || !current.valid || current.qualified ||
        strcmp(current.revision, revision))
      return 0;
    ng_usb_approval approval = {.version = 1};
    memcpy(approval.revision, revision, sizeof(approval.revision));
    return ng_blob_write(NG_STATE, "usb-approval.bin", &approval,
                          sizeof(approval));
  }
  ng_usb_approval previous;
  if (!ng_usb_approval_load(&previous) || strcmp(previous.revision, revision))
    return 0;
  char path[512];
  snprintf(path, sizeof(path), "%s/usb-approval.bin", NG_STATE);
  if (unlink(path))
    return 0;
  int fd = open(NG_STATE, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return 0;
  int ok = !fsync(fd);
  close(fd);
  return ok;
}
static inline void ng_usb_public_json(FILE *out) {
  ng_usb_profile current = ng_usb_observe();
  ng_usb_approval approval;
  int saved = ng_usb_approval_load(&approval);
  fprintf(out,
          "{\"readable\":%s,\"count\":%d,\"valid\":%s,"
          "\"qualified\":%s,\"approved\":%s,\"allowed\":%s,\"revision\":",
          current.readable ? "true" : "false", current.count,
          current.valid ? "true" : "false", current.qualified ? "true" : "false",
          current.approved ? "true" : "false",
          ng_usb_allowed(&current) ? "true" : "false");
  ng_json_quote(out, current.revision);
  fprintf(out, ",\"can_try\":%s",
          current.valid && !strcmp(current.revision, "3000") ? "true" : "false");
  fputs(",\"approval_revision\":", out);
  ng_json_quote(out, saved ? approval.revision : "");
  fputc('}', out);
}
#endif

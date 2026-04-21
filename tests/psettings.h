/* Test shim for psettings.h */
#ifndef _PSYNC_SETTINGS_H
#define _PSYNC_SETTINGS_H

#define _PS(x) #x

static inline const char *psync_setting_get_string(const char *key) {
  (void)key;
  return NULL;
}

#endif

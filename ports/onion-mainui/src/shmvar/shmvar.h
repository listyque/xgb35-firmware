/* Minimal shmvar interface for XGB35 ATM7051H Open MainUI port. */
#ifndef XGB35_SHMVAR_H
#define XGB35_SHMVAR_H

typedef enum {
    MONITOR_VOLUME = 0,
    MONITOR_BRIGHTNESS,
    MONITOR_BGM_VOLUME,
    MONITOR_HIBERNATE_DELAY,
    MONITOR_LUMINATION,
    MONITOR_HUE,
    MONITOR_SATURATION,
    MONITOR_CONTRAST,
    MONITOR_AUDIOFIX,
    MONITOR_MUTE,
    MONITOR_VOLUME_CHANGED,
    MONITOR_VALUE_COUNT
} MonitorValue;

typedef struct {
    int id;
    int *addr;
} KeyShmInfo;

int InitKeyShm(KeyShmInfo *info);
int SetKeyShm(KeyShmInfo *info, MonitorValue key, int value);
int GetKeyShm(KeyShmInfo *info, MonitorValue key);

#endif

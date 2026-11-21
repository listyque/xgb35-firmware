#include "sgxapi.h"
#include "sgxapiperf.h"
#include <stdio.h>
#include <unistd.h>
static PVRSRV_CONNECTION *conn;
static PVRSRV_DEV_DATA dev;
static PVRSRV_SGX_HWPERF_CB_ENTRY entries[256];
static int status(unsigned flags) {
    SGX_MISC_INFO info={0};
    info.eRequest=SGX_MISC_INFO_REQUEST_SET_HWPERF_STATUS;
    info.uData.sSetHWPerfStatus.ui32NewHWPerfStatus=flags;
    return SGXGetMiscInfo(&dev,&info);
}
int model_hw_begin(void) {
    unsigned n=0;
    PVRSRV_DEVICE_IDENTIFIER ids[PVRSRV_MAX_DEVICES];
    if (PVRSRVConnect(&conn,0)) return 0;
    int err=PVRSRVEnumerateDevices(conn,&n,ids);
    if (!err) for (unsigned i=0;i<n && i<PVRSRV_MAX_DEVICES;i++) {
        if (ids[i].eDeviceType!=PVRSRV_DEVICE_TYPE_SGX) continue;
        if (PVRSRVAcquireDeviceData(conn,ids[i].ui32DeviceIndex,&dev,ids[i].eDeviceType)) break;
        err=status(PVRSRV_SGX_HWPERF_STATUS_GRAPHICS_ON);
        if (!err) {
            unsigned count,hz,stamp;
            SGXReadHWPerfCB(&dev,256,entries,&count,&hz,&stamp);
            return 1;
        }
        break;
    }
    fprintf(stderr,"model-hwperf unavailable error=%d\n",err);
    PVRSRVDisconnect(conn);conn=0;
    return 0;
}
void model_hw_sample(int sample) {
    if (!conn) return;
    unsigned count=0,hz=0,stamp=0;
    int err=SGXReadHWPerfCB(&dev,256,entries,&count,&hz,&stamp);
    fprintf(stderr,"model-hwperf sample=%d count=%u hz=%u error=%d pid=%u\n",sample,count,hz,err,(unsigned)getpid());
    if (!err) for (unsigned i=0;i<count && i<256;i++) {
        PVRSRV_SGX_HWPERF_CB_ENTRY *e=entries+i;
        if (e->ui32PID==(unsigned)getpid())
            fprintf(stderr,"model-hwperf event=%x clocks16=%u ordinal=%u frame=%u\n",
                    e->ui32Type,e->ui32Clocksx16,e->ui32Ordinal,e->ui32FrameNo);
    }
}
void model_hw_end(void) {
    if (!conn) return;
    status(PVRSRV_SGX_HWPERF_STATUS_OFF);
    PVRSRVDisconnect(conn);conn=0;
}
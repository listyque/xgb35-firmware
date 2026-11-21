/*
 * aotg_hub_hcd.c — Multi-device USB Hub Host Controller Driver for Actions ATM7051H (ATS3605)
 *
 * Replaces the single-device packet engine of built-in drivers/usb/legacy/aotg_hcd/aotg_hcd.c
 * at runtime (via hcd->driver hot-swap, preserving clocks, resets, PHY calibration & OTG hotplug)
 * to enable external USB hubs with simultaneous Keyboard + Mouse + Flash Drive + Wi-Fi:
 *
 *   1. Single FNADDR (+0x1A6) solved via atomic 1-packet-at-a-time Time-Division Multiplexing (TDM):
 *      FNADDR is switched to urb->dev->devnum before every EP0 stage, Bulk packet, or Interrupt poll.
 *   2. 5x HCIN / 5x HCOUT & 4 KB FIFO limit solved via single-buffered 10-pipe layout (< 4 KB total)
 *      plus automatic LRU channel multiplexing with DATA0/DATA1 sync via ENDPRST (+0x1A2).
 *   3. Missing hardware Split-transactions (TT) bypassed in software via BKDOOR (+0x406 bit 7 = 0x80,
 *      Force Full-Speed 12 Mbps on root reset) so any USB 2.0 hub operates as a transparent USB 1.1
 *      repeater without Split tokens; switchable live via sysfs to 480 Mbps High-Speed hub mode.
 */

#include "aotg_kabi.h"
#ifndef EBUSY
#define EBUSY 16
#endif

extern void usb_disable_autosuspend(struct usb_device *udev);

/* Module parameters (writable at runtime via /sys/module/aotg_hub_hcd/parameters/) */
static int port_mask = 2;        /* bit0 = aotg0 (Mini-USB), bit1 = aotg1 (Big USB-A); default 2 = Big USB-A only */
static int force_full_speed = 1; /* 0 = 480M HS, 1 = 12M FS on attached ports, 2 = FS on aotg0 + HS on aotg1 */
static int ls_preamble_bit = -1; /* -1 = auto-detect {0x80,0x40,0x20,0x10,0x00} on first LS SETUP ACK, >=0 = fixed */
static int hs_nak_us = 60;       /* HS IN poll window before NAK yield (us) */
static int fs_nak_us = 150;      /* FS IN poll window before NAK yield (us) */
static int ls_nak_us = 260;      /* LS IN poll window before NAK yield (us) */
static int idle_backoff_us = 2500;/* Backoff when all queued Bulk IN URBs NAKed (us) — avoids CPU0 timer storm */
static int block_port_suspend = 0;/* Allow clean auto->on PM runtime resume on both hubs and usbhid */
static int need_rebind = 0;       /* Set to 1 on physical unplug so wifi-init cleanly re-attaches on replug */

static int trace_hub = 0;         /* Log hub_irq & GetPortStatus bytes (rate-limited) */

module_param(port_mask, int, 0644);
module_param(force_full_speed, int, 0644);
module_param(ls_preamble_bit, int, 0644);
module_param(hs_nak_us, int, 0644);
module_param(fs_nak_us, int, 0644);
module_param(ls_nak_us, int, 0644);
module_param(idle_backoff_us, int, 0644);
module_param(block_port_suspend, int, 0644);
module_param(need_rebind, int, 0444);
module_param(trace_hub, int, 0644);

/* Debug snapshot of scheduler state (read via /sys/module/aotg_hub_hcd/parameters/dbg_*) */
static int dbg_kicks = 0, dbg_busy = 0, dbg_stage = 0, dbg_sched = 0;
module_param(dbg_kicks, int, 0444);
module_param(dbg_busy, int, 0444);
module_param(dbg_stage, int, 0444);
module_param(dbg_sched, int, 0444);

/* Verified CUSB2 / Actions AOTG register offsets from base (0xB0240000 / 0xB0250000) */
#define AOTG_IO_ADDR(x)      ((void __iomem *)((((unsigned long)(x)) & 0x03FFFFFFUL) + 0xF8000000UL))
#define REG8(base, off)      (*(volatile u8 __iomem *)((u8 __iomem *)(base) + (off)))
#define REG16(base, off)     (*(volatile u16 __iomem *)((u8 __iomem *)(base) + (off)))
#define REG32(base, off)     (*(volatile u32 __iomem *)((u8 __iomem *)(base) + (off)))

#define R_HCIN0BC            0x000
#define R_HCOUT0BC           0x001
#define R_HCEP0CS            0x002
#define   EP0CS_STALL        0x01
#define   EP0CS_HXBUSY       0x02
#define   EP0CS_OUTBUSY      0x04
#define   EP0CS_INBUSY       0x08
#define   EP0CS_HCSET        0x10
#define   EP0CS_HSNAK        0x40

#define R_HCINxBC(c)         (0x008 + (((c) - 1) * 8))
#define R_HCINxCON(c)        (0x00A + (((c) - 1) * 8))
#define R_HCINxCS(c)         (0x00B + (((c) - 1) * 8))
#define R_HCOUTxBC(c)        (0x00C + (((c) - 1) * 8))
#define R_HCOUTxCON(c)       (0x00E + (((c) - 1) * 8))
#define R_HCOUTxCS(c)        (0x00F + (((c) - 1) * 8))
#define   EPCON_VAL          0x80
#define   EPCON_TYPE_BULK    0x08
#define   EPCON_TYPE_INTR    0x0C
#define   EPCS_BUSY          0x02

#define R_FIFOxDAT(c)        (0x084 + (((c) - 1) * 4))
#define R_HCEP0CTRL          0x0C0
#define R_HCOUT0ERR          0x0C1
#define R_HCIN0ERR           0x0C3
#define R_HCOUTxCTRL(c)      (0x0C4 + (((c) - 1) * 4))
#define R_HCOUTxERR(c)       (0x0C5 + (((c) - 1) * 4))
#define R_HCINxCTRL(c)       (0x0C6 + (((c) - 1) * 4))
#define R_HCINxERR(c)        (0x0C7 + (((c) - 1) * 4))
#define   EPERR_RESEND       0x20

#define R_EP0OUTDAT          0x100
#define R_EP0INDAT           0x140

#define R_HCOUT07IRQ         0x188
#define R_HCIN07IRQ          0x18A
#define R_USBIRQ             0x18C
#define   USBIRQ_URES        0x10
#define   USBIRQ_HS          0x20
#define R_HCOUT07IEN         0x194
#define R_HCIN07IEN          0x196
#define R_USBIEN             0x198

#define R_ENDPRST            0x1A2
#define   ENDPRST_OUT        0x10
#define   ENDPRST_TOGRST     0x20
#define   ENDPRST_FIFORST    0x40
#define   ENDPRST_TOGSETQ    0x80
#define R_USBCS              0x1A3
#define R_FNADDR             0x1A6
#define R_FIFOCTRL           0x1A8
#define R_HCPORTCTRL         0x1AB

#define R_HCIN07ERRIRQ       0x1B4
#define R_HCOUT07ERRIRQ      0x1B6
#define R_HCIN07ERRIEN       0x1B8
#define R_HCOUT07ERRIEN      0x1BA
#define R_OTGIRQ             0x1BC
#define R_OTGSTATE           0x1BD
#define R_OTGIEN             0x1C0

#define R_HCIN0MAXPCK        0x1E0
#define R_HCINxMAXPCK(c)     (0x1E2 + (((c) - 1) * 2))
#define R_HCINxSTARTADDR(c)  (0x304 + (((c) - 1) * 4))
#define R_HCOUTxSTARTADDR(c) (0x344 + (((c) - 1) * 4))
#define R_HCOUT0MAXPCK       0x3E0
#define R_HCOUTxMAXPCK(c)    (0x3E2 + (((c) - 1) * 2))

#define R_BKDOOR             0x406
#define   BKDOOR_HSDISABLE   0x80
#define R_USBEIRQ            0x40A
#define R_USBEIEN            0x40C
#define R_HCINSTOP           0x410
#define R_HCINAUTO           0x414
#define R_HCINXSTART         0x416
#define R_HCINxCOUNTL(c)     (0x420 + (((c) - 1) * 4))
#define R_UDMAIRQ            0x434
#define R_UDMAIEN            0x436

/* Single-buffered FIFO map strictly inside lower 4 KB SRAM (< 0x0F80) */
static const u16 in_fifo_addr[5]  = { 0x0080, 0x0280, 0x0480, 0x04C0, 0x0500 };
static const u16 in_fifo_cap[5]   = {    512,    512,     64,     64,     64 };
static const u16 out_fifo_addr[5] = { 0x0580, 0x0780, 0x0980, 0x0B80, 0x0D80 };
static const u16 out_fifo_cap[5]  = {    512,    512,    512,    512,    512 };

enum xfer_stage {
    STAGE_IDLE = 0,
    STAGE_EP0_SETUP,
    STAGE_EP0_IN,
    STAGE_EP0_OUT,
    STAGE_EP0_STATUS_IN,
    STAGE_EP0_STATUS_OUT,
    STAGE_DATA_IN,
    STAGE_DATA_OUT,
};

struct aotg_hub_ep {
    struct list_head sched_node;
    struct usb_host_endpoint *ep;
    struct usb_device *udev;
    ktime_t next_poll;
    u16 maxpacket;
    u8 epnum;
    u8 pipetype;      /* PIPE_CONTROL(2), PIPE_INTERRUPT(1), PIPE_BULK(3), PIPE_ISOCHRONOUS(0) */
    u8 is_out;
    u8 hw_chan;       /* 0 = unassigned, 1..5 = assigned HCIN/HCOUT channel */
    u8 last_sw_toggle;
    u8 toggle_valid;
};

struct hw_chan_slot {
    struct aotg_hub_ep *owner;
    unsigned long last_used_seq;
};

struct done_urb_entry {
    struct list_head node;
    struct urb *urb;
    int status;
};

struct aotg_hub_port {
    int id;
    int attached;
    struct device *pdev_dev;
    struct usb_hcd *hcd;
    void __iomem *base;
    const struct hc_driver *orig_driver;
    struct hc_driver patched_driver;

    spinlock_t lock;
    struct tasklet_struct bh_tasklet;
    struct hrtimer poll_timer;
    struct hrtimer sched_timer;

    struct list_head ctrl_eps;
    struct list_head intr_eps;
    struct list_head bulk_eps;
    struct list_head done_urbs;

    struct hw_chan_slot in_slots[5];
    struct hw_chan_slot out_slots[5];
    unsigned long use_seq;

    struct urb *active_urb;
    struct aotg_hub_ep *active_hep;
    enum xfer_stage stage;
    u8 active_chan;
    u8 last_err_reg;
    u8 err_retries;
    u16 last_out_len;
    u8 zlp_pending;
    u8 ls_auto_idx;
    u8 ls_auto_locked;
    u8 in_sched_cb;
    u8 sched_running;
    u32 nak_streak;
    unsigned long sched_wait_ns;

    u32 pio_bounce[128]; /* 32-bit aligned 512-byte bounce buffer */
};

static struct aotg_hub_port g_ports[2];

/* Built-in acthcd helpers via verified offsets from hcd (+0xe0) */
static inline int *acthcd_speed_ptr(struct usb_hcd *hcd)
{
    return (int *)((u8 *)hcd + 0x110);
}

static inline u32 *acthcd_port_ptr(struct usb_hcd *hcd)
{
    return (u32 *)((u8 *)hcd + 0x118);
}

static inline int port_wants_fs(const struct aotg_hub_port *p)
{
    if (force_full_speed == 1)
        return 1;
    if (force_full_speed == 2)
        return (p->id == 0);
    return 0;
}

static void aotg_udelay(unsigned long us)
{
    if (arm_delay_ops.udelay)
        arm_delay_ops.udelay(us);
}

static void endprst_cmd(void __iomem *base, u8 chan, int is_out, u8 flags)
{
    u8 sel = (is_out ? ENDPRST_OUT : 0) | (chan & 0x0F);
    REG8(base, R_ENDPRST) = sel;
    dsb();
    REG8(base, R_ENDPRST) = sel | flags;
    dsb();
}

static void stop_in_chan(void __iomem *base, u8 chan)
{
    u8 mask = (u8)(1U << chan);
    REG8(base, R_HCINxCON(chan)) &= (u8)~EPCON_VAL;
    if (REG8(base, R_HCINSTOP) & mask)
        REG8(base, R_HCINSTOP) = mask;
    REG8(base, R_HCINXSTART) &= (u8)~mask;
    REG8(base, R_HCINAUTO) &= (u8)~mask;
    REG16(base, R_HCINxCOUNTL(chan)) = 0;
    dsb();
}

static void aotg_hw_quiesce_pipes(struct aotg_hub_port *p)
{
    void __iomem *base = p->base;
    int c;

    /* Mask UDMA & per-EP interrupts; keep OTG + USB reset/disconnect IRQs enabled */
    REG8(base, R_UDMAIEN) = 0;
    REG8(base, R_UDMAIRQ) = 0xFF;
    REG8(base, R_HCIN07IEN) = 0;
    REG8(base, R_HCOUT07IEN) = 0;
    REG8(base, R_HCIN07ERRIEN) = 0;
    REG8(base, R_HCOUT07ERRIEN) = 0;
    REG8(base, R_HCINXSTART) = 0;
    REG8(base, R_HCINAUTO) = 0;

    endprst_cmd(base, 0, 0, ENDPRST_FIFORST | ENDPRST_TOGRST);
    endprst_cmd(base, 0, 1, ENDPRST_FIFORST | ENDPRST_TOGRST);

    for (c = 1; c <= 5; c++) {
        REG32(base, R_HCINxSTARTADDR(c))  = 0x80000000U | in_fifo_addr[c - 1];
        REG32(base, R_HCOUTxSTARTADDR(c)) = 0x80000000U | out_fifo_addr[c - 1];
        REG8(base, R_HCINxCON(c))  = EPCON_VAL | EPCON_TYPE_BULK;
        REG8(base, R_HCOUTxCON(c)) = EPCON_VAL | EPCON_TYPE_BULK;
        if (!(REG8(base, R_HCINxCS(c)) & EPCS_BUSY))
            REG8(base, R_HCINxCS(c)) = 0;
        endprst_cmd(base, (u8)c, 0, ENDPRST_FIFORST | ENDPRST_TOGRST);
        endprst_cmd(base, (u8)c, 1, ENDPRST_FIFORST | ENDPRST_TOGRST);
        stop_in_chan(base, (u8)c);
        REG8(base, R_HCINxCON(c)) = 0;
        REG8(base, R_HCOUTxCON(c)) = 0;
        p->in_slots[c - 1].owner = NULL;
        p->out_slots[c - 1].owner = NULL;
    }

    REG8(base, R_HCIN07IRQ) = 0xFF;
    REG8(base, R_HCOUT07IRQ) = 0xFF;
    REG8(base, R_HCIN07ERRIRQ) = 0xFF;
    REG8(base, R_HCOUT07ERRIRQ) = 0xFF;
    REG8(base, R_USBEIEN) |= 0x80;
    dsb();
}

/* Copy payload between URB (contiguous transfer_buffer OR scatterlist) and aligned bounce buffer */
static void urb_copy_bytes(struct urb *urb, u32 offset, void *bounce, u32 len, int to_urb)
{
    u8 *b = (u8 *)bounce;
    if (!len)
        return;

    if (urb->num_sgs > 0 && urb->sg) {
        struct scatterlist *sg = urb->sg;
        u32 cur = 0;
        int i;
        for (i = 0; i < urb->num_sgs && sg && len > 0; i++, sg = sg_next(sg)) {
            if (offset >= cur + sg->length) {
                cur += sg->length;
                continue;
            }
            {
                u32 sg_off = offset - cur;
                u32 avail = sg->length - sg_off;
                u32 chunk = (len < avail) ? len : avail;
                u8 * page_va = (u8 *)kmap_atomic(sg_page(sg));
                if (to_urb)
                    memcpy(page_va + sg->offset + sg_off, b, chunk);
                else
                    memcpy(b, page_va + sg->offset + sg_off, chunk);
                __kunmap_atomic(page_va);
                b += chunk;
                offset += chunk;
                cur += sg->length;
                len -= chunk;
            }
        }
        return;
    }

    if (urb->transfer_buffer) {
        u8 *ub = (u8 *)urb->transfer_buffer + offset;
        if (to_urb)
            memcpy(ub, b, len);
        else
            memcpy(b, ub, len);
    }
}

/* EP0 uses linear 64-byte SRAM windows at R_EP0OUTDAT (0x100..0x13F) & R_EP0INDAT (0x140..0x17F) */
static void ep0_read_buf(void __iomem *base, u8 *dst, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++)
        dst[i] = REG8(base, R_EP0INDAT + i);
}

static void ep0_write_buf(void __iomem *base, const u8 *src, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++)
        REG8(base, R_EP0OUTDAT + i) = src[i];
}

/* EP1..5 use streaming 32-bit FIFO data ports at R_FIFOxDAT(1..5) (0x084..0x094) */
static void fifo_read_pio(void __iomem *fifo_reg, u32 *aligned_dst, u32 len)
{
    u32 words = len >> 2;
    u32 rem = len & 3;
    u32 i;
    for (i = 0; i < words; i++)
        aligned_dst[i] = REG32(fifo_reg, 0);
    if (rem) {
        u8 *tail = (u8 *)&aligned_dst[words];
        for (i = 0; i < rem; i++)
            tail[i] = REG8(fifo_reg, i);
    }
}

static void fifo_write_pio(void __iomem *fifo_reg, const u32 *aligned_src, u32 len)
{
    u32 words = len >> 2;
    u32 rem = len & 3;
    u32 i;
    for (i = 0; i < words; i++)
        REG32(fifo_reg, 0) = aligned_src[i];
    if (rem) {
        const u8 *tail = (const u8 *)&aligned_src[words];
        for (i = 0; i < rem; i++)
            REG8(fifo_reg, i) = tail[i];
    }
}

static int decode_ep_err(u8 err_reg)
{
    u8 code = (err_reg >> 2) & 0x07;
    if (code == 0)
        return 0;
    if (code == 3)
        return -EPIPE;     /* STALL */
    if (code == 4)
        return -ETIMEDOUT; /* Device did not respond / CRC timeout */
    return -EPROTO;
}

static const u8 ls_pre_candidates[5] = { 0x80, 0x40, 0x20, 0x10, 0x00 };

static u8 current_ls_pre_bits(const struct aotg_hub_port *p)
{
    if (ls_preamble_bit >= 0)
        return (u8)(ls_preamble_bit & 0xF0);
    return ls_pre_candidates[p->ls_auto_idx % 5];
}

static u8 make_epctrl(const struct aotg_hub_port *p, const struct urb *urb, u8 epnum)
{
    u8 ctrl = epnum & 0x0F;
    /* Behind a Full-Speed (12M) root hub, Low-Speed (1.5M) wired mice/keyboards use PRE preamble */
    if (*acthcd_speed_ptr(p->hcd) == USB_SPEED_FULL &&
        urb->dev && urb->dev->speed == USB_SPEED_LOW) {
        ctrl |= current_ls_pre_bits(p);
    }
    return ctrl;
}

/* Bind or LRU-multiplex a hardware channel 1..5 for IN or OUT */
static u8 acquire_hw_chan(struct aotg_hub_port *p, struct aotg_hub_ep *hep)
{
    void __iomem *base = p->base;
    struct hw_chan_slot *slots = hep->is_out ? p->out_slots : p->in_slots;
    const u16 *caps = hep->is_out ? out_fifo_cap : in_fifo_cap;
    u8 sw_tog = usb_gettoggle(hep->udev, hep->epnum, hep->is_out);
    u8 chan = hep->hw_chan;
    int need_rebind = 0;
    int i;

    if (chan >= 1 && chan <= 5 && slots[chan - 1].owner == hep &&
        caps[chan - 1] >= hep->maxpacket) {
        slots[chan - 1].last_used_seq = ++p->use_seq;
        if (!hep->toggle_valid || hep->last_sw_toggle != sw_tog)
            need_rebind = 1;
    } else {
        int best = -1;
        int best_free = 0;
        u16 best_cap = 0xFFFFU;
        unsigned long oldest = ~0UL;
        for (i = 0; i < 5; i++) {
            int is_free = (slots[i].owner == NULL);
            if (caps[i] < hep->maxpacket)
                continue;
            /* Best-fit free slot first (keeps 512B channels 1..2 free for Flash/Wi-Fi while 64B 3..5 take HID/Hub) */
            if (is_free) {
                if (!best_free || caps[i] < best_cap) {
                    best = i;
                    best_free = 1;
                    best_cap = caps[i];
                }
                continue;
            }
            if (!best_free) {
                int cur_intr = (slots[i].owner->pipetype == PIPE_INTERRUPT);
                int best_intr = (best >= 0 && slots[best].owner && slots[best].owner->pipetype == PIPE_INTERRUPT);
                /* Prefer evicting Bulk over Interrupt so Hub/HID channels keep native toggle */
                if (best < 0 || (best_intr && !cur_intr) ||
                    (best_intr == cur_intr && slots[i].last_used_seq < oldest)) {
                    best = i;
                    best_cap = caps[i];
                    oldest = slots[i].last_used_seq;
                }
            }
        }
        if (best < 0)
            return 0;
        if (slots[best].owner && slots[best].owner != hep) {
            slots[best].owner->hw_chan = 0;
            slots[best].owner->toggle_valid = 0;
        }
        chan = (u8)(best + 1);
        slots[best].owner = hep;
        slots[best].last_used_seq = ++p->use_seq;
        hep->hw_chan = chan;
        need_rebind = 1;
    }

    if (need_rebind) {
        u8 con = (hep->pipetype == PIPE_INTERRUPT) ? EPCON_TYPE_INTR : EPCON_TYPE_BULK;
        if (hep->is_out) {
            REG8(base, R_HCOUTxCON(chan)) = EPCON_VAL | con;
            REG16(base, R_HCOUTxMAXPCK(chan)) = hep->maxpacket;
            REG8(base, R_HCOUTxCTRL(chan)) = hep->epnum;
            endprst_cmd(base, chan, 1, ENDPRST_FIFORST | ENDPRST_TOGRST);
            if (sw_tog)
                endprst_cmd(base, chan, 1, ENDPRST_TOGSETQ);
            REG8(base, R_HCOUTxCON(chan)) = con;
        } else {
            REG8(base, R_HCINxCON(chan)) = EPCON_VAL | con;
            if (!(REG8(base, R_HCINxCS(chan)) & EPCS_BUSY))
                REG8(base, R_HCINxCS(chan)) = 0;
            stop_in_chan(base, chan);
            REG16(base, R_HCINxMAXPCK(chan)) = hep->maxpacket;
            REG8(base, R_HCINxCTRL(chan)) = hep->epnum;
            REG8(base, R_HCINxCON(chan)) = EPCON_VAL | con;
            endprst_cmd(base, chan, 0, ENDPRST_FIFORST | ENDPRST_TOGRST);
            if (sw_tog)
                endprst_cmd(base, chan, 0, ENDPRST_TOGSETQ);
            REG8(base, R_HCINxCON(chan)) = con;
        }
        hep->last_sw_toggle = sw_tog;
        hep->toggle_valid = 1;
    }

    return chan;
}

static void queue_urb_done_locked(struct aotg_hub_port *p, struct urb *urb, int status)
{
    struct done_urb_entry *d;
    usb_hcd_unlink_urb_from_ep(p->hcd, urb);
    urb->hcpriv = NULL;
    d = (struct done_urb_entry *)__kmalloc(sizeof(*d), GFP_ATOMIC);
    if (d) {
        d->urb = urb;
        d->status = status;
        list_add_tail(&d->node, &p->done_urbs);
        tasklet_schedule(&p->bh_tasklet);
    } else {
        /* Fallback if atomic pool is exhausted */
        spin_unlock(&p->lock);
        usb_hcd_giveback_urb(p->hcd, urb, status);
        spin_lock(&p->lock);
    }
}

static void arm_poll_timer_us(struct aotg_hub_port *p, unsigned int us)
{
    ktime_t kt = ktime_set(0, (unsigned long)us * 1000UL);
    hrtimer_start(&p->poll_timer, kt, HRTIMER_MODE_REL);
}

static void aotg_hub_kick_locked(struct aotg_hub_port *p);

static void start_urb_step_locked(struct aotg_hub_port *p, struct aotg_hub_ep *hep, struct urb *urb)
{
    void __iomem *base = p->base;
    u8 devnum = (u8)usb_pipedevice(urb->pipe);

    p->active_urb = urb;
    p->active_hep = hep;

    /* Core TDM rule: switch single hardware FNADDR before EVERY packet */
    REG8(base, R_FNADDR) = devnum;
    dsb();

    if (hep->pipetype == PIPE_CONTROL) {
        u16 maxp = hep->maxpacket ? hep->maxpacket : 8;
        int tries;
        REG8(base, R_HCEP0CTRL) = make_epctrl(p, urb, hep->epnum);
        REG8(base, R_HCIN0MAXPCK) = (u8)maxp;
        REG8(base, R_HCOUT0MAXPCK) = (u8)maxp;
        REG8(base, R_HCIN07IRQ) = 0x01;
        REG8(base, R_HCOUT07IRQ) = 0x01;
        REG8(base, R_HCIN07ERRIRQ) = 0x01;
        REG8(base, R_HCOUT07ERRIRQ) = 0x01;
        REG8(base, R_HCIN07IEN) |= 0x01;
        REG8(base, R_HCOUT07IEN) |= 0x01;
        REG8(base, R_HCIN07ERRIEN) |= 0x01;
        REG8(base, R_HCOUT07ERRIEN) |= 0x01;

        if (p->stage == STAGE_IDLE) {
            p->stage = STAGE_EP0_SETUP;
            endprst_cmd(base, 0, 0, ENDPRST_FIFORST);
            endprst_cmd(base, 0, 1, ENDPRST_FIFORST);
            REG8(base, R_HCEP0CS) = EP0CS_HCSET;
            for (tries = 0; tries < 200 && (REG8(base, R_HCEP0CS) & EP0CS_OUTBUSY); tries++)
                aotg_udelay(1);
            memcpy(p->pio_bounce, urb->setup_packet, 8);
            REG32(base, R_EP0OUTDAT) = p->pio_bounce[0];
            REG32(base, R_EP0OUTDAT + 4) = p->pio_bounce[1];
            dsb();
            REG8(base, R_HCOUT0BC) = 8;
            arm_poll_timer_us(p, 80000);
            return;
        }
        if (p->stage == STAGE_EP0_IN) {
            REG8(base, R_HCIN0BC) = 0;
            arm_poll_timer_us(p, 80000);
            return;
        }
        if (p->stage == STAGE_EP0_OUT) {
            u32 rem = urb->transfer_buffer_length - urb->actual_length;
            u32 chunk = (rem > maxp) ? maxp : rem;
            urb_copy_bytes(urb, urb->actual_length, p->pio_bounce, chunk, 0);
            ep0_write_buf(base, (const u8 *)p->pio_bounce, chunk);
            p->last_out_len = (u16)chunk;
            dsb();
            REG8(base, R_HCOUT0BC) = (u8)chunk;
            arm_poll_timer_us(p, 80000);
            return;
        }
        if (p->stage == STAGE_EP0_STATUS_OUT) {
            REG8(base, R_HCEP0CS) = EP0CS_HSNAK;
            dsb();
            REG8(base, R_HCOUT0BC) = 0;
            arm_poll_timer_us(p, 80000);
            return;
        }
        if (p->stage == STAGE_EP0_STATUS_IN) {
            REG8(base, R_HCEP0CS) = EP0CS_HSNAK;
            dsb();
            REG8(base, R_HCIN0BC) = 0;
            arm_poll_timer_us(p, 80000);
            return;
        }
    }

    /* Bulk or Interrupt endpoint */
    {
        u8 chan = acquire_hw_chan(p, hep);
        u8 mask;
        if (!chan) {
            p->active_urb = NULL;
            p->active_hep = NULL;
            p->stage = STAGE_IDLE;
            queue_urb_done_locked(p, urb, -ENOSPC);
            return;
        }
        p->active_chan = chan;
        mask = (u8)(1U << chan);

        if (hep->is_out) {
            u32 rem = urb->transfer_buffer_length - urb->actual_length;
            u32 chunk = (rem > hep->maxpacket) ? hep->maxpacket : rem;
            p->stage = STAGE_DATA_OUT;
            p->last_out_len = (u16)chunk;

            REG8(base, R_HCOUTxCTRL(chan)) = make_epctrl(p, urb, hep->epnum);
            REG8(base, R_HCOUTxCON(chan)) |= EPCON_VAL;
            REG8(base, R_FIFOCTRL) = ENDPRST_OUT | chan;
            REG8(base, R_HCOUT07IRQ) = mask;
            REG8(base, R_HCOUT07ERRIRQ) = mask;
            REG8(base, R_HCOUT07IEN) |= mask;
            REG8(base, R_HCOUT07ERRIEN) |= mask;

            if (chunk > 0) {
                urb_copy_bytes(urb, urb->actual_length, p->pio_bounce, chunk, 0);
                fifo_write_pio((u8 __iomem *)base + R_FIFOxDAT(chan), p->pio_bounce, chunk);
            }
            REG16(base, R_HCOUTxBC(chan)) = (u16)chunk;
            dsb();
            REG8(base, R_HCOUTxCS(chan)) = 0; /* Arm & transmit 1 OUT packet */
            arm_poll_timer_us(p, 500000);
        } else {
            unsigned int wait_us = hs_nak_us;
            if (urb->dev->speed == USB_SPEED_LOW)
                wait_us = ls_nak_us;
            else if (*acthcd_speed_ptr(p->hcd) != USB_SPEED_HIGH || urb->dev->speed == USB_SPEED_FULL)
                wait_us = fs_nak_us;

            p->stage = STAGE_DATA_IN;
            REG8(base, R_HCINxCTRL(chan)) = make_epctrl(p, urb, hep->epnum);
            REG8(base, R_HCINxCON(chan)) |= EPCON_VAL;
            REG8(base, R_FIFOCTRL) = chan;
            REG8(base, R_HCIN07IRQ) = mask;
            REG8(base, R_HCIN07ERRIRQ) = mask;
            REG8(base, R_HCIN07IEN) |= mask;
            REG8(base, R_HCIN07ERRIEN) |= mask;

            REG16(base, R_HCINxCOUNTL(chan)) = 1;
            REG8(base, R_HCINAUTO) |= mask;
            dsb();
            REG8(base, R_HCINXSTART) |= mask; /* Request 1 IN packet via CUSB2 auto-token engine */
            arm_poll_timer_us(p, wait_us);
        }
    }
}

static unsigned long ep_interval_ns(const struct urb *urb)
{
    unsigned long ns;
    int ivl = urb->interval;
    if (ivl <= 0)
        ivl = 1;
    if (urb->dev && urb->dev->speed == USB_SPEED_HIGH) {
        /* urb->interval is already in microframes (125 us) for HS in Linux USB core */
        ns = (unsigned long)ivl * 125000UL;
        if (ns > 255000000UL) ns = 255000000UL;
    } else {
        ns = (unsigned long)ivl * 1000000UL;
        if (ns > 32000000UL) ns = 32000000UL;
    }
    /* shortcut: 4ms (250Hz) min HID poll floor on shared 12M TDM bus, lower to 1ms if 1000Hz mouse needed */
    if (ns < 4000000UL)
        ns = 4000000UL;
    return ns;
}

/* Pick next runnable URB across all devices: Interrupt (due) -> Control -> Round-Robin Bulk */
static void aotg_hub_kick_locked(struct aotg_hub_port *p)
{
    struct aotg_hub_ep *hep, *tmp;
    ktime_t now;
    ktime_t earliest_wait = { .tv64 = 0x7FFFFFFFFFFFFFFFLL };
    int has_wait = 0;

    dbg_kicks++;
    dbg_busy = p->active_urb ? 1 : 0;
    dbg_stage = p->stage;
    dbg_sched = p->sched_running;
    if (p->active_urb)
        return;
    if (REG8(p->base, R_OTGSTATE) != 3)
        return;

    now = ktime_get();

    /* 1. Periodic Interrupt pipes whose polling interval is due */
    list_for_each_entry_safe(hep, tmp, &p->intr_eps, sched_node) {
        if (list_empty(&hep->ep->urb_list))
            continue;
        if (hep->next_poll.tv64 <= now.tv64) {
            struct urb *urb = list_first_entry(&hep->ep->urb_list, struct urb, urb_list);
            list_move_tail(&hep->sched_node, &p->intr_eps);
            p->stage = STAGE_IDLE;
            p->zlp_pending = 0;
            start_urb_step_locked(p, hep, urb);
            return;
        }
        if (hep->next_poll.tv64 < earliest_wait.tv64) {
            earliest_wait = hep->next_poll;
            has_wait = 1;
        }
    }

    /* 2. Control EP0 pipes */
    list_for_each_entry_safe(hep, tmp, &p->ctrl_eps, sched_node) {
        if (!list_empty(&hep->ep->urb_list)) {
            struct urb *urb = list_first_entry(&hep->ep->urb_list, struct urb, urb_list);
            list_move_tail(&hep->sched_node, &p->ctrl_eps);
            p->stage = STAGE_IDLE;
            p->zlp_pending = 0;
            start_urb_step_locked(p, hep, urb);
            return;
        }
    }

    /* 3. Round-Robin Bulk pipes (with gentle idle backoff if every Bulk IN pipe just NAKed) */
    {
        u32 runnable_bulk = 0;
        list_for_each_entry(hep, &p->bulk_eps, sched_node) {
            if (!list_empty(&hep->ep->urb_list))
                runnable_bulk++;
        }
        if (runnable_bulk > 0) {
            if (p->nak_streak >= runnable_bulk && idle_backoff_us > 0) {
                ktime_t bk = ktime_add_ns(now, (unsigned long)idle_backoff_us * 1000UL);
                p->nak_streak = 0;
                if (!has_wait || bk.tv64 < earliest_wait.tv64) {
                    earliest_wait = bk;
                    has_wait = 1;
                }
            } else {
                list_for_each_entry_safe(hep, tmp, &p->bulk_eps, sched_node) {
                    if (!list_empty(&hep->ep->urb_list)) {
                        struct urb *urb = list_first_entry(&hep->ep->urb_list, struct urb, urb_list);
                        list_move_tail(&hep->sched_node, &p->bulk_eps);
                        p->stage = STAGE_IDLE;
                        p->zlp_pending = (u8)(hep->is_out && (urb->transfer_flags & URB_ZERO_PACKET) &&
                                              urb->transfer_buffer_length > 0 &&
                                              (urb->transfer_buffer_length % hep->maxpacket) == 0);
                        start_urb_step_locked(p, hep, urb);
                        return;
                    }
                }
            }
        }
    }

    if (has_wait) {
        s64 delta_ns = earliest_wait.tv64 - now.tv64;
        if (delta_ns < 50000LL)
            delta_ns = 50000LL;
        if (delta_ns > 50000000LL)
            delta_ns = 50000000LL;
        p->sched_wait_ns = (unsigned long)delta_ns;
        if (!p->in_sched_cb && !p->sched_running) {
            p->sched_running = 1;
            hrtimer_start(&p->sched_timer, ktime_set(0, (unsigned long)delta_ns), HRTIMER_MODE_REL);
        }
    }
}

static void finish_active_urb_locked(struct aotg_hub_port *p, int status)
{
    struct urb *urb = p->active_urb;
    struct aotg_hub_ep *hep = p->active_hep;

    hrtimer_try_to_cancel(&p->poll_timer);
    p->active_urb = NULL;
    p->active_hep = NULL;
    p->stage = STAGE_IDLE;
    p->active_chan = 0;
    p->err_retries = 0;
    p->zlp_pending = 0;

    if (hep && hep->pipetype == PIPE_INTERRUPT && urb)
        hep->next_poll = ktime_add_ns(ktime_get(), ep_interval_ns(urb));

    if (trace_hub && status != 0) {
        static int err_budget = 40;
        if (err_budget > 0 && urb) {
            err_budget--;
            printk("<6>aotg_trace: ERR dev=%u ep=%u out=%u stage=%d er=%02x otg=%02x cs=%02x st=%d\n",
                   usb_pipedevice(urb->pipe), hep ? hep->epnum : 0,
                   hep ? hep->is_out : 0, p->stage, p->last_err_reg,
                   REG8(p->base, R_OTGSTATE), REG8(p->base, R_USBCS), status);
        }
    }
    if (trace_hub && status == 0 && urb && urb->setup_packet &&
        urb->setup_packet[0] == 0xA3 && urb->setup_packet[1] == 0x00 && urb->actual_length == 4) {
        static int gps_budget = 40;
        if (gps_budget > 0) {
            u8 *b = (u8 *)urb->transfer_buffer;
            gps_budget--;
            printk("<6>aotg_trace: GetPortStatus dev=%u port=%u -> st=%02x%02x chg=%02x%02x\n",
                   usb_pipedevice(urb->pipe), urb->setup_packet[4],
                   b[1], b[0], b[3], b[2]);
        }
    }
    if (trace_hub && status == 0 && hep && hep->pipetype == PIPE_INTERRUPT && hep->maxpacket == 1 && urb) {
        static int hirq_budget = 40;
        if (hirq_budget > 0) {
            u8 *b = (u8 *)urb->transfer_buffer;
            hirq_budget--;
            printk("<6>aotg_trace: hub_irq dev=%u ch=%u len=%u byte=%02x\n",
                   usb_pipedevice(urb->pipe), p->active_chan, urb->actual_length, b ? b[0] : 0xff);
        }
    }

    if (urb)
        queue_urb_done_locked(p, urb, status);
}

static void handle_active_in_done_locked(struct aotg_hub_port *p)
{
    void __iomem *base = p->base;
    struct urb *urb = p->active_urb;
    struct aotg_hub_ep *hep = p->active_hep;
    u8 chan = p->active_chan;
    u32 cnt, rem, copy_len;
    int status = 0;

    p->nak_streak = 0;

    cnt = REG16(base, R_HCINxBC(chan)) & 0x0FFFU;
    rem = urb->transfer_buffer_length - urb->actual_length;
    copy_len = (cnt <= rem) ? cnt : rem;

    if (copy_len > 0) {
        fifo_read_pio((u8 __iomem *)base + R_FIFOxDAT(chan), p->pio_bounce, copy_len);
        urb_copy_bytes(urb, urb->actual_length, p->pio_bounce, copy_len, 1);
        urb->actual_length += copy_len;
    }
    if (cnt > rem)
        status = -EOVERFLOW;

    /* MUST ack R_HCINxCS = 0 BEFORE clearing EPCON_VAL in stop_in_chan (per 0xc0294000..0xc0294038)! */
    REG8(base, R_HCINxCS(chan)) = 0;
    dsb();
    stop_in_chan(base, chan);

    usb_dotoggle(hep->udev, hep->epnum, 0);
    hep->last_sw_toggle = usb_gettoggle(hep->udev, hep->epnum, 0);

    if (status == 0 && urb->actual_length < urb->transfer_buffer_length && cnt == hep->maxpacket) {
        /* More packets remain in this URB; if no Interrupt pipe is overdue, continue immediately,
         * otherwise yield between packets so keyboard/mouse latency stays < 1 ms even during large reads!
         */
        hrtimer_try_to_cancel(&p->poll_timer);
        p->active_urb = NULL;
        p->active_hep = NULL;
        p->stage = STAGE_IDLE;
        list_move(&hep->sched_node, (hep->pipetype == PIPE_INTERRUPT) ? &p->intr_eps : &p->bulk_eps);
        aotg_hub_kick_locked(p);
        return;
    }

    if (status == 0 && cnt < hep->maxpacket &&
        urb->actual_length < urb->transfer_buffer_length &&
        (urb->transfer_flags & URB_SHORT_NOT_OK))
        status = -EREMOTEIO;

    finish_active_urb_locked(p, status);
    aotg_hub_kick_locked(p);
}

static irqreturn_t aotg_hub_irq(struct usb_hcd *hcd)
{
    struct aotg_hub_port *p = (hcd == g_ports[0].hcd) ? &g_ports[0] : &g_ports[1];
    void __iomem *base = p->base;
    u8 usbeirq, usbirq, in_err, out_err, in_irq, out_irq;
    unsigned long flags;

    usbeirq = REG8(base, R_USBEIRQ);
    if (!(usbeirq & 0x80))
        return IRQ_NONE;

    /* Let built-in OTG state machine & hotplug_timer handle physical cable plug/unplug */
    if (REG8(base, R_OTGIRQ) & REG8(base, R_OTGIEN)) {
        u8 saved_inien, saved_outien, saved_usbien;
        spin_lock_irqsave(&p->lock, flags);
        saved_inien  = REG8(base, R_HCIN07IEN);
        saved_outien = REG8(base, R_HCOUT07IEN);
        saved_usbien = REG8(base, R_USBIEN);
        REG8(base, R_HCIN07IEN)  = 0;
        REG8(base, R_HCOUT07IEN) = 0;
        REG8(base, R_USBIEN)     = 0;
        spin_unlock_irqrestore(&p->lock, flags);

        p->orig_driver->irq(hcd);

        spin_lock_irqsave(&p->lock, flags);
        if (port_wants_fs(p))
            REG8(base, R_BKDOOR) |= BKDOOR_HSDISABLE;
        else
            REG8(base, R_BKDOOR) &= (u8)~BKDOOR_HSDISABLE;
        if (REG8(base, R_OTGSTATE) != 3) {
            hrtimer_try_to_cancel(&p->poll_timer);
            if (p->active_urb)
                finish_active_urb_locked(p, -ENODEV);
            p->ls_auto_locked = 0;
            p->ls_auto_idx = 0;
            aotg_hw_quiesce_pipes(p);
        } else {
            REG8(base, R_HCIN07IEN)  = saved_inien;
            REG8(base, R_HCOUT07IEN) = saved_outien;
            REG8(base, R_USBIEN)     = saved_usbien;
            REG8(base, R_USBEIEN)   |= 0x80;
        }
        spin_unlock_irqrestore(&p->lock, flags);

        if (!(REG8(base, R_USBEIRQ) & 0x80))
            return IRQ_HANDLED;
    }

    REG8(base, R_USBEIRQ) = REG8(base, R_USBEIEN) | 0x80;

    spin_lock_irqsave(&p->lock, flags);

    usbirq = REG8(base, R_USBIRQ) & REG8(base, R_USBIEN);
    if (usbirq) {
        REG8(base, R_USBIRQ) = usbirq;
        if (usbirq & USBIRQ_URES) {
            u8 usbcs = REG8(base, R_USBCS);
            u32 port_st = *acthcd_port_ptr(hcd);
            port_st &= ~(USB_PORT_STAT_RESET | USB_PORT_STAT_LOW_SPEED | USB_PORT_STAT_HIGH_SPEED);
            port_st |= USB_PORT_STAT_ENABLE | (USB_PORT_STAT_C_RESET << 16);
            if (usbcs & 0x02) {
                port_st |= USB_PORT_STAT_HIGH_SPEED;
                *acthcd_speed_ptr(hcd) = USB_SPEED_HIGH;
                REG8(base, R_USBIRQ) = USBIRQ_HS;
            } else if (usbcs & 0x01) {
                port_st |= USB_PORT_STAT_LOW_SPEED;
                *acthcd_speed_ptr(hcd) = USB_SPEED_LOW;
            } else {
                *acthcd_speed_ptr(hcd) = USB_SPEED_FULL;
            }
            *acthcd_port_ptr(hcd) = port_st;
            aotg_hw_quiesce_pipes(p);
        }
    }

    in_err  = REG8(base, R_HCIN07ERRIRQ)  & REG8(base, R_HCIN07ERRIEN);
    out_err = REG8(base, R_HCOUT07ERRIRQ) & REG8(base, R_HCOUT07ERRIEN);
    if (in_err)
        REG8(base, R_HCIN07ERRIRQ) = in_err;
    if (out_err)
        REG8(base, R_HCOUT07ERRIRQ) = out_err;

    in_irq  = REG8(base, R_HCIN07IRQ)  & REG8(base, R_HCIN07IEN);
    out_irq = REG8(base, R_HCOUT07IRQ) & REG8(base, R_HCOUT07IEN);
    if (in_irq)
        REG8(base, R_HCIN07IRQ) = in_irq;
    if (out_irq)
        REG8(base, R_HCOUT07IRQ) = out_irq;

    if (!p->active_urb)
        goto out_unlock;

    /* Handle EP0 errors / completions */
    if (p->stage >= STAGE_EP0_SETUP && p->stage <= STAGE_EP0_STATUS_OUT) {
        if ((in_err & 0x01) || (out_err & 0x01)) {
            u8 er = (in_err & 0x01) ? REG8(base, R_HCIN0ERR) : REG8(base, R_HCOUT0ERR);
            int st = decode_ep_err(er);
            p->last_err_reg = er;
            if (st == 0) {
                REG8(base, (in_err & 0x01) ? R_HCIN0ERR : R_HCOUT0ERR) = EPERR_RESEND;
                goto out_unlock;
            }
            if (p->stage == STAGE_EP0_SETUP && st == -ETIMEDOUT && ls_preamble_bit < 0 &&
                !p->ls_auto_locked && *acthcd_speed_ptr(hcd) == USB_SPEED_FULL &&
                p->active_urb && p->active_urb->dev && p->active_urb->dev->speed == USB_SPEED_LOW) {
                p->ls_auto_idx = (u8)((p->ls_auto_idx + 1) % 5);
            }
            endprst_cmd(base, 0, 0, ENDPRST_FIFORST | ENDPRST_TOGRST);
            endprst_cmd(base, 0, 1, ENDPRST_FIFORST | ENDPRST_TOGRST);
            finish_active_urb_locked(p, st ? st : -EPROTO);
            aotg_hub_kick_locked(p);
            goto out_unlock;
        }

        if (p->stage == STAGE_EP0_SETUP && (out_irq & 0x01)) {
            struct urb *urb = p->active_urb;
            if (ls_preamble_bit < 0 && !p->ls_auto_locked &&
                *acthcd_speed_ptr(hcd) == USB_SPEED_FULL &&
                urb->dev && urb->dev->speed == USB_SPEED_LOW) {
                p->ls_auto_locked = 1;
                printk("<6>aotg_hub_hcd: auto-locked Low-Speed PRE bit=0x%02x on aotg%d\n",
                       current_ls_pre_bits(p), p->id);
            }
            hrtimer_try_to_cancel(&p->poll_timer);
            if (urb->transfer_buffer_length == 0) {
                p->stage = STAGE_EP0_STATUS_IN;
            } else if (usb_pipein(urb->pipe)) {
                p->stage = STAGE_EP0_IN;
            } else {
                p->stage = STAGE_EP0_OUT;
            }
            start_urb_step_locked(p, p->active_hep, urb);
            goto out_unlock;
        }

        if (p->stage == STAGE_EP0_IN && (in_irq & 0x01)) {
            struct urb *urb = p->active_urb;
            u16 maxp = p->active_hep->maxpacket ? p->active_hep->maxpacket : 8;
            u32 cnt = REG8(base, R_HCIN0BC);
            u32 rem = urb->transfer_buffer_length - urb->actual_length;
            u32 copy_len = (cnt <= rem) ? cnt : rem;
            hrtimer_try_to_cancel(&p->poll_timer);
            if (copy_len > 0) {
                ep0_read_buf(base, (u8 *)p->pio_bounce, copy_len);
                urb_copy_bytes(urb, urb->actual_length, p->pio_bounce, copy_len, 1);
                urb->actual_length += copy_len;
            }
            if (cnt > rem) {
                finish_active_urb_locked(p, -EOVERFLOW);
                aotg_hub_kick_locked(p);
                goto out_unlock;
            }
            if (urb->actual_length < urb->transfer_buffer_length && cnt == maxp) {
                start_urb_step_locked(p, p->active_hep, urb);
            } else {
                p->stage = STAGE_EP0_STATUS_OUT;
                start_urb_step_locked(p, p->active_hep, urb);
            }
            goto out_unlock;
        }

        if (p->stage == STAGE_EP0_OUT && (out_irq & 0x01)) {
            struct urb *urb = p->active_urb;
            hrtimer_try_to_cancel(&p->poll_timer);
            urb->actual_length += p->last_out_len;
            if (urb->actual_length < urb->transfer_buffer_length) {
                start_urb_step_locked(p, p->active_hep, urb);
            } else {
                p->stage = STAGE_EP0_STATUS_IN;
                start_urb_step_locked(p, p->active_hep, urb);
            }
            goto out_unlock;
        }

        if ((p->stage == STAGE_EP0_STATUS_OUT && (out_irq & 0x01)) ||
            (p->stage == STAGE_EP0_STATUS_IN  && (in_irq  & 0x01))) {
            finish_active_urb_locked(p, 0);
            aotg_hub_kick_locked(p);
            goto out_unlock;
        }
    }

    /* Handle Bulk / Interrupt IN & OUT */
    if (p->stage == STAGE_DATA_IN && p->active_chan) {
        u8 chan = p->active_chan;
        u8 mask = (u8)(1U << chan);
        /* Check in_irq / !(CS & EPCS_BUSY) BEFORE in_err: HCINAUTO can receive a valid packet
         * immediately after an earlier NAK latched in_err (er=0x03). */
        if ((in_irq & mask) || !(REG8(base, R_HCINxCS(chan)) & EPCS_BUSY)) {
            REG8(base, R_HCIN07IRQ) = mask;
            handle_active_in_done_locked(p);
            goto out_unlock;
        }
        if (in_err & mask) {
            u8 er = REG8(base, R_HCINxERR(chan));
            int st = decode_ep_err(er);
            p->last_err_reg = er;
            /* code == 0 (e.g. er=0x03) is CUSB2 hardware NAK notification — clean TDM yield! */
            if (st == 0) {
                REG8(base, R_HCINAUTO) &= (u8)~mask;
                REG8(base, R_HCINXSTART) &= (u8)~mask;
                dsb();
                aotg_udelay(15);
                if ((REG8(base, R_HCIN07IRQ) & mask) || !(REG8(base, R_HCINxCS(chan)) & EPCS_BUSY)) {
                    REG8(base, R_HCIN07IRQ) = mask;
                    handle_active_in_done_locked(p);
                    goto out_unlock;
                }
                hrtimer_try_to_cancel(&p->poll_timer);
                stop_in_chan(base, chan);
                if (p->active_hep->pipetype == PIPE_INTERRUPT)
                    p->active_hep->next_poll = ktime_add_ns(ktime_get(), ep_interval_ns(p->active_urb));
                else
                    p->nak_streak++;
                p->active_urb = NULL;
                p->active_hep = NULL;
                p->stage = STAGE_IDLE;
                p->active_chan = 0;
                p->err_retries = 0;
                aotg_hub_kick_locked(p);
                goto out_unlock;
            }
            /* Per stock 0xc02983d8..0xc02987a8: retry non-STALL bus errors in hardware via EPERR_RESEND */
            if (st != -EPIPE && p->err_retries < 10) {
                p->err_retries++;
                REG8(base, R_HCINxERR(chan)) = EPERR_RESEND;
                goto out_unlock;
            }
            stop_in_chan(base, chan);
            REG8(base, R_HCINxERR(chan)) = EPERR_RESEND;
            if (st == -EPIPE) {
                endprst_cmd(base, chan, 0, ENDPRST_FIFORST | ENDPRST_TOGRST);
                p->active_hep->toggle_valid = 0;
            } else {
                endprst_cmd(base, chan, 0, ENDPRST_FIFORST);
            }
            finish_active_urb_locked(p, st ? st : -EPROTO);
            aotg_hub_kick_locked(p);
            goto out_unlock;
        }
    }

    if (p->stage == STAGE_DATA_OUT && p->active_chan) {
        u8 chan = p->active_chan;
        u8 mask = (u8)(1U << chan);
        if (out_err & mask) {
            u8 er = REG8(base, R_HCOUTxERR(chan));
            int st = decode_ep_err(er);
            p->last_err_reg = er;
            if (st == 0 || (st != -EPIPE && p->err_retries < 10)) {
                if (st != 0)
                    p->err_retries++;
                REG8(base, R_HCOUTxERR(chan)) = EPERR_RESEND;
                goto out_unlock;
            }
            REG8(base, R_HCOUTxCON(chan)) &= (u8)~EPCON_VAL;
            REG8(base, R_HCOUTxERR(chan)) = EPERR_RESEND;
            if (st == -EPIPE) {
                endprst_cmd(base, chan, 1, ENDPRST_FIFORST | ENDPRST_TOGRST);
                p->active_hep->toggle_valid = 0;
            } else {
                endprst_cmd(base, chan, 1, ENDPRST_FIFORST);
            }
            finish_active_urb_locked(p, st ? st : -EPROTO);
            aotg_hub_kick_locked(p);
            goto out_unlock;
        }
        if (out_irq & mask) {
            struct urb *urb = p->active_urb;
            struct aotg_hub_ep *hep = p->active_hep;
            REG8(base, R_HCOUTxCON(chan)) &= (u8)~EPCON_VAL;
            p->nak_streak = 0;
            urb->actual_length += p->last_out_len;
            usb_dotoggle(hep->udev, hep->epnum, 1);
            hep->last_sw_toggle = usb_gettoggle(hep->udev, hep->epnum, 1);

            if (urb->actual_length < urb->transfer_buffer_length) {
                hrtimer_try_to_cancel(&p->poll_timer);
                p->active_urb = NULL;
                p->active_hep = NULL;
                p->stage = STAGE_IDLE;
                list_move(&hep->sched_node, (hep->pipetype == PIPE_INTERRUPT) ? &p->intr_eps : &p->bulk_eps);
                aotg_hub_kick_locked(p);
            } else if (p->zlp_pending && p->last_out_len > 0) {
                p->zlp_pending = 0;
                hrtimer_try_to_cancel(&p->poll_timer);
                start_urb_step_locked(p, hep, urb);
            } else {
                finish_active_urb_locked(p, 0);
                aotg_hub_kick_locked(p);
            }
        }
    }

out_unlock:
    spin_unlock_irqrestore(&p->lock, flags);
    return IRQ_HANDLED;
}

static enum hrtimer_restart aotg_poll_timer_fn(struct hrtimer *t)
{
    struct aotg_hub_port *p = container_of(t, struct aotg_hub_port, poll_timer);
    void __iomem *base = p->base;
    unsigned long flags;

    spin_lock_irqsave(&p->lock, flags);
    if (!p->active_urb)
        goto out;

    if (p->stage == STAGE_DATA_IN && p->active_chan) {
        u8 chan = p->active_chan;
        u8 mask = (u8)(1U << chan);
        REG8(base, R_HCINAUTO) &= (u8)~mask;
        REG8(base, R_HCINXSTART) &= (u8)~mask;
        dsb();
        /* Let any in-flight 12M FS HID packet (~10us) finish shifting into FIFO before stopping channel */
        aotg_udelay(15);
        if ((REG8(base, R_HCIN07IRQ) & mask) || !(REG8(base, R_HCINxCS(chan)) & EPCS_BUSY)) {
            REG8(base, R_HCIN07IRQ) = mask;
            handle_active_in_done_locked(p);
            goto out;
        }
        /* Clean NAK yield: stop IN token polling WITHOUT touching Data Toggle */
        stop_in_chan(base, chan);
        if (p->active_hep->pipetype == PIPE_INTERRUPT) {
            p->active_hep->next_poll = ktime_add_ns(ktime_get(), ep_interval_ns(p->active_urb));
        } else {
            p->nak_streak++;
        }
        p->active_urb = NULL;
        p->active_hep = NULL;
        p->stage = STAGE_IDLE;
        p->active_chan = 0;
        aotg_hub_kick_locked(p);
        goto out;
    }

    /* EP0 or OUT stage timed out (device unplugged or hung) */
    if (p->stage >= STAGE_EP0_SETUP && p->stage <= STAGE_EP0_STATUS_OUT) {
        if (p->stage == STAGE_EP0_SETUP && ls_preamble_bit < 0 && !p->ls_auto_locked &&
            *acthcd_speed_ptr(p->hcd) == USB_SPEED_FULL &&
            p->active_urb && p->active_urb->dev && p->active_urb->dev->speed == USB_SPEED_LOW) {
            p->ls_auto_idx = (u8)((p->ls_auto_idx + 1) % 5);
        }
        endprst_cmd(base, 0, 0, ENDPRST_FIFORST | ENDPRST_TOGRST);
        endprst_cmd(base, 0, 1, ENDPRST_FIFORST | ENDPRST_TOGRST);
    } else if (p->active_chan) {
        REG8(base, R_HCOUTxCON(p->active_chan)) &= (u8)~EPCON_VAL;
        endprst_cmd(base, p->active_chan, 1, ENDPRST_FIFORST);
    }
    finish_active_urb_locked(p, -ETIMEDOUT);
    aotg_hub_kick_locked(p);

out:
    spin_unlock_irqrestore(&p->lock, flags);
    return HRTIMER_NORESTART;
}

static int port_has_queued_urbs_locked(struct aotg_hub_port *p)
{
    struct aotg_hub_ep *hep;
    if (p->active_urb)
        return 1;
    list_for_each_entry(hep, &p->intr_eps, sched_node)
        if (!list_empty(&hep->ep->urb_list)) return 1;
    list_for_each_entry(hep, &p->ctrl_eps, sched_node)
        if (!list_empty(&hep->ep->urb_list)) return 1;
    list_for_each_entry(hep, &p->bulk_eps, sched_node)
        if (!list_empty(&hep->ep->urb_list)) return 1;
    return 0;
}

static enum hrtimer_restart aotg_sched_timer_fn(struct hrtimer *t)
{
    struct aotg_hub_port *p = container_of(t, struct aotg_hub_port, sched_timer);
    unsigned long flags, wait_ns;

    spin_lock_irqsave(&p->lock, flags);
    p->in_sched_cb = 1;
    p->sched_wait_ns = 0;
    aotg_hub_kick_locked(p);
    p->in_sched_cb = 0;
    wait_ns = p->sched_wait_ns;
    if (!wait_ns && port_has_queued_urbs_locked(p))
        wait_ns = 1000000UL; /* Keep 1ms SMP-safe heartbeat while any URB is in flight */
    if (wait_ns) {
        hrtimer_forward(t, ktime_get(), ktime_set(0, wait_ns));
        spin_unlock_irqrestore(&p->lock, flags);
        return HRTIMER_RESTART;
    }
    p->sched_running = 0;
    spin_unlock_irqrestore(&p->lock, flags);
    return HRTIMER_NORESTART;
}

static void aotg_bh_tasklet_fn(unsigned long data)
{
    struct aotg_hub_port *p = (struct aotg_hub_port *)data;
    unsigned long flags;

    spin_lock_irqsave(&p->lock, flags);
    while (!list_empty(&p->done_urbs)) {
        struct done_urb_entry *d = list_first_entry(&p->done_urbs, struct done_urb_entry, node);
        struct urb *urb = d->urb;
        int st = d->status;
        list_del(&d->node);
        /* Keep local IRQs disabled across usb_hcd_giveback_urb (required by Linux 3.10 USB core) */
        spin_unlock(&p->lock);

        kfree(d);
        usb_hcd_giveback_urb(p->hcd, urb, st);

        spin_lock(&p->lock);
    }
    aotg_hub_kick_locked(p);
    spin_unlock_irqrestore(&p->lock, flags);
}

static struct aotg_hub_ep *find_our_hep_locked(struct aotg_hub_port *p, struct usb_host_endpoint *ep)
{
    struct aotg_hub_ep *hep;
    if (!ep || !ep->hcpriv)
        return NULL;
    list_for_each_entry(hep, &p->ctrl_eps, sched_node) {
        if (hep->ep == ep)
            return hep;
    }
    list_for_each_entry(hep, &p->intr_eps, sched_node) {
        if (hep->ep == ep)
            return hep;
    }
    list_for_each_entry(hep, &p->bulk_eps, sched_node) {
        if (hep->ep == ep)
            return hep;
    }
    return NULL;
}

static int aotg_hub_urb_enqueue(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags)
{
    struct aotg_hub_port *p = (hcd == g_ports[0].hcd) ? &g_ports[0] : &g_ports[1];
    struct usb_host_endpoint *ep = urb->ep;
    struct aotg_hub_ep *hep;
    unsigned long flags;
    int ret;

    if (!ep || !urb->dev)
        return -EINVAL;
    /* shortcut: no isochronous USB audio/uvc support, add ISO FIFO mode if needed */
    if (usb_pipetype(urb->pipe) == PIPE_ISOCHRONOUS)
        return -ENOSYS;

    spin_lock_irqsave(&p->lock, flags);
    if (REG8(p->base, R_OTGSTATE) != 3 && !(*acthcd_port_ptr(hcd) & USB_PORT_STAT_CONNECTION)) {
        spin_unlock_irqrestore(&p->lock, flags);
        return -ENODEV;
    }

    hep = find_our_hep_locked(p, ep);
    if (!hep) {
        if (ep->hcpriv) {
            /* Pre-existing endpoint from built-in aotg_hcd is still disconnecting */
            spin_unlock_irqrestore(&p->lock, flags);
            return -ENODEV;
        }
        spin_unlock_irqrestore(&p->lock, flags);
        /* Keep HID mice/keyboards/Wi-Fi/flash permanently awake (skip 1-byte hub status pipe during hub_probe) */
        if ((mem_flags & 0x10U) && urb->dev->devnum > 0 &&
            usb_pipetype(urb->pipe) != PIPE_CONTROL && (ep->desc.wMaxPacketSize & 0x07FFU) > 1)
            usb_disable_autosuspend(urb->dev);
        hep = (struct aotg_hub_ep *)__kmalloc(sizeof(*hep), mem_flags);
        if (!hep)
            return -ENOMEM;
        memset(hep, 0, sizeof(*hep));
        INIT_LIST_HEAD(&hep->sched_node);
        hep->ep = ep;
        hep->udev = urb->dev;
        hep->epnum = (u8)usb_pipeendpoint(urb->pipe);
        hep->pipetype = (u8)usb_pipetype(urb->pipe);
        hep->is_out = (u8)usb_pipeout(urb->pipe);
        hep->maxpacket = (u16)(ep->desc.wMaxPacketSize & 0x07FFU);
        if (!hep->maxpacket)
            hep->maxpacket = (hep->pipetype == PIPE_CONTROL) ? 8 : 64;
        if (hep->maxpacket > 512)
            hep->maxpacket = 512;

        spin_lock_irqsave(&p->lock, flags);
        if (ep->hcpriv) {
            kfree(hep);
            hep = find_our_hep_locked(p, ep);
            if (!hep) {
                spin_unlock_irqrestore(&p->lock, flags);
                return -ENODEV;
            }
        } else {
            ep->hcpriv = hep;
            if (hep->pipetype == PIPE_CONTROL)
                list_add_tail(&hep->sched_node, &p->ctrl_eps);
            else if (hep->pipetype == PIPE_INTERRUPT)
                list_add_tail(&hep->sched_node, &p->intr_eps);
            else
                list_add_tail(&hep->sched_node, &p->bulk_eps);
        }
    } else {
        u16 cur_maxp = (u16)(ep->desc.wMaxPacketSize & 0x07FFU);
        hep->udev = urb->dev;
        if (cur_maxp && cur_maxp <= 512 && cur_maxp != hep->maxpacket) {
            hep->maxpacket = cur_maxp;
            hep->toggle_valid = 0;
        }
    }

    /* Reject USB_PORT_FEAT_SUSPEND with -EBUSY so Linux keeps hub & downstream devices active */
    if (block_port_suspend && usb_pipetype(urb->pipe) == PIPE_CONTROL && urb->setup_packet &&
        urb->setup_packet[0] == 0x23 && urb->setup_packet[1] == 0x03 &&
        urb->setup_packet[2] == 0x02 && urb->setup_packet[3] == 0x00) {
        spin_unlock_irqrestore(&p->lock, flags);
        return -EBUSY;
    }

    ret = usb_hcd_link_urb_to_ep(hcd, urb);
    if (ret) {
        spin_unlock_irqrestore(&p->lock, flags);
        return ret;
    }

    urb->hcpriv = hep;
    if (hep->pipetype != PIPE_INTERRUPT)
        p->nak_streak = 0;

    aotg_hub_kick_locked(p);
    spin_unlock_irqrestore(&p->lock, flags);
    return 0;
}

static int aotg_hub_urb_dequeue(struct usb_hcd *hcd, struct urb *urb, int status)
{
    struct aotg_hub_port *p = (hcd == g_ports[0].hcd) ? &g_ports[0] : &g_ports[1];
    unsigned long flags;
    int ret;

    spin_lock_irqsave(&p->lock, flags);
    ret = usb_hcd_check_unlink_urb(hcd, urb, status);
    if (ret) {
        spin_unlock_irqrestore(&p->lock, flags);
        return ret;
    }

    if (p->active_urb == urb) {
        if (p->stage == STAGE_DATA_IN && p->active_chan)
            stop_in_chan(p->base, p->active_chan);
        else if (p->stage == STAGE_DATA_OUT && p->active_chan)
            endprst_cmd(p->base, p->active_chan, 1, ENDPRST_FIFORST);
        else {
            endprst_cmd(p->base, 0, 0, ENDPRST_FIFORST | ENDPRST_TOGRST);
            endprst_cmd(p->base, 0, 1, ENDPRST_FIFORST | ENDPRST_TOGRST);
        }
        finish_active_urb_locked(p, status);
        aotg_hub_kick_locked(p);
    } else {
        queue_urb_done_locked(p, urb, status);
    }

    spin_unlock_irqrestore(&p->lock, flags);
    return 0;
}

static void aotg_hub_endpoint_disable(struct usb_hcd *hcd, struct usb_host_endpoint *ep)
{
    struct aotg_hub_port *p = (hcd == g_ports[0].hcd) ? &g_ports[0] : &g_ports[1];
    struct aotg_hub_ep *hep;
    unsigned long flags;

    spin_lock_irqsave(&p->lock, flags);
    hep = find_our_hep_locked(p, ep);
    if (!hep) {
        int had_foreign_hcpriv = (ep && ep->hcpriv != NULL);
        spin_unlock_irqrestore(&p->lock, flags);
        if (had_foreign_hcpriv && p->orig_driver->endpoint_disable) {
            p->orig_driver->endpoint_disable(hcd, ep);
            spin_lock_irqsave(&p->lock, flags);
            aotg_hw_quiesce_pipes(p);
            spin_unlock_irqrestore(&p->lock, flags);
        }
        return;
    }

    if (p->active_hep == hep && p->active_urb) {
        if (p->stage == STAGE_DATA_IN && p->active_chan)
            stop_in_chan(p->base, p->active_chan);
        finish_active_urb_locked(p, -ESHUTDOWN);
    }

    while (!list_empty(&ep->urb_list)) {
        struct urb *urb = list_first_entry(&ep->urb_list, struct urb, urb_list);
        queue_urb_done_locked(p, urb, -ESHUTDOWN);
    }

    if (hep->hw_chan >= 1 && hep->hw_chan <= 5) {
        struct hw_chan_slot *slots = hep->is_out ? p->out_slots : p->in_slots;
        if (slots[hep->hw_chan - 1].owner == hep)
            slots[hep->hw_chan - 1].owner = NULL;
    }

    list_del_init(&hep->sched_node);
    ep->hcpriv = NULL;
    spin_unlock_irqrestore(&p->lock, flags);

    kfree(hep);
    tasklet_schedule(&p->bh_tasklet);
}

static int aotg_hub_map_dma_noop(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags)
{
    (void)hcd;
    (void)urb;
    (void)mem_flags;
    return 0;
}

static void aotg_hub_unmap_dma_noop(struct usb_hcd *hcd, struct urb *urb)
{
    (void)hcd;
    (void)urb;
}

static int aotg_hub_bus_suspend(struct usb_hcd *hcd)
{
    (void)hcd;
    return block_port_suspend ? -EBUSY : 0;
}

static int aotg_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
                            u16 wIndex, char *buf, u16 wLength)
{
    struct aotg_hub_port *p = (hcd == g_ports[0].hcd) ? &g_ports[0] : &g_ports[1];
    unsigned long flags;

    /* Prevent root-hub bus autosuspend from putting external USB 2.0 hub to sleep */
    if (block_port_suspend && typeReq == 0x2303 && wValue == 2)
        return -EBUSY;

    /* shortcut: no HS Split-TT state machine, uses BKDOOR 12M FS hub or dual-port HS+FS */
    if (typeReq == 0x2303 && wValue == 4) {
        spin_lock_irqsave(&p->lock, flags);
        if (p->active_urb)
            finish_active_urb_locked(p, -ESHUTDOWN);
        if (port_wants_fs(p))
            REG8(p->base, R_BKDOOR) |= BKDOOR_HSDISABLE;
        else
            REG8(p->base, R_BKDOOR) &= (u8)~BKDOOR_HSDISABLE;
        aotg_hw_quiesce_pipes(p);
        spin_unlock_irqrestore(&p->lock, flags);
    }

    return p->orig_driver->hub_control(hcd, typeReq, wValue, wIndex, buf, wLength);
}

static int attach_one_port(int id)
{
    const char *devname = (id == 0) ? "aotg_hcd.0" : "aotg_hcd.1";
    struct aotg_hub_port *p = &g_ports[id];
    struct device *dev;
    struct usb_hcd *hcd;
    void __iomem *raw_base;
    unsigned long flags;

    dev = bus_find_device_by_name(&platform_bus_type, NULL, devname);
    if (!dev)
        return 0;

    hcd = (struct usb_hcd *)dev_get_drvdata(dev);
    if (!hcd || !hcd->driver) {
        put_device(dev);
        return 0;
    }

    raw_base = *(void __iomem **)((u8 *)hcd + 0xF0);
    p->id = id;
    p->pdev_dev = dev;
    p->hcd = hcd;
    p->base = AOTG_IO_ADDR(raw_base);
    p->orig_driver = hcd->driver;

    spin_lock_init(&p->lock);
    INIT_LIST_HEAD(&p->ctrl_eps);
    INIT_LIST_HEAD(&p->intr_eps);
    INIT_LIST_HEAD(&p->bulk_eps);
    INIT_LIST_HEAD(&p->done_urbs);

    tasklet_init(&p->bh_tasklet, aotg_bh_tasklet_fn, (unsigned long)p);
    hrtimer_init(&p->poll_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    p->poll_timer.function = aotg_poll_timer_fn;
    hrtimer_init(&p->sched_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    p->sched_timer.function = aotg_sched_timer_fn;

    memcpy(&p->patched_driver, p->orig_driver, sizeof(p->patched_driver));
    p->patched_driver.product_desc    = "Actions AOTG Multi-Device Hub TDM HCD";
    p->patched_driver.irq             = aotg_hub_irq;
    p->patched_driver.urb_enqueue     = aotg_hub_urb_enqueue;
    p->patched_driver.urb_dequeue     = aotg_hub_urb_dequeue;
    p->patched_driver.map_urb_for_dma = aotg_hub_map_dma_noop;
    p->patched_driver.unmap_urb_for_dma = aotg_hub_unmap_dma_noop;
    p->patched_driver.endpoint_disable = aotg_hub_endpoint_disable;
    p->patched_driver.hub_control     = aotg_hub_control;
    p->patched_driver.bus_suspend     = aotg_hub_bus_suspend;

    /* Stop stock 500ms check_trb_timer (verified @ hcd + 0x1F0) so it never toggles uhc_irq */
    hrtimer_cancel((struct hrtimer *)((u8 *)hcd + 0x1F0));

    spin_lock_irqsave(&p->lock, flags);
    if (port_wants_fs(p))
        REG8(p->base, R_BKDOOR) |= BKDOOR_HSDISABLE;
    else
        REG8(p->base, R_BKDOOR) &= (u8)~BKDOOR_HSDISABLE;
    aotg_hw_quiesce_pipes(p);
    hcd->driver = &p->patched_driver;
    p->attached = 1;

    /* If a device/hub is already plugged in, trigger root port status change so Linux re-enumerates it */
    if (*acthcd_port_ptr(hcd) & USB_PORT_STAT_CONNECTION) {
        *acthcd_port_ptr(hcd) &= ~(USB_PORT_STAT_ENABLE | USB_PORT_STAT_HIGH_SPEED | USB_PORT_STAT_LOW_SPEED);
        *acthcd_port_ptr(hcd) |= (USB_PORT_STAT_C_CONNECTION << 16);
        spin_unlock_irqrestore(&p->lock, flags);
        usb_hcd_poll_rh_status(hcd);
    } else {
        spin_unlock_irqrestore(&p->lock, flags);
    }

    printk("<6>aotg_hub_hcd: attached to %s (base=%p, mode=%s)\n",
           devname, p->base, port_wants_fs(p) ? "Full-Speed 12M Hub-No-TT" : "High-Speed 480M");
    return 1;
}

static void flush_ep_list_locked(struct aotg_hub_port *p, struct list_head *head)
{
    struct aotg_hub_ep *hep, *tmp;
    list_for_each_entry_safe(hep, tmp, head, sched_node) {
        while (!list_empty(&hep->ep->urb_list)) {
            struct urb *urb = list_first_entry(&hep->ep->urb_list, struct urb, urb_list);
            queue_urb_done_locked(p, urb, -ESHUTDOWN);
        }
        hep->ep->hcpriv = NULL;
        list_del_init(&hep->sched_node);
        kfree(hep);
    }
}

static void free_ep_list_stale(struct list_head *head)
{
    struct aotg_hub_ep *hep, *tmp;
    list_for_each_entry_safe(hep, tmp, head, sched_node) {
        list_del_init(&hep->sched_node);
        kfree(hep);
    }
}

static void detach_one_port(int id)
{
    const char *devname = (id == 0) ? "aotg_hcd.0" : "aotg_hcd.1";
    struct aotg_hub_port *p = &g_ports[id];
    struct device *cur_dev;
    int hcd_alive = 0;
    unsigned long flags;
    int was_connected = 0;

    if (!p->attached)
        return;

    cur_dev = bus_find_device_by_name(&platform_bus_type, NULL, devname);
    if (cur_dev) {
        if (cur_dev == p->pdev_dev && dev_get_drvdata(cur_dev) == p->hcd)
            hcd_alive = 1;
        put_device(cur_dev);
    }

    if (hcd_alive) {
        spin_lock_irqsave(&p->lock, flags);
        if (p->active_urb)
            finish_active_urb_locked(p, -ESHUTDOWN);
        flush_ep_list_locked(p, &p->ctrl_eps);
        flush_ep_list_locked(p, &p->intr_eps);
        flush_ep_list_locked(p, &p->bulk_eps);
        REG8(p->base, R_BKDOOR) &= (u8)~BKDOOR_HSDISABLE;
        aotg_hw_quiesce_pipes(p);
        p->hcd->driver = p->orig_driver;
        p->attached = 0;
        if (*acthcd_port_ptr(p->hcd) & USB_PORT_STAT_CONNECTION) {
            *acthcd_port_ptr(p->hcd) &= ~(USB_PORT_STAT_ENABLE | USB_PORT_STAT_HIGH_SPEED | USB_PORT_STAT_LOW_SPEED);
            *acthcd_port_ptr(p->hcd) |= (USB_PORT_STAT_C_CONNECTION << 16);
            was_connected = 1;
        }
        spin_unlock_irqrestore(&p->lock, flags);
    } else {
        /* aotg_dev1_onoff already unregistered & freed the old hcd after physical unplug */
        p->attached = 0;
        p->active_urb = NULL;
        p->active_hep = NULL;
        free_ep_list_stale(&p->ctrl_eps);
        free_ep_list_stale(&p->intr_eps);
        free_ep_list_stale(&p->bulk_eps);
    }

    hrtimer_cancel(&p->poll_timer);
    hrtimer_cancel(&p->sched_timer);
    tasklet_kill(&p->bh_tasklet);
    if (hcd_alive && was_connected)
        usb_hcd_poll_rh_status(p->hcd);
    put_device(p->pdev_dev);
    printk("<6>aotg_hub_hcd: detached from %s (alive=%d)\n", devname, hcd_alive);
}

int init_module(void)
{
    int count = 0;
    memset(g_ports, 0, sizeof(g_ports));
    if (port_mask & 1)
        count += attach_one_port(0);
    if (port_mask & 2)
        count += attach_one_port(1);
    if (!count) {
        printk("<4>aotg_hub_hcd: no active aotg_hcd.{0,1} host devices found (run 'usb-mode host' first if using aotg0)\n");
        return -ENODEV;
    }
    return 0;
}

void cleanup_module(void)
{
    detach_one_port(1);
    detach_one_port(0);
}

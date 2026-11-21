/*
 * aotg_kabi.h — Exact binary-verified kernel ABI definitions for
 * Actions ATM7051H (ATS3605) Linux 3.10.52 SMP preempt mod_unload ARMv7 p2v8.
 * Every struct size & field offset is verified at compile time against
 * disassembled build/firmware-inspection/misc/kernel.uncompressed.
 */
#ifndef _AOTG_KABI_H_
#define _AOTG_KABI_H_

#include <stddef.h>
#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;
typedef u32      gfp_t;

#define __iomem
#define GFP_ATOMIC 0x20U
#define GFP_KERNEL 0xD0U

#define EINVAL     22
#define ENODEV     19
#define ENOMEM     12
#define ENOSPC     28
#define EPIPE      32
#define ENOSYS     38
#define EPROTO     71
#define EOVERFLOW  75
#define ESHUTDOWN  108
#define ETIMEDOUT  110
#define EREMOTEIO  121

#define dsb() __asm__ __volatile__ ("dsb sy" : : : "memory")

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#define ABI_EQ(a, b) _Static_assert((a) == (b), #a " != " #b)

/* Standard doubly-linked list */
struct list_head {
    struct list_head *next, *prev;
};

static inline void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

static inline int list_empty(const struct list_head *head)
{
    return head->next == head;
}

static inline void __list_add(struct list_head *n, struct list_head *prev, struct list_head *next)
{
    next->prev = n;
    n->next = next;
    n->prev = prev;
    prev->next = n;
}

static inline void list_add(struct list_head *n, struct list_head *head)
{
    __list_add(n, head, head->next);
}

static inline void list_add_tail(struct list_head *n, struct list_head *head)
{
    __list_add(n, head->prev, head);
}

static inline void __list_del(struct list_head *prev, struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

static inline void list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    entry->next = (struct list_head *)0x00100100;
    entry->prev = (struct list_head *)0x00200200;
}

static inline void list_del_init(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    INIT_LIST_HEAD(entry);
}

static inline void list_move(struct list_head *list, struct list_head *head)
{
    __list_del(list->prev, list->next);
    list_add(list, head);
}

static inline void list_move_tail(struct list_head *list, struct list_head *head)
{
    __list_del(list->prev, list->next);
    list_add_tail(list, head);
}

#define list_first_entry(ptr, type, member) \
    container_of((ptr)->next, type, member)

#define list_for_each_entry(pos, head, member) \
    for (pos = container_of((head)->next, __typeof__(*pos), member); \
         &pos->member != (head); \
         pos = container_of(pos->member.next, __typeof__(*pos), member))

#define list_for_each_entry_safe(pos, n, head, member) \
    for (pos = container_of((head)->next, __typeof__(*pos), member), \
         n = container_of(pos->member.next, __typeof__(*pos), member); \
         &pos->member != (head); \
         pos = n, n = container_of(n->member.next, __typeof__(*n), member))

/* Spinlock (4 bytes on ARMv7 SMP without DEBUG_SPINLOCK) */
typedef struct { u32 raw_lock; } spinlock_t;
ABI_EQ(sizeof(spinlock_t), 4);

extern unsigned long _raw_spin_lock_irqsave(spinlock_t *lock);
extern void _raw_spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags);
extern void _raw_spin_lock(spinlock_t *lock);
extern void _raw_spin_unlock(spinlock_t *lock);

static inline void spin_lock_init(spinlock_t *lock) { lock->raw_lock = 0; }
#define spin_lock_irqsave(lock, flags)    do { (flags) = _raw_spin_lock_irqsave(lock); } while (0)
#define spin_unlock_irqrestore(lock, fl)  _raw_spin_unlock_irqrestore((lock), (fl))
#define spin_lock(lock)                   _raw_spin_lock(lock)
#define spin_unlock(lock)                 _raw_spin_unlock(lock)

/* Memory & String */
extern void *__kmalloc(size_t size, gfp_t flags);
extern void kfree(const void *objp);
extern int printk(const char *fmt, ...);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memset(void *s, int c, size_t n);

/* ARM delay ops */
struct arm_delay_ops {
    void (*delay)(unsigned long);
    void (*const_udelay)(unsigned long);
    void (*udelay)(unsigned long);
    unsigned long ticks_per_jiffy;
};
extern struct arm_delay_ops arm_delay_ops;

/* Scatterlist & HighMem atomic kmap */
struct page;
struct scatterlist {
    unsigned long page_link;
    unsigned int  offset;
    unsigned int  length;
    u32           dma_address;
};
ABI_EQ(offsetof(struct scatterlist, offset), 4);
ABI_EQ(offsetof(struct scatterlist, length), 8);

extern struct scatterlist *sg_next(struct scatterlist *sg);
extern void *kmap_atomic(struct page *page);
extern void __kunmap_atomic(void *kvaddr);

static inline struct page *sg_page(struct scatterlist *sg)
{
    return (struct page *)(sg->page_link & ~3UL);
}

/* Tasklet (20 bytes, verified at 0xc0025aa4) */
struct tasklet_struct {
    struct tasklet_struct *next;
    unsigned long state;
    u32 count;
    void (*func)(unsigned long);
    unsigned long data;
};
ABI_EQ(sizeof(struct tasklet_struct), 0x14);
ABI_EQ(offsetof(struct tasklet_struct, func), 0x0c);

extern void tasklet_init(struct tasklet_struct *t, void (*func)(unsigned long), unsigned long data);
extern void tasklet_kill(struct tasklet_struct *t);
extern void __tasklet_schedule(struct tasklet_struct *t);

static inline void tasklet_schedule(struct tasklet_struct *t)
{
    if (!(__sync_fetch_and_or(&t->state, 1UL) & 1UL))
        __tasklet_schedule(t);
}

/* High-resolution timer (48 bytes, function at +0x20, verified at 0xc02974d4..0xc0297504) */
typedef union { s64 tv64; } ktime_t;

enum hrtimer_mode {
    HRTIMER_MODE_ABS = 0x0,
    HRTIMER_MODE_REL = 0x1,
};
enum hrtimer_restart {
    HRTIMER_NORESTART = 0,
    HRTIMER_RESTART = 1,
};
#define CLOCK_MONOTONIC 1

struct hrtimer {
    u32 _rb_node[3];
    ktime_t _expires;
    ktime_t _softexpires;
    enum hrtimer_restart (*function)(struct hrtimer *);
    void *base;
    unsigned long state;
} __attribute__((aligned(8)));
ABI_EQ(offsetof(struct hrtimer, function), 0x20);
ABI_EQ(sizeof(struct hrtimer), 0x30);

extern ktime_t ktime_get(void);
extern void hrtimer_init(struct hrtimer *timer, int clock_id, enum hrtimer_mode mode);
extern int hrtimer_start(struct hrtimer *timer, ktime_t tim, const enum hrtimer_mode mode);
extern u64 hrtimer_forward(struct hrtimer *timer, ktime_t now, ktime_t interval);
extern int hrtimer_cancel(struct hrtimer *timer);
extern int hrtimer_try_to_cancel(struct hrtimer *timer);

static inline ktime_t ktime_set(const long secs, const unsigned long nsecs)
{
    ktime_t kt;
    kt.tv64 = (s64)secs * 1000000000LL + (s64)nsecs;
    return kt;
}

static inline ktime_t ktime_add_ns(const ktime_t kt, u64 nsec)
{
    ktime_t res;
    res.tv64 = kt.tv64 + (s64)nsec;
    return res;
}

/* Device model */
struct device;
struct bus_type;
extern struct bus_type platform_bus_type;
extern struct device *bus_find_device_by_name(struct bus_type *bus, struct device *start, const char *name);
extern void *dev_get_drvdata(const struct device *dev);
extern void put_device(struct device *dev);

/* IRQ */
typedef int irqreturn_t;
#define IRQ_NONE    0
#define IRQ_HANDLED 1

/* USB constants & binary-verified core structs */
#define USB_SPEED_LOW  1
#define USB_SPEED_FULL 2
#define USB_SPEED_HIGH 3

#define PIPE_ISOCHRONOUS 0
#define PIPE_INTERRUPT   1
#define PIPE_CONTROL     2
#define PIPE_BULK        3

#define usb_pipetype(pipe)     (((pipe) >> 30) & 3)
#define usb_pipeendpoint(pipe) (((pipe) >> 15) & 0x0f)
#define usb_pipedevice(pipe)   (((pipe) >> 8) & 0x7f)
#define usb_pipein(pipe)       (((pipe) & 0x80) != 0)
#define usb_pipeout(pipe)      (((pipe) & 0x80) == 0)

#define URB_SHORT_NOT_OK 0x0001
#define URB_ZERO_PACKET  0x0040

#define USB_PORT_STAT_CONNECTION  0x0001
#define USB_PORT_STAT_ENABLE      0x0002
#define USB_PORT_STAT_RESET       0x0010
#define USB_PORT_STAT_LOW_SPEED   0x0200
#define USB_PORT_STAT_HIGH_SPEED  0x0400
#define USB_PORT_STAT_C_CONNECTION 0x0001
#define USB_PORT_STAT_C_RESET     0x0010

struct usb_endpoint_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u8  bEndpointAddress;
    u8  bmAttributes;
    u16 wMaxPacketSize;
    u8  bInterval;
    u8  bRefresh;
    u8  bSynchAddress;
} __attribute__((packed));

struct usb_host_endpoint {
    struct usb_endpoint_descriptor desc;
    u8 _pad_desc[7]; /* desc is 9B; ss_ep_comp (6B) + pad -> urb_list at +0x10 */
    struct list_head urb_list;
    void *hcpriv;
    void *ep_dev;
    unsigned char *extra;
    int extralen;
    int enabled;
};
ABI_EQ(offsetof(struct usb_host_endpoint, desc.wMaxPacketSize), 0x04);
ABI_EQ(offsetof(struct usb_host_endpoint, urb_list), 0x10);
ABI_EQ(offsetof(struct usb_host_endpoint, hcpriv), 0x18);
ABI_EQ(offsetof(struct usb_host_endpoint, enabled), 0x28);

struct usb_device {
    int devnum;
    char devpath[16];
    u32 route;
    int state;
    int speed;
    void *tt;
    int ttport;
    unsigned int toggle[2];
    void *parent;
    void *bus;
};
ABI_EQ(offsetof(struct usb_device, devnum), 0x00);
ABI_EQ(offsetof(struct usb_device, speed), 0x1c);
ABI_EQ(offsetof(struct usb_device, toggle), 0x28);
ABI_EQ(offsetof(struct usb_device, bus), 0x34);

#define usb_gettoggle(dev, ep, out) (((dev)->toggle[out] >> (ep)) & 1)
#define	usb_dotoggle(dev, ep, out)  ((dev)->toggle[out] ^= (1U << (ep)))
#define usb_settoggle(dev, ep, out, bit) \
    ((dev)->toggle[out] = ((dev)->toggle[out] & ~(1U << (ep))) | ((bit) << (ep)))

struct urb;
typedef void (*usb_complete_t)(struct urb *);

struct urb {
    u32 _kref_unlinked;          /* +0x00 */
    void *hcpriv;                /* +0x04 */
    u32 _atomic_use_reject[3];   /* +0x08..0x13 */
    struct list_head urb_list;   /* +0x14..0x1b */
    struct list_head anchor_list;/* +0x1c..0x23 */
    void *anchor;                /* +0x24 */
    struct usb_device *dev;      /* +0x28 */
    struct usb_host_endpoint *ep;/* +0x2c */
    unsigned int pipe;           /* +0x30 */
    unsigned int stream_id;      /* +0x34 */
    int status;                  /* +0x38 */
    unsigned int transfer_flags; /* +0x3c */
    void *transfer_buffer;       /* +0x40 */
    u32 transfer_dma;            /* +0x44 */
    struct scatterlist *sg;      /* +0x48 */
    int num_mapped_sgs;          /* +0x4c */
    int num_sgs;                 /* +0x50 */
    u32 transfer_buffer_length;  /* +0x54 */
    u32 actual_length;           /* +0x58 */
    unsigned char *setup_packet; /* +0x5c */
    u32 setup_dma;               /* +0x60 */
    int start_frame;             /* +0x64 */
    int number_of_packets;       /* +0x68 */
    int interval;                /* +0x6c */
    int error_count;             /* +0x70 */
    void *context;               /* +0x74 */
    usb_complete_t complete;     /* +0x78 */
};
ABI_EQ(offsetof(struct urb, hcpriv), 0x04);
ABI_EQ(offsetof(struct urb, urb_list), 0x14);
ABI_EQ(offsetof(struct urb, dev), 0x28);
ABI_EQ(offsetof(struct urb, ep), 0x2c);
ABI_EQ(offsetof(struct urb, pipe), 0x30);
ABI_EQ(offsetof(struct urb, status), 0x38);
ABI_EQ(offsetof(struct urb, transfer_flags), 0x3c);
ABI_EQ(offsetof(struct urb, transfer_buffer), 0x40);
ABI_EQ(offsetof(struct urb, sg), 0x48);
ABI_EQ(offsetof(struct urb, num_sgs), 0x50);
ABI_EQ(offsetof(struct urb, transfer_buffer_length), 0x54);
ABI_EQ(offsetof(struct urb, actual_length), 0x58);
ABI_EQ(offsetof(struct urb, setup_packet), 0x5c);
ABI_EQ(offsetof(struct urb, interval), 0x6c);
ABI_EQ(offsetof(struct urb, complete), 0x78);

struct usb_hcd;

struct hc_driver {
    const char *description;                                      /* +0x00 */
    const char *product_desc;                                     /* +0x04 */
    size_t hcd_priv_size;                                         /* +0x08 */
    irqreturn_t (*irq)(struct usb_hcd *hcd);                      /* +0x0c */
    int flags;                                                    /* +0x10 */
    int (*reset)(struct usb_hcd *hcd);                            /* +0x14 */
    int (*start)(struct usb_hcd *hcd);                            /* +0x18 */
    int (*pci_suspend)(struct usb_hcd *hcd, int do_wakeup);       /* +0x1c */
    int (*pci_resume)(struct usb_hcd *hcd, int hibernated);       /* +0x20 */
    void (*stop)(struct usb_hcd *hcd);                            /* +0x24 */
    void (*shutdown)(struct usb_hcd *hcd);                        /* +0x28 */
    int (*get_frame_number)(struct usb_hcd *hcd);                 /* +0x2c */
    int (*urb_enqueue)(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags); /* +0x30 */
    int (*urb_dequeue)(struct usb_hcd *hcd, struct urb *urb, int status);      /* +0x34 */
    int (*map_urb_for_dma)(struct usb_hcd *hcd, struct urb *urb, gfp_t mem_flags); /* +0x38 */
    void (*unmap_urb_for_dma)(struct usb_hcd *hcd, struct urb *urb);           /* +0x3c */
    void (*endpoint_disable)(struct usb_hcd *hcd, struct usb_host_endpoint *ep); /* +0x40 */
    void (*endpoint_reset)(struct usb_hcd *hcd, struct usb_host_endpoint *ep);   /* +0x44 */
    int (*hub_status_data)(struct usb_hcd *hcd, char *buf);       /* +0x48 */
    int (*hub_control)(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
                       u16 wIndex, char *buf, u16 wLength);       /* +0x4c */
    int (*bus_suspend)(struct usb_hcd *hcd);                      /* +0x50 */
    int (*bus_resume)(struct usb_hcd *hcd);                       /* +0x54 */
    u32 _tail_callbacks[18];                                      /* +0x58..0x9c */
};
ABI_EQ(offsetof(struct hc_driver, irq), 0x0c);
ABI_EQ(offsetof(struct hc_driver, start), 0x18);
ABI_EQ(offsetof(struct hc_driver, stop), 0x24);
ABI_EQ(offsetof(struct hc_driver, get_frame_number), 0x2c);
ABI_EQ(offsetof(struct hc_driver, urb_enqueue), 0x30);
ABI_EQ(offsetof(struct hc_driver, urb_dequeue), 0x34);
ABI_EQ(offsetof(struct hc_driver, map_urb_for_dma), 0x38);
ABI_EQ(offsetof(struct hc_driver, unmap_urb_for_dma), 0x3c);
ABI_EQ(offsetof(struct hc_driver, endpoint_disable), 0x40);
ABI_EQ(offsetof(struct hc_driver, hub_status_data), 0x48);
ABI_EQ(offsetof(struct hc_driver, hub_control), 0x4c);

struct usb_hcd {
    u8 _usb_bus[0x9c];
    const struct hc_driver *driver; /* +0x9c */
    unsigned long flags;            /* +0xa0 */
    int state;                      /* +0xa4 */
    u32 _rh_fields[2];              /* +0xa8..0xaf */
    void __iomem *regs;             /* +0xb0 */
    u32 rsrc_start;                 /* +0xb4 */
    u32 rsrc_len;                   /* +0xb8 */
    u8 _tail[0x24];                 /* +0xbc..0xdf */
};
ABI_EQ(offsetof(struct usb_hcd, driver), 0x9c);
ABI_EQ(offsetof(struct usb_hcd, state), 0xa4);
ABI_EQ(offsetof(struct usb_hcd, regs), 0xb0);
ABI_EQ(sizeof(struct usb_hcd), 0xe0);

extern int usb_hcd_link_urb_to_ep(struct usb_hcd *hcd, struct urb *urb);
extern void usb_hcd_unlink_urb_from_ep(struct usb_hcd *hcd, struct urb *urb);
extern int usb_hcd_check_unlink_urb(struct usb_hcd *hcd, struct urb *urb, int status);
extern void usb_hcd_giveback_urb(struct usb_hcd *hcd, struct urb *urb, int status);
extern void usb_hcd_poll_rh_status(struct usb_hcd *hcd);

/* Module & kernel_param metadata (verified 0x150 module size & 16-byte __param entry) */
struct kernel_param_ops;
extern struct kernel_param_ops param_ops_int;

struct kernel_param {
    const char *name;
    const struct kernel_param_ops *ops;
    u16 perm;
    s16 level;
    void *arg;
};
ABI_EQ(sizeof(struct kernel_param), 16);

#define module_param(varname, type, perm_val) \
    static const char __param_str_##varname[] = #varname; \
    static struct kernel_param __param_##varname \
    __attribute__((used, section("__param"), aligned(4))) = { \
        .name = __param_str_##varname, \
        .ops = &param_ops_int, \
        .perm = (perm_val), \
        .level = -1, \
        .arg = &(varname), \
    }

struct module {
    u8 _head[0x0c];
    char name[56];       /* +0x0c..0x43 */
    u8 _mid[0x78];       /* +0x44..0xbb */
    int (*init)(void);   /* +0xbc */
    u8 _body[0x88];      /* +0xc0..0x147 */
    void (*exit)(void);  /* +0x148 */
    u32 _ref;            /* +0x14c */
};
ABI_EQ(sizeof(struct module), 0x150);
ABI_EQ(offsetof(struct module, name), 0x0c);
ABI_EQ(offsetof(struct module, init), 0xbc);
ABI_EQ(offsetof(struct module, exit), 0x148);

extern int init_module(void);
extern void cleanup_module(void);

struct module __this_module
__attribute__((used, section(".gnu.linkonce.this_module"))) = {
    .name = "aotg_hub_hcd",
    .init = init_module,
    .exit = cleanup_module,
};

static const char __mod_vermagic[]
__attribute__((used, section(".modinfo"), aligned(1))) =
    "vermagic=3.10.52 SMP preempt mod_unload ARMv7 p2v8 ";
static const char __mod_license[]
__attribute__((used, section(".modinfo"), aligned(1))) =
    "license=GPL";
static const char __mod_desc[]
__attribute__((used, section(".modinfo"), aligned(1))) =
    "description=Actions ATM7051H Multi-Device USB Hub TDM HCD";

#endif /* _AOTG_KABI_H_ */

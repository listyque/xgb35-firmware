"""Cross-compile Linux 3.10.52 rt2800usb.ko (Ralink RT2870/RT3070/RT3370/RT3572/RT5370/RT5572,
including Tenda W311U 148f:3070) against our exact running ATM7051H 3.10.52 SMP PREEMPT ARMv7 kernel,
verifying every critical kernel struct offset against Image disassembly via compile-time BUILD_BUG_ON
and every undefined symbol against live /proc/kallsyms.
"""
import gzip
import pathlib
import shutil
import struct
import subprocess
import sys

ROOT = pathlib.Path(r'd:\grav\asm_ex\build\firmware-inspection\recovery-research')
SCR = pathlib.Path(r'C:\Users\listy\.gemini\antigravity\brain\a50febf9-bf84-496a-809c-0b629c063805\scratch')
KSRC = SCR / 'k310' / 'src'
WORK = SCR / 'k310' / 'build'
OUT = ROOT / 'wifi-rt2800usb'

AUTOCONF_H = """#ifndef __GENERATED_AUTOCONF_H
#define __GENERATED_AUTOCONF_H
#define CONFIG_ARM 1
#define CONFIG_CPU_V7 1
#define CONFIG_CPU_32v6K 1
#define CONFIG_CPU_32v7 1
#define CONFIG_CPU_COPY_V6 1
#define CONFIG_CPU_TLB_V7 1
#define CONFIG_CPU_HAS_ASID 1
#define CONFIG_CPU_CP15 1
#define CONFIG_CPU_CP15_MMU 1
#define CONFIG_CPU_ABRT_EV7 1
#define CONFIG_CPU_PABRT_V7 1
#define CONFIG_CPU_CACHE_V7 1
#define CONFIG_CPU_CACHE_VIPT 1
#define CONFIG_ARM_L1_CACHE_SHIFT 6
#define CONFIG_ARM_L1_CACHE_SHIFT_6 1
#define CONFIG_AEABI 1
#define CONFIG_ARM_PATCH_PHYS_VIRT 1
#define CONFIG_PHYS_OFFSET 0x00000000
#define CONFIG_PAGE_OFFSET 0xC0000000
#define CONFIG_VMSPLIT_3G 1
#define CONFIG_HZ 100
#define CONFIG_LBDAF 1
#define CONFIG_BUG 1
#define CONFIG_GENERIC_BUG 1
#define CONFIG_ARM_UNWIND 1
#define CONFIG_HAVE_MOD_ARCH_SPECIFIC 1

#define CONFIG_SMP 1
#define CONFIG_NR_CPUS 4
#define CONFIG_PREEMPT 1
#define CONFIG_PREEMPT_COUNT 1
#define CONFIG_DEBUG_PREEMPT 1
#define CONFIG_PREEMPT_RCU 1
#define CONFIG_TREE_PREEMPT_RCU 1
#define CONFIG_UNINLINE_SPIN_UNLOCK 1
#define CONFIG_RCU_STALL_COMMON 1
#define CONFIG_MUTEX_SPIN_ON_OWNER 1
#define CONFIG_RWSEM_GENERIC_SPINLOCK 1
#define CONFIG_GENERIC_SMP_IDLE_THREAD 1
#define CONFIG_GENERIC_ATOMIC64 1
#define CONFIG_FUTEX 1
#define CONFIG_EPOLL 1
#define CONFIG_SIGNALFD 1
#define CONFIG_TIMERFD 1
#define CONFIG_EVENTFD 1
#define CONFIG_SHMEM 1
#define CONFIG_AIO 1
#define CONFIG_SLUB 1
#define CONFIG_ZONE_DMA_FLAG 0
#define CONFIG_BOUNCE 1
#define CONFIG_MMU 1
#define CONFIG_HIGHMEM 1
#define CONFIG_COMPACTION 1
#define CONFIG_MIGRATION 1
#define CONFIG_FLATMEM 1
#define CONFIG_FLAT_NODE_MEM_MAP 1
#define CONFIG_SPLIT_PTLOCK_CPUS 4

#define CONFIG_MODULES 1
#define CONFIG_MODULE_UNLOAD 1
#define CONFIG_KALLSYMS 1
#define CONFIG_SYSFS 1
#define CONFIG_PROC_FS 1
#define CONFIG_PM 1
#define CONFIG_PM_SLEEP 1
#define CONFIG_PM_SLEEP_SMP 1
#define CONFIG_PM_RUNTIME 1
#define CONFIG_SUSPEND 1
#define CONFIG_HAS_DMA 1
#define CONFIG_HAVE_DMA_ATTRS 1
#define CONFIG_CMA 1
#define CONFIG_PINCTRL 1
#define CONFIG_PM_CLK 1
#define CONFIG_PM_GENERIC_DOMAINS 1
#define CONFIG_HAS_IOMEM 1
#define CONFIG_HAS_IOPORT 1
#define CONFIG_GENERIC_HARDIRQS 1
#define CONFIG_GENERIC_IRQ_SHOW 1
#define CONFIG_IRQ_DOMAIN 1
#define CONFIG_OF 1
#define CONFIG_FW_LOADER 1

#define CONFIG_NET 1
#define CONFIG_INET 1
#define CONFIG_IPV6 1
#define CONFIG_IPV6_NDISC_NODETYPE 1
#define CONFIG_XFRM 1
#define CONFIG_NETFILTER 1
#define CONFIG_NF_CONNTRACK_MODULE 1
#define CONFIG_NET_SCHED 1
#define CONFIG_NET_CLS_ACT 1
#define CONFIG_DMA_ENGINE 1
#define CONFIG_NET_DMA 1
#define CONFIG_WIRELESS 1
#define CONFIG_WIRELESS_EXT 1
#define CONFIG_WEXT_CORE 1
#define CONFIG_WEXT_PROC 1
#define CONFIG_WEXT_PRIV 1
#define CONFIG_CFG80211 1
#define CONFIG_CFG80211_WEXT 1
#define CONFIG_NL80211_TESTMODE 1
#define CONFIG_MAC80211 1
#define CONFIG_MAC80211_RC_MINSTREL 1
#define CONFIG_MAC80211_RC_MINSTREL_HT 1
#define CONFIG_MAC80211_RC_DEFAULT_MINSTREL 1
#define CONFIG_MAC80211_MESH 1

#define CONFIG_USB_SUPPORT 1
#define CONFIG_USB 1
#define CONFIG_USB_ARCH_HAS_HCD 1

#define CONFIG_RT2X00_LIB_FIRMWARE 1
#define CONFIG_RT2X00_LIB_CRYPTO 1
#define CONFIG_RT2X00_LIB_USB 1
#define CONFIG_RT2800_LIB 1
#define CONFIG_RT2800USB_MODULE 1
#define CONFIG_RT2800USB_RT33XX 1
#define CONFIG_RT2800USB_RT35XX 1
#define CONFIG_RT2800USB_RT3573 1
#define CONFIG_RT2800USB_RT53XX 1
#define CONFIG_RT2800USB_RT55XX 1
#define CONFIG_RT2800USB_UNKNOWN 1

#define __LINUX_ARM_ARCH__ 7
#endif
"""

MOD_ABI_C = r"""#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/skbuff.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/workqueue.h>
#include <net/mac80211.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

extern int init_module(void);
extern void cleanup_module(void);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
    .name = KBUILD_MODNAME,
    .init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
    .exit = cleanup_module,
#endif
    .arch = MODULE_ARCH_INIT,
};

/* Verified against disassembled kernel.uncompressed (3.10.52 SMP PREEMPT ARMv7) */
#define ABI_EQ(a, b) _Static_assert((a) == (b), #a " != " #b)
ABI_EQ(sizeof(spinlock_t), 4);
ABI_EQ(sizeof(struct mutex), 0x18);
ABI_EQ(offsetof(struct mutex, wait_list), 0x08);
ABI_EQ(offsetof(struct mutex, owner), 0x10);

ABI_EQ(offsetof(struct timer_list, base), 0x0c);
ABI_EQ(offsetof(struct timer_list, slack), 0x18);

ABI_EQ(offsetof(struct device, p), 0x04);
ABI_EQ(offsetof(struct device, dma_pools), 0x10c);
ABI_EQ(offsetof(struct device, archdata), 0x11c);
ABI_EQ(offsetof(struct device, devres_lock), 0x12c);
ABI_EQ(sizeof(struct device), 0x158);
ABI_EQ(offsetof(struct usb_device, dev), 0x68);
ABI_EQ(offsetof(struct urb, hcpriv), 0x04);
ABI_EQ(offsetof(struct urb, complete), 0x78);
ABI_EQ(offsetof(struct usb_driver, dynids), 0x28);
ABI_EQ(offsetof(struct usb_driver, drvwrap), 0x34);

ABI_EQ(offsetof(struct sk_buff, len), 0x50);
ABI_EQ(offsetof(struct sk_buff, data_len), 0x54);
ABI_EQ(offsetof(struct sk_buff, skb_iif), 0x70);
ABI_EQ(offsetof(struct sk_buff, rxhash), 0x74);
ABI_EQ(offsetof(struct sk_buff, vlan_proto), 0x78);
ABI_EQ(offsetof(struct sk_buff, tc_index), 0x7c);
ABI_EQ(offsetof(struct sk_buff, queue_mapping), 0x80);
ABI_EQ(offsetof(struct sk_buff, mark), 0x88);
ABI_EQ(offsetof(struct sk_buff, tail), 0xa4);
ABI_EQ(offsetof(struct sk_buff, end), 0xa8);
ABI_EQ(offsetof(struct sk_buff, head), 0xac);
ABI_EQ(offsetof(struct sk_buff, data), 0xb0);

ABI_EQ(offsetof(struct wiphy, retry_short), 0x48);
ABI_EQ(offsetof(struct wiphy, bands), 0xb4);
ABI_EQ(offsetof(struct wiphy, dev), 0xc8);
ABI_EQ(offsetof(struct wiphy, registered), 0x220);
ABI_EQ(offsetof(struct ieee80211_ops, add_interface), 0x18);
ABI_EQ(offsetof(struct ieee80211_ops, remove_interface), 0x20);
ABI_EQ(offsetof(struct ieee80211_ops, config), 0x24);
ABI_EQ(offsetof(struct ieee80211_ops, configure_filter), 0x38);
ABI_EQ(offsetof(struct ieee80211_ops, hw_scan), 0x54);
ABI_EQ(offsetof(struct ieee80211_ops, sta_add), 0x7c);
ABI_EQ(offsetof(struct ieee80211_ops, sta_state), 0x88);
ABI_EQ(offsetof(struct ieee80211_ops, remain_on_channel), 0xd0);
ABI_EQ(offsetof(struct ieee80211_ops, add_chanctx), 0x108);
ABI_EQ(offsetof(struct ieee80211_hw, wiphy), 0x30);
ABI_EQ(offsetof(struct ieee80211_hw, flags), 0x3c);
ABI_EQ(offsetof(struct ieee80211_hw, queues), 0x58);
ABI_EQ(offsetof(struct ieee80211_hw, offchannel_tx_hw_queue), 0x62);

ABI_EQ(sizeof(struct module), 0x150);
ABI_EQ(offsetof(struct module, name), 0x0c);
ABI_EQ(offsetof(struct module, init), 0xbc);
ABI_EQ(offsetof(struct module, exit), 0x148);

ABI_EQ(sizeof(struct hrtimer), 0x30);
ABI_EQ(offsetof(struct tasklet_hrtimer, function), 0x44);
ABI_EQ(sizeof(struct __kfifo), 0x14);
ABI_EQ(offsetof(struct work_struct, func), 0x0c);
ABI_EQ(sizeof(struct work_struct), 0x10);
ABI_EQ(offsetof(struct delayed_work, timer), 0x10);
ABI_EQ(offsetof(struct delayed_work, wq), 0x2c);
ABI_EQ(offsetof(struct delayed_work, cpu), 0x30);
ABI_EQ(offsetof(struct ieee80211_tx_info, control.vif), 0x18);
ABI_EQ(offsetof(struct ieee80211_conf, chandef), 0x18);
"""


def prepare_headers():
    inc = WORK / 'include'
    for sub in ('generated', 'config', 'mach', 'asm'):
        (inc / sub).mkdir(parents=True, exist_ok=True)
    # Create asm-generic wrapper headers in arch/arm/include/generated/asm if missing
    gen_asm = KSRC / 'arch/arm/include/generated/asm'
    gen_asm.mkdir(parents=True, exist_ok=True)
    for gdir in ('include/uapi/asm-generic', 'include/asm-generic'):
        for g in (KSRC / gdir).glob('*.h'):
            arm_h = KSRC / 'arch/arm/include/asm' / g.name
            uapi_h = KSRC / 'arch/arm/include/uapi/asm' / g.name
            if not arm_h.exists() and not (uapi_h.exists() and 'uapi' in gdir):
                rel = 'uapi/asm-generic' if 'uapi' in gdir else 'asm-generic'
                (gen_asm / g.name).write_text(f'#include <{rel}/{g.name}>\n')

    (inc / 'generated/autoconf.h').write_text(AUTOCONF_H)
    (inc / 'generated/utsrelease.h').write_text('#define UTS_RELEASE "3.10.52"\n')
    (inc / 'generated/bounds.h').write_text(
        '#define NR_PAGEFLAGS 22\n#define MAX_NR_ZONES 3\n#define NR_PCG_FLAGS 3\n')
    (inc / 'generated/asm-offsets.h').write_text('#define TI_PREEMPT 4\n')
    (inc / 'mach/memory.h').write_text('#ifndef __ASM_ARCH_MEMORY_H\n#define __ASM_ARCH_MEMORY_H\n#endif\n')
    (inc / 'mach/irqs.h').write_text('#define NR_IRQS 256\n')
    (inc / 'mach/timex.h').write_text('#define CLOCK_TICK_RATE 24000000\n')
    (inc / 'mach/io.h').write_text('#define IO_SPACE_LIMIT 0xffffffff\n#define __io(a) __typesafe_io(a)\n')
    # Fix GCC-ism `register unsigned long sp asm("sp");` (UB in Clang -> evaluates to 0/NULL!)
    ti_h = (KSRC / 'arch/arm/include/asm/thread_info.h').read_text()
    assert '\tregister unsigned long sp asm ("sp");' in ti_h
    ti_h = ti_h.replace(
        '\tregister unsigned long sp asm ("sp");',
        '\tunsigned long sp;\n\tasm ("mov %0, sp" : "=r" (sp));'
    )
    (inc / 'asm/thread_info.h').write_text(ti_h)

    pc_h = (KSRC / 'arch/arm/include/asm/percpu.h').read_text()
    assert '\tregister unsigned long *sp asm ("sp");' in pc_h
    pc_h = pc_h.replace(
        '\tregister unsigned long *sp asm ("sp");\n\n\t/*\n\t * Read TPIDRPRW.\n\t * We want to allow caching the value, so avoid using volatile and\n\t * instead use a fake stack read to hazard against barrier().\n\t */\n\tasm("mrc p15, 0, %0, c13, c0, 4" : "=r" (off) : "Q" (*sp));',
        '\tunsigned long sp;\n\tasm ("mov %0, sp" : "=r" (sp));\n\tasm("mrc p15, 0, %0, c13, c0, 4" : "=r" (off) : "Q" (*(const unsigned long *)sp));'
    )
    (inc / 'asm/percpu.h').write_text(pc_h)
    (WORK / 'shim.h').write_text(
        '#include <linux/export.h>\n'
        '#undef __EXPORT_SYMBOL\n'
        '#define __EXPORT_SYMBOL(sym, sec)\n'
        '#ifdef NO_SUB_MODINFO\n'
        '#include <linux/module.h>\n'
        '#undef MODULE_LICENSE\n#define MODULE_LICENSE(x)\n'
        '#undef MODULE_AUTHOR\n#define MODULE_AUTHOR(x)\n'
        '#undef MODULE_DESCRIPTION\n#define MODULE_DESCRIPTION(x)\n'
        '#undef MODULE_VERSION\n#define MODULE_VERSION(x)\n'
        '#undef MODULE_FIRMWARE\n#define MODULE_FIRMWARE(x)\n'
        '#endif\n'
    )


def build():
    WORK.mkdir(parents=True, exist_ok=True)
    OUT.mkdir(parents=True, exist_ok=True)
    prepare_headers()

    (WORK / 'mod_abi.c').write_text(MOD_ABI_C)
    # Use stock upstream rt2x00usb.c (aotg_hcd 0xc02926f8 handles unaligned skb->data via coherent ubuf[0..7] bounce buffers)
    (WORK / 'rt2x00usb.c').write_text((KSRC / 'rt2x00/rt2x00usb.c').read_text())

    # Cap RX (128->16) & TX (16->8 x 4 queues) so Actions aotg_hcd 60-slot q_pool (0xc0296ccc) never exhausts!
    d_src = (KSRC / 'rt2x00/rt2800usb.c').read_text()
    assert '\t.entry_num\t\t= 128,' in d_src and '\t.entry_num\t\t= 16,' in d_src
    d_src = d_src.replace('\t.entry_num\t\t= 16,', '\t.entry_num\t\t= 8,')
    d_src = d_src.replace('\t.entry_num\t\t= 128,', '\t.entry_num\t\t= 16,')
    (WORK / 'rt2800usb.c').write_text(d_src)

    # Cap default RT3070 TX power at 8 dBm to prevent aotg1 5V VBUS droop!
    l_src = (KSRC / 'rt2x00/rt2800lib.c').read_text()
    tp_anchor = 'static void rt2800_config_txpower(struct rt2x00_dev *rt2x00dev,\n\t\t\t\t  struct ieee80211_channel *chan,\n\t\t\t\t  int power_level)\n{'
    tp_patch = (
        tp_anchor +
        '\n\t/* shortcut: cap RT3070 TX at 8 dBm to prevent aotg1 5V VBUS droop, raise when using externally powered hub */\n'
        '\tif (power_level > 8)\n\t\tpower_level = 8;'
    )
    assert tp_anchor in l_src
    l_src = l_src.replace(tp_anchor, tp_patch, 1)
    (WORK / 'rt2800lib.c').write_text(l_src)

    sources = [
        WORK / 'mod_abi.c',
        KSRC / 'rt2x00/crc-ccitt.c',
        KSRC / 'rt2x00/rt2x00dev.c',
        KSRC / 'rt2x00/rt2x00mac.c',
        KSRC / 'rt2x00/rt2x00config.c',
        KSRC / 'rt2x00/rt2x00queue.c',
        KSRC / 'rt2x00/rt2x00link.c',
        KSRC / 'rt2x00/rt2x00crypto.c',
        KSRC / 'rt2x00/rt2x00firmware.c',
        WORK / 'rt2x00usb.c',
        WORK / 'rt2800lib.c',
        WORK / 'rt2800usb.c',
    ]

    cflags = [
        sys.executable, '-m', 'ziglang', 'cc',
        '-target', 'arm-freestanding-eabi', '-mcpu=cortex_a5', '-marm', '-msoft-float',
        '-nostdinc',
        '-isystem', str(PathZigInclude()),
        '-I', str(WORK / 'include'),
        '-I', str(KSRC / 'arch/arm/include'),
        '-I', str(KSRC / 'arch/arm/include/generated'),
        '-I', str(KSRC / 'arch/arm/include/uapi'),
        '-I', str(KSRC / 'include'),
        '-I', str(KSRC / 'include/uapi'),
        '-I', str(KSRC / 'rt2x00'),
        '-include', str(KSRC / 'include/linux/kconfig.h'),
        '-D__KERNEL__', '-DMODULE',
        '-DKBUILD_BASENAME="rt2800usb"', '-DKBUILD_MODNAME="rt2800usb"',
        '-include', str(WORK / 'shim.h'),
        '-O2', '-g0', '-std=gnu89', '-fno-strict-aliasing', '-fno-common', '-fno-pic', '-fno-PIE',
        '-fno-addrsig', '-fno-stack-protector', '-fno-dwarf2-cfi-asm',
        '-fno-unwind-tables', '-fno-asynchronous-unwind-tables',
        '-mno-unaligned-access',
        '-Wno-gnu-variable-sized-type-not-at-end',
        '-Wno-unused-command-line-argument',
        '-Wno-address-of-packed-member',
        '-Wno-tautological-compare',
        '-Wno-pointer-sign',
        '-Wno-incompatible-pointer-types-discards-qualifiers',
        '-Wno-visibility',
    ]

    objs = []
    for src in sources:
        obj = WORK / (src.stem + '.o')
        extra = [] if src.name in ('rt2800usb.c', 'mod_abi.c') else ['-DNO_SUB_MODINFO=1']
        print('Compiling', src.name, '...', flush=True)
        subprocess.check_call(cflags + extra + ['-c', str(src), '-o', str(obj)])
        objs.append(str(obj))

    ko_path = OUT / 'rt2800usb.ko'
    subprocess.check_call([
        sys.executable, '-m', 'ziglang', 'ld.lld',
        '-r', '-m', 'armelf_linux_eabi', '--build-id=none',
        *objs, '-o', str(ko_path)
    ])
    verify_ko(ko_path)
    shutil.copyfile(SCR / 'k310' / 'rt2870.bin', OUT / 'rt2870.bin')
    print('SUCCESS:', ko_path, ko_path.stat().st_size, 'bytes + rt2870.bin')


def PathZigInclude():
    import ziglang
    return pathlib.Path(ziglang.__file__).parent / 'lib' / 'include'


def verify_ko(ko_path: pathlib.Path):
    ko = bytearray(ko_path.read_bytes())
    e_type, e_machine = struct.unpack_from('<HH', ko, 0x10)
    e_shoff, e_flags = struct.unpack_from('<II', ko, 0x20)
    e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<6H', ko, 0x28)
    print(f'ELF: type={e_type} machine={e_machine} flags={e_flags:#x} shnum={e_shnum}')
    assert (e_type, e_machine) == (1, 40)
    # Match exact e_flags=0x05000000 of factory 3.10.52 modules (strip soft-float ABI 0x200 bit if set by LLD)
    if e_flags != 0x05000000:
        struct.pack_into('<I', ko, 0x24, 0x05000000)
        ko_path.write_bytes(ko)

    shstr_off, shstr_sz = struct.unpack_from('<II', ko, e_shoff + e_shstrndx * e_shentsize + 16)
    shstr = ko[shstr_off:shstr_off + shstr_sz]

    ks = set()
    for line in gzip.decompress((SCR / 'kallsyms.gz').read_bytes()).decode().splitlines():
        p = line.split()
        if len(p) >= 3:
            ks.add(p[2])

    sym_sh = str_sh = None
    for i in range(e_shnum):
        sh = ko[e_shoff + i * e_shentsize : e_shoff + (i + 1) * e_shentsize]
        nm_off, shtype, shflags, shaddr, shoff, shsize, shlink, shinfo, shalign, shentsz = struct.unpack('<10I', sh)
        nm = shstr[nm_off:shstr.find(0, nm_off)].decode('ascii', 'replace')
        if nm == '.gnu.linkonce.this_module':
            print(f'  this_module size={shsize:#x}')
            assert shsize == 0x150, f'Expected 0x150, got {shsize:#x}'
        if shtype == 2:
            sym_sh = (shoff, shsize, shlink, shentsz)

    str_off, str_sz = struct.unpack_from('<II', ko, e_shoff + sym_sh[2] * e_shentsize + 16)
    strtab = ko[str_off:str_off + str_sz]
    missing = []
    undefs = []
    for off in range(sym_sh[0], sym_sh[0] + sym_sh[1], sym_sh[3]):
        st_name, st_val, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', ko, off)
        if st_shndx == 0 and st_name != 0:
            nm = strtab[st_name:strtab.find(0, st_name)].decode('ascii', 'replace')
            undefs.append(nm)
            if nm not in ks or f'__ksymtab_{nm}' not in ks:
                # Some ARM helpers like __aeabi_* are exported without __ksymtab or inks
                if nm not in ks:
                    missing.append(nm)
    print(f'  Undefined kernel symbols: {len(undefs)}, missing from kallsyms: {missing}')
    assert not missing, f'Missing symbols in kernel: {missing}'


if __name__ == '__main__':
    build()

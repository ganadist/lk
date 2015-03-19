/*
 * Copyright (c) 2012-2015 Travis Geiselbrecht
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <err.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include <arch/arm/mmu.h>
#include <kernel/vm.h>
#include <dev/uart.h>
#include <dev/interrupt/arm_gic.h>
#include <dev/timer/arm_cortex_a9.h>
#include <lib/console.h>
#include <platform.h>
#include <platform/zynq.h>
#include <platform/gem.h>
#include <platform/timer.h>
#include "platform_p.h"

#if ZYNQ_SDRAM_INIT
STATIC_ASSERT(SDRAM_SIZE != 0);
#endif

/* target can specify this as the initial jam table to set up the soc */
__WEAK void ps7_init(void) { }

/* These should be defined in the target somewhere */
extern const uint32_t zynq_mio_cfg[ZYNQ_MIO_CNT];
extern const long zynq_ddr_cfg[];
extern const uint32_t zynq_ddr_cfg_cnt;
extern const zynq_pll_cfg_tree_t zynq_pll_cfg;
extern const zynq_clk_cfg_t zynq_clk_cfg;
extern const zynq_ddriob_cfg_t zynq_ddriob_cfg;


static inline int reg_poll(uint32_t addr,uint32_t mask)
{
    uint32_t iters = UINT_MAX;
    while (iters-- && !(*REG32(addr) & mask)) ;

    if (iters) {
        return 0;
    }

    return -1;
}

/* For each PLL we need to configure the cp / res / lock_cnt and then place the PLL in bypass
 * before doing a reset to switch to the new values. Then bypass is removed to switch back to using
 * the PLL once its locked.
 */
int zynq_pll_init(void) {
    const zynq_pll_cfg_tree_t *cfg = &zynq_pll_cfg;

    SLCR_REG(ARM_PLL_CFG)  = PLL_CFG_LOCK_CNT(cfg->arm.lock_cnt) | PLL_CFG_PLL_CP(cfg->arm.cp) |
                                PLL_CFG_PLL_RES(cfg->arm.res);
    SLCR_REG(ARM_PLL_CTRL) = PLL_FDIV(cfg->arm.fdiv) | PLL_BYPASS_FORCE | PLL_RESET;
    SLCR_REG(ARM_PLL_CTRL) &= ~PLL_RESET;

    if (reg_poll((uintptr_t)&SLCR->PLL_STATUS, PLL_STATUS_ARM_PLL_LOCK) == -1) {
        return -1;
    }

    SLCR_REG(ARM_PLL_CTRL) &= ~PLL_BYPASS_FORCE;
    SLCR_REG(ARM_CLK_CTRL) = zynq_clk_cfg.arm_clk;

#if ZYNQ_SDRAM_INIT
    SLCR_REG(DDR_PLL_CFG)  = PLL_CFG_LOCK_CNT(cfg->ddr.lock_cnt) | PLL_CFG_PLL_CP(cfg->ddr.cp) |
                                PLL_CFG_PLL_RES(cfg->ddr.res);
    SLCR_REG(DDR_PLL_CTRL) = PLL_FDIV(cfg->ddr.fdiv) | PLL_BYPASS_FORCE | PLL_RESET;
    SLCR_REG(DDR_PLL_CTRL) &= ~PLL_RESET;

    if (reg_poll((uintptr_t)&SLCR->PLL_STATUS, PLL_STATUS_DDR_PLL_LOCK) == -1) {
        return -1;
    }

    SLCR_REG(DDR_PLL_CTRL) &= ~PLL_BYPASS_FORCE;
    SLCR_REG(DDR_CLK_CTRL) = zynq_clk_cfg.ddr_clk;
#elif SDRAM_SIZE == 0
    /* if we're not using sdram and haven't been told to initialize sdram, stop the DDR pll */
    SLCR_REG(DDR_CLK_CTRL) = 0;
    SLCR_REG(DDR_PLL_CTRL) |= PLL_PWRDOWN;
#endif
    SLCR_REG(IO_PLL_CFG)  = PLL_CFG_LOCK_CNT(cfg->io.lock_cnt) | PLL_CFG_PLL_CP(cfg->io.cp) |
                                PLL_CFG_PLL_RES(cfg->io.res);
    SLCR_REG(IO_PLL_CTRL) = PLL_FDIV(cfg->io.fdiv) | PLL_BYPASS_FORCE | PLL_RESET;
    SLCR_REG(IO_PLL_CTRL) &= ~PLL_RESET;

    if (reg_poll((uintptr_t)&SLCR->PLL_STATUS, PLL_STATUS_IO_PLL_LOCK) == -1) {
        return -1;
    }

    SLCR_REG(IO_PLL_CTRL) &= ~PLL_BYPASS_FORCE;
    return 0;
}

int zynq_mio_init(void)
{

    /* This DDRIOB configuration applies to both zybo and uzed, but it's possible
     * it may not work for all boards in the future. Just something to keep in mind
     * with different memory configurations.
     */
    SLCR_REG(GPIOB_CTRL) = GPIOB_CTRL_VREF_EN;

    for (size_t pin = 0; pin < countof(zynq_mio_cfg); pin++) {
        if (zynq_mio_cfg[pin] != 0) {
            SLCR_REG(MIO_PIN_00 + (pin * 4)) = zynq_mio_cfg[pin];
        }
    }

    SLCR_REG(SD0_WP_CD_SEL) = SDIO0_WP_SEL(0x37) | SDIO0_CD_SEL(0x2F);

    return 0;
}

void zynq_clk_init(void)
{
    SLCR_REG(DCI_CLK_CTRL)   = zynq_clk_cfg.dci_clk;
    SLCR_REG(GEM0_CLK_CTRL)  = zynq_clk_cfg.gem0_clk;
    SLCR_REG(GEM0_RCLK_CTRL) = zynq_clk_cfg.gem0_rclk;
    SLCR_REG(GEM1_CLK_CTRL)  = zynq_clk_cfg.gem1_clk;
    SLCR_REG(GEM1_RCLK_CTRL) = zynq_clk_cfg.gem1_rclk;
    SLCR_REG(SMC_CLK_CTRL)   = zynq_clk_cfg.smc_clk;
    SLCR_REG(LQSPI_CLK_CTRL) = zynq_clk_cfg.lqspi_clk;
    SLCR_REG(SDIO_CLK_CTRL)  = zynq_clk_cfg.sdio_clk;
    SLCR_REG(UART_CLK_CTRL)  = zynq_clk_cfg.uart_clk;
    SLCR_REG(SPI_CLK_CTRL)   = zynq_clk_cfg.spi_clk;
    SLCR_REG(CAN_CLK_CTRL)   = zynq_clk_cfg.can_clk;
    SLCR_REG(CAN_MIOCLK_CTRL)= zynq_clk_cfg.can_mioclk;
    SLCR_REG(USB0_CLK_CTRL)  = zynq_clk_cfg.usb0_clk;
    SLCR_REG(USB1_CLK_CTRL)  = zynq_clk_cfg.usb1_clk;
    SLCR_REG(PCAP_CLK_CTRL)  = zynq_clk_cfg.pcap_clk;
    SLCR_REG(FPGA0_CLK_CTRL) = zynq_clk_cfg.fpga0_clk;
    SLCR_REG(FPGA1_CLK_CTRL) = zynq_clk_cfg.fpga1_clk;
    SLCR_REG(FPGA2_CLK_CTRL) = zynq_clk_cfg.fpga2_clk;
    SLCR_REG(FPGA3_CLK_CTRL) = zynq_clk_cfg.fpga3_clk;
    SLCR_REG(APER_CLK_CTRL)  = zynq_clk_cfg.aper_clk;
    SLCR_REG(CLK_621_TRUE)   = zynq_clk_cfg.clk_621_true;
}

#if ZYNQ_SDRAM_INIT
void zynq_ddr_init(void)
{
    SLCR_REG(DDRIOB_ADDR0) = zynq_ddriob_cfg.addr0;
    SLCR_REG(DDRIOB_ADDR1) = zynq_ddriob_cfg.addr1;
    SLCR_REG(DDRIOB_DATA0) = zynq_ddriob_cfg.data0;
    SLCR_REG(DDRIOB_DATA1) = zynq_ddriob_cfg.data1;
    SLCR_REG(DDRIOB_DIFF0) = zynq_ddriob_cfg.diff0;
    SLCR_REG(DDRIOB_DIFF1) = zynq_ddriob_cfg.diff1;
    SLCR_REG(DDRIOB_CLOCK) = DDRIOB_OUTPUT_EN(0x3);

    /* These register fields are not documented in the TRM. These
     * values represent the defaults generated via the Zynq tools
     */
    SLCR_REG(DDRIOB_DRIVE_SLEW_ADDR) = 0x0018C61CU;
    SLCR_REG(DDRIOB_DRIVE_SLEW_DATA) = 0x00F9861CU;
    SLCR_REG(DDRIOB_DRIVE_SLEW_DIFF) = 0x00F9861CU;
    SLCR_REG(DDRIOB_DRIVE_SLEW_CLOCK) = 0x00F9861CU;
    SLCR_REG(DDRIOB_DDR_CTRL) = 0x00000E60U;
    SLCR_REG(DDRIOB_DCI_CTRL) = 0x00000001U;
    SLCR_REG(DDRIOB_DCI_CTRL) |= 0x00000020U;
    SLCR_REG(DDRIOB_DCI_CTRL) |= 0x00000823U;

    /* Write addresss / value pairs from target table */
    for (size_t i = 0; i < zynq_ddr_cfg_cnt; i += 2) {
        *REG32(zynq_ddr_cfg[i]) = zynq_ddr_cfg[i+1];
    }

    /* Wait for DCI done */
    reg_poll((uintptr_t)&SLCR->DDRIOB_DCI_STATUS, 0x2000);

    /* Bring ddr out of reset and wait until self refresh */
    *REG32(DDRC_CTRL) |= DDRC_CTRL_OUT_OF_RESET;
    reg_poll(DDRC_MODE_STATUS, DDRC_STS_SELF_REFRESH);

    /* Switch timer to 64k */
    *REG32(0XF8007000) = *REG32(0xF8007000) & ~0x20000000U;

    if (zynq_ddriob_cfg.ibuf_disable) {
        SLCR_REG(DDRIOB_DATA0) |= DDRIOB_IBUF_DISABLE_MODE;
        SLCR_REG(DDRIOB_DATA1) |= DDRIOB_IBUF_DISABLE_MODE;
        SLCR_REG(DDRIOB_DIFF0) |= DDRIOB_IBUF_DISABLE_MODE;
        SLCR_REG(DDRIOB_DIFF1) |= DDRIOB_IBUF_DISABLE_MODE;
    }

    if (zynq_ddriob_cfg.term_disable) {
        SLCR_REG(DDRIOB_DATA0) |= DDRIOB_TERM_DISABLE_MODE;
        SLCR_REG(DDRIOB_DATA1) |= DDRIOB_TERM_DISABLE_MODE;
        SLCR_REG(DDRIOB_DIFF0) |= DDRIOB_TERM_DISABLE_MODE;
        SLCR_REG(DDRIOB_DIFF1) |= DDRIOB_TERM_DISABLE_MODE;
    }
}
#endif

STATIC_ASSERT(IS_ALIGNED(SDRAM_BASE, MB));
STATIC_ASSERT(IS_ALIGNED(SDRAM_SIZE, MB));

#if SDRAM_SIZE != 0
/* if we have sdram, the first 1MB is covered by sram */
#define RAM_SIZE (MB + (SDRAM_SIZE - MB))
#else
#define RAM_SIZE (MB)
#endif

/* initial memory mappings. parsed by start.S */
struct mmu_initial_mapping mmu_initial_mappings[] = {
    /* 1GB of sram + sdram space */
    { .phys = SRAM_BASE,
      .virt = KERNEL_BASE,
      .size = RAM_SIZE,
      .flags = 0,
      .name = "memory" },

    /* AXI fpga fabric bus 0 */
    { .phys = 0x40000000,
      .virt = 0x40000000,
      .size = (128*1024*1024),
      .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
      .name = "axi0" },

    /* AXI fpga fabric bus 1 */
    { .phys = 0x80000000,
      .virt = 0x80000000,
      .size = (16*1024*1024),
      .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
      .name = "axi1" },
    /* 0xe0000000 hardware devices */
    { .phys = 0xe0000000,
      .virt = 0xe0000000,
      .size = 0x00300000,
      .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
      .name = "hw-e0000000" },

    /* 0xe1000000 hardware devices */
    { .phys = 0xe1000000,
      .virt = 0xe1000000,
      .size = 0x05000000,
      .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
      .name = "hw-e1000000" },

    /* 0xf8000000 hardware devices */
    { .phys = 0xf8000000,
      .virt = 0xf8000000,
      .size = 0x01000000,
      .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
      .name = "hw-f8000000" },

    /* 0xfc000000 hardware devices */
    { .phys = 0xfc000000,
      .virt = 0xfc000000,
      .size = 0x02000000,
      .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE,
      .name = "hw-fc000000" },

    /* sram high aperture */
    { .phys = 0xfff00000,
      .virt = 0xfff00000,
      .size = 0x00100000,
      .flags = MMU_INITIAL_MAPPING_FLAG_DEVICE },

    /* identity map to let the boot code run */
    { .phys = SRAM_BASE,
      .virt = SRAM_BASE,
      .size = RAM_SIZE,
      .flags = MMU_INITIAL_MAPPING_TEMPORARY },

    /* null entry to terminate the list */
    { 0 }
};

#if SDRAM_SIZE != 0
static pmm_arena_t sdram_arena = {
    .name = "sdram",
    .base = SDRAM_BASE,
    .size = SDRAM_SIZE - MB, /* first 1MB is covered by SRAM */
    .flags = PMM_ARENA_FLAG_KMAP
};
#endif

static pmm_arena_t sram_arena = {
    .name = "sram",
    .base = SRAM_BASE,
    .size = SRAM_SIZE,
    .priority = 1,
    .flags = PMM_ARENA_FLAG_KMAP
};

void platform_init_mmu_mappings(void)
{
}

void platform_early_init(void)
{
    /* Unlock the registers and leave them that way */
#if 0
    ps7_init();
#else
    zynq_slcr_unlock();
    zynq_mio_init();
    zynq_pll_init();
    zynq_clk_init();
#if ZYNQ_SDRAM_INIT
    zynq_ddr_init();
#endif
#endif

    /* Enable all level shifters */
    SLCR_REG(LVL_SHFTR_EN) = 0xF;
    /* FPGA SW reset (not documented, but mandatory) */
    SLCR_REG(FPGA_RST_CTRL) = 0x0;

    /* zynq manual says this is mandatory for cache init */
    *REG32(SLCR_BASE + 0xa1c) = 0x020202;


    /* early initialize the uart so we can printf */
    uart_init_early();

    /* initialize the interrupt controller */
    arm_gic_init();

    /* initialize the timer block */
    arm_cortex_a9_timer_init(CPUPRIV_BASE, zynq_get_arm_timer_freq());

    /* add the main memory arena */
#if !ZYNQ_CODE_IN_SDRAM && SDRAM_SIZE != 0
    /* In the case of running from SRAM, and we are using SDRAM,
     * there is a discontinuity between the end of SRAM (256K) and the start of SDRAM (1MB),
     * so intentionally bump the boot-time allocator to start in the base of SDRAM.
     */
    extern uintptr_t boot_alloc_start;
    extern uintptr_t boot_alloc_end;

    boot_alloc_start = KERNEL_BASE + MB;
    boot_alloc_end = KERNEL_BASE + MB;
#endif

#if SDRAM_SIZE != 0
    pmm_add_arena(&sdram_arena);
#endif
    pmm_add_arena(&sram_arena);

    /* start the second cpu */
    /* the boot rom has been holding it in a wfe loop up until now */
    *REG32(0xfffffff0) = (uint32_t)(MEMBASE + KERNEL_LOAD_OFFSET);
    __asm__ volatile("sev");
}

void platform_init(void)
{
    uart_init();

    /* enable if we want to see some hardware boot status */
#if 0
    printf("zynq boot status:\n");
    printf("\tREBOOT_STATUS 0x%x\n", SLCR_REG(REBOOT_STATUS));
    printf("\tBOOT_MODE 0x%x\n", SLCR_REG(BOOT_MODE));

    zynq_dump_clocks();
#endif
}

void platform_quiesce(void)
{
#if ZYNQ_WITH_GEM_ETH
    gem_disable();
#endif

    platform_stop_timer();
}

#if WITH_LIB_CONSOLE
static int cmd_zynq(int argc, const cmd_args *argv)
{
    if (argc < 2) {
notenoughargs:
        printf("not enough arguments\n");
usage:
        printf("usage: %s <command>\n", argv[0].str);
        printf("\tslcr lock\n");
        printf("\tslcr unlock\n");
        printf("\tslcr lockstatus\n");
        printf("\tmio\n");
        printf("\tclocks\n");
        return -1;
    }

    if (!strcmp(argv[1].str, "slcr")) {
        if (argc < 3) goto notenoughargs;

        bool print_lock_status = false;
        if (!strcmp(argv[2].str, "lock")) {
            zynq_slcr_lock();
            print_lock_status = true;
        } else if (!strcmp(argv[2].str, "unlock")) {
            zynq_slcr_unlock();
            print_lock_status = true;
        } else if (print_lock_status || !strcmp(argv[2].str, "lockstatus")) {
            printf("%s\n", (SLCR->SLCR_LOCKSTA & 0x1) ? "locked" : "unlocked");
        } else {
            goto usage;
        }
    } else if (!strcmp(argv[1].str, "mio")) {
        printf("zynq mio:\n");
        for (size_t i = 0; i < ZYNQ_MIO_CNT; i++) {
            printf("\t%02u: 0x%08x", i, *REG32((uintptr_t)&SLCR->MIO_PIN_00 + (i * 4)));
            if (i % 4 == 3 || i == 53) {
                putchar('\n');
            }
        }
    } else if (!strcmp(argv[1].str, "clocks")) {
        zynq_dump_clocks();
    } else {
        goto usage;
    }

    return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("zynq", "zynq configuration commands", &cmd_zynq)
STATIC_COMMAND_END(zynq);
#endif // WITH_LIB_CONSOLE

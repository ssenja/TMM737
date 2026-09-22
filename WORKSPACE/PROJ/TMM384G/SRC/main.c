/*!
    \file    main.c
    \brief   led spark with systick, USART print and key example

    \version 2026-03-24, V1.6.0, firmware for GD32H73x_75x
*/

/*
    Copyright (c) 2026, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#include "gd32h73x_75x.h"
#include "systick.h"
#include <stdio.h>
#include <stdint.h>

#include "main.h"
#include "sdram.h"

void cache_enable(void);
void mpu_config(void);
static void dbg_uart_init(void);
/*
 * Reduce dynamic power by clock-gating peripherals that are not used by
 * the current SDRAM validation firmware.  Keep EXMC, GPIOD/E/F/G/H and
 * USART1 clocks available because SDRAM and DBG UART require them.
 *
 * This is clock gating, not removal of the MCU power rail.  Re-enable the
 * corresponding RCU clock before a disabled peripheral is used later.
 */
static void unused_peripheral_clocks_disable(void)
{
    /* High-bandwidth / communication blocks. */
    rcu_periph_clock_disable(RCU_DMA0);
    rcu_periph_clock_disable(RCU_DMA1);
    rcu_periph_clock_disable(RCU_DMAMUX);
    rcu_periph_clock_disable(RCU_ENET0);
    rcu_periph_clock_disable(RCU_ENET0TX);
    rcu_periph_clock_disable(RCU_ENET0RX);
    rcu_periph_clock_disable(RCU_ENET0PTP);
    rcu_periph_clock_disable(RCU_ENET1);
    rcu_periph_clock_disable(RCU_ENET1TX);
    rcu_periph_clock_disable(RCU_ENET1RX);
    rcu_periph_clock_disable(RCU_ENET1PTP);
    rcu_periph_clock_disable(RCU_USBHS0);
    rcu_periph_clock_disable(RCU_USBHS0ULPI);
    rcu_periph_clock_disable(RCU_IPA);
    rcu_periph_clock_disable(RCU_SDIO0);
    rcu_periph_clock_disable(RCU_MDMA);
    rcu_periph_clock_disable(RCU_OSPIM);
    rcu_periph_clock_disable(RCU_OSPI0);
    rcu_periph_clock_disable(RCU_OSPI1);

    /* Timers not used by this test (SysTick is a CPU timer, not these). */
    rcu_periph_clock_disable(RCU_TIMER0);
    rcu_periph_clock_disable(RCU_TIMER1);
    rcu_periph_clock_disable(RCU_TIMER2);
    rcu_periph_clock_disable(RCU_TIMER3);
    rcu_periph_clock_disable(RCU_TIMER4);
    rcu_periph_clock_disable(RCU_TIMER5);
    rcu_periph_clock_disable(RCU_TIMER6);
    rcu_periph_clock_disable(RCU_TIMER7);
    rcu_periph_clock_disable(RCU_TIMER22);
    rcu_periph_clock_disable(RCU_TIMER23);
    rcu_periph_clock_disable(RCU_TIMER30);
    rcu_periph_clock_disable(RCU_TIMER31);
    rcu_periph_clock_disable(RCU_TIMER50);
    rcu_periph_clock_disable(RCU_TIMER51);

    /* Serial / analog blocks not used by this test.  USART1 is retained. */
    rcu_periph_clock_disable(RCU_SPI1);
    rcu_periph_clock_disable(RCU_SPI2);
    rcu_periph_clock_disable(RCU_I2C0);
    rcu_periph_clock_disable(RCU_I2C1);
    rcu_periph_clock_disable(RCU_I2C2);
    rcu_periph_clock_disable(RCU_I2C3);
    rcu_periph_clock_disable(RCU_USART0);
    rcu_periph_clock_disable(RCU_USART2);
    rcu_periph_clock_disable(RCU_USART5);
    rcu_periph_clock_disable(RCU_UART3);
    rcu_periph_clock_disable(RCU_UART4);
    rcu_periph_clock_disable(RCU_UART6);
    rcu_periph_clock_disable(RCU_UART7);
    rcu_periph_clock_disable(RCU_ADC0);
    rcu_periph_clock_disable(RCU_ADC1);
    rcu_periph_clock_disable(RCU_DAC);
    rcu_periph_clock_disable(RCU_CAN0);
    rcu_periph_clock_disable(RCU_CAN1);
    rcu_periph_clock_disable(RCU_CAN2);

    printf("Unused peripheral clocks gated for low-power test.\r\n");
}

static uint32_t sdram_full_memory_test(void)
{
    static const uint16_t fixed_pattern[] = {
        0x0000U, 0xFFFFU, 0xAAAAU, 0x5555U
    };
    volatile uint16_t *mem = (volatile uint16_t *)SDRAM_BASE_ADDR;
    const uint32_t words = SDRAM_SIZE_BYTES / 2U;
    const uint32_t progress_words = (1U * 1024U * 1024U) / 2U; /* 1 MiB */
    uint32_t p;
    uint32_t i;
    uint16_t expected;
    uint16_t actual;

    printf("\r\n=== FULL 32 MiB SDRAM TEST ===\r\n");
    printf("WARNING: destructive test; entire SDRAM contents are overwritten.\r\n");

    for(p = 0U; p < (sizeof(fixed_pattern) / sizeof(fixed_pattern[0])); ++p) {
        printf("\r\nFIXED %04X : WRITE", (unsigned int)fixed_pattern[p]);
        for(i = 0U; i < words; ++i) {
            mem[i] = fixed_pattern[p];
            if(((i + 1U) % progress_words) == 0U) {
                printf(".");
            }
        }
        __DSB();

        printf(" VERIFY");
        for(i = 0U; i < words; ++i) {
            actual = mem[i];
            if(actual != fixed_pattern[p]) {
                printf("\r\nFAIL FIXED=%04X ADDR=0x%08lX EXP=%04X READ=%04X\r\n",
                       (unsigned int)fixed_pattern[p],
                       (unsigned long)(SDRAM_BASE_ADDR + (i * 2U)),
                       (unsigned int)fixed_pattern[p],
                       (unsigned int)actual);
                return 1U;
            }
            if(((i + 1U) % progress_words) == 0U) {
                printf(".");
            }
        }
        printf(" PASS\r\n");
    }

    printf("\r\nADDRESS PATTERN : WRITE");
    for(i = 0U; i < words; ++i) {
        mem[i] = (uint16_t)(i ^ (i >> 16));
        if(((i + 1U) % progress_words) == 0U) {
            printf(".");
        }
    }
    __DSB();

    printf(" VERIFY");
    for(i = 0U; i < words; ++i) {
        expected = (uint16_t)(i ^ (i >> 16));
        actual = mem[i];
        if(actual != expected) {
            printf("\r\nFAIL ADDRESS ADDR=0x%08lX EXP=%04X READ=%04X\r\n",
                   (unsigned long)(SDRAM_BASE_ADDR + (i * 2U)),
                   (unsigned int)expected,
                   (unsigned int)actual);
            return 2U;
        }
        if(((i + 1U) % progress_words) == 0U) {
            printf(".");
        }
    }
    printf(" PASS\r\n");

    printf("\r\nINVERSE ADDRESS : WRITE");
    for(i = 0U; i < words; ++i) {
        mem[i] = (uint16_t)~(uint16_t)(i ^ (i >> 16));
        if(((i + 1U) % progress_words) == 0U) {
            printf(".");
        }
    }
    __DSB();

    printf(" VERIFY");
    for(i = 0U; i < words; ++i) {
        expected = (uint16_t)~(uint16_t)(i ^ (i >> 16));
        actual = mem[i];
        if(actual != expected) {
            printf("\r\nFAIL INV_ADDR ADDR=0x%08lX EXP=%04X READ=%04X\r\n",
                   (unsigned long)(SDRAM_BASE_ADDR + (i * 2U)),
                   (unsigned int)expected,
                   (unsigned int)actual);
            return 3U;
        }
        if(((i + 1U) % progress_words) == 0U) {
            printf(".");
        }
    }
    printf(" PASS\r\n");
    printf("FULL MEMORY RESULT : PASS\r\n");
    printf("===============================\r\n");
    return 0U;
}

#define DBG_UART_BAUDRATE    115200U
#define DBG_UART             USART1

/*!
    \brief      main function
    \param[in]  none
    \param[out] none
    \retval     none
*/
int main(void)
{
#ifdef __FIRMWARE_VERSION_DEFINE
    uint32_t fw_ver = 0U;
#endif

    cache_enable();
    mpu_config();
    systick_config();

    dbg_uart_init();
    printf("\r\n\r\n=== TMM384G SDRAM TEST ===\r\n");
    printf("DBG UART : USART1 / PD5-TX PD6-RX / %lu baud\r\n",
           (unsigned long)DBG_UART_BAUDRATE);

    /* Reduce dynamic power before enabling the blocks required by SDRAM. */
    unused_peripheral_clocks_disable();

    /* MT48LC16M16A2B4-6A, 32 MiB, EXMC read pipeline = 1 CK. */
    sdram_init();

#ifdef __FIRMWARE_VERSION_DEFINE
    fw_ver = gd32h73x_75x_firmware_version_get();
    printf("\r\nGD32H7XX series firmware version: V%d.%d.%d\r\n",
           (uint8_t)(fw_ver >> 24), (uint8_t)(fw_ver >> 16),
           (uint8_t)(fw_ver >> 8));
#endif

    printf("CK_SYS  = %lu\r\n", (unsigned long)rcu_clock_freq_get(CK_SYS));
    printf("CK_AHB  = %lu\r\n", (unsigned long)rcu_clock_freq_get(CK_AHB));

    /*
     * Keep only the full 32 MiB destructive memory test.
     * The earlier base/geometry/signature diagnostics were bring-up tools
     * and are redundant now that the interface has passed validation.
     */
    if(sdram_full_memory_test() == 0U) {
        printf("\r\n*** FULL SDRAM TEST : PASS ***\r\n");
    } else {
        printf("\r\n*** FULL SDRAM TEST : FAIL ***\r\n");
    }

    while(1)
    {
        __WFI();
    }
}

/*!
    \brief      initialize debug UART (USART1)
    \param[in]  none
    \param[out] none
    \retval     none
*/
static void dbg_uart_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_USART1);

    /* PD5 = USART1_TX, PD6 = USART1_RX, alternate function 7 */
    gpio_af_set(GPIOD, GPIO_AF_7, GPIO_PIN_5 | GPIO_PIN_6);
    gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_PULLUP,
                  GPIO_PIN_5 | GPIO_PIN_6);
    gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_100_220MHZ,
                            GPIO_PIN_5 | GPIO_PIN_6);

    usart_deinit(DBG_UART);
    usart_baudrate_set(DBG_UART, DBG_UART_BAUDRATE);
    usart_word_length_set(DBG_UART, USART_WL_8BIT);
    usart_stop_bit_set(DBG_UART, USART_STB_1BIT);
    usart_parity_config(DBG_UART, USART_PM_NONE);
    usart_transmit_config(DBG_UART, USART_TRANSMIT_ENABLE);
    usart_receive_config(DBG_UART, USART_RECEIVE_ENABLE);
    usart_enable(DBG_UART);
}

/*!
    \brief      retarget printf to debug USART1
*/
int fputc(int ch, FILE *f)
{
    (void)f;
    usart_data_transmit(DBG_UART, (uint32_t)(uint8_t)ch);
    while(RESET == usart_flag_get(DBG_UART, USART_FLAG_TBE)) {
    }
    return ch;
}


/*!
    \brief      enable the CPU cache
    \param[in]  none
    \param[out] none
    \retval     none
*/
void cache_enable(void)
{
    /* enable i-cache */
    SCB_EnableICache();
    /* enable d-cache */
    SCB_EnableDCache();
}

/*!
    \brief      configure the MPU attributes
    \param[in]  none
    \param[out] none
    \retval     none
*/
void mpu_config(void)
{
    mpu_region_init_struct mpu_init_struct;
    mpu_region_struct_para_init(&mpu_init_struct);

    /* disable the MPU */
    ARM_MPU_Disable();
    ARM_MPU_SetRegion(0, 0);

    /* configure the MPU attributes for the entire 4GB area, Reserved, no access */
    /* This configuration is highly recommended to prevent Speculative Prefetching of external memory, 
       which may cause CPU read locks and even system errors */
    mpu_init_struct.region_base_address  = 0x0;
    mpu_init_struct.region_size          = MPU_REGION_SIZE_4GB;
    mpu_init_struct.access_permission    = MPU_AP_NO_ACCESS;
    mpu_init_struct.access_bufferable    = MPU_ACCESS_NON_BUFFERABLE;
    mpu_init_struct.access_cacheable     = MPU_ACCESS_NON_CACHEABLE;
    mpu_init_struct.access_shareable     = MPU_ACCESS_SHAREABLE;
    mpu_init_struct.region_number        = MPU_REGION_NUMBER0;
    mpu_init_struct.subregion_disable    = 0x87;
    mpu_init_struct.instruction_exec     = MPU_INSTRUCTION_EXEC_NOT_PERMIT;
    mpu_init_struct.tex_type             = MPU_TEX_TYPE0;
    mpu_region_config(&mpu_init_struct);
    mpu_region_enable();

    /*
     * Region 1: external SDRAM (MT48LC16M16A2, 32 MB)
     * 0xC0000000 - 0xC1FFFFFF
     *
     * Region 0 intentionally blocks speculative accesses over the external
     * address space. A higher-numbered MPU region has priority, so explicitly
     * allow normal read/write data accesses to the SDRAM window.
     *
     * Keep this region non-cacheable while bringing up and testing SDRAM.
     * This avoids D-cache hiding EXMC/SDRAM timing or wiring problems.
     */
    mpu_region_struct_para_init(&mpu_init_struct);
    mpu_init_struct.region_base_address = SDRAM_BASE_ADDR;
    mpu_init_struct.region_size         = MPU_REGION_SIZE_32MB;
    mpu_init_struct.access_permission   = MPU_AP_FULL_ACCESS;
    mpu_init_struct.access_bufferable   = MPU_ACCESS_NON_BUFFERABLE;
    mpu_init_struct.access_cacheable    = MPU_ACCESS_NON_CACHEABLE;
    mpu_init_struct.access_shareable    = MPU_ACCESS_SHAREABLE;
    mpu_init_struct.region_number       = MPU_REGION_NUMBER1;
    mpu_init_struct.subregion_disable   = 0x00U;
    mpu_init_struct.instruction_exec    = MPU_INSTRUCTION_EXEC_NOT_PERMIT;
    mpu_init_struct.tex_type            = MPU_TEX_TYPE1;
    mpu_region_config(&mpu_init_struct);
    mpu_region_enable();

    /* enable the MPU */
    ARM_MPU_Enable(MPU_MODE_PRIV_DEFAULT);
}

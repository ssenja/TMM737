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
static uint32_t gpio_af_read(uint32_t gpio_periph, uint32_t pin);
static void sdram_register_dump(void);
static void sdram_base_stability_test(void);

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
    uint32_t fw_ver = 0;
#endif /* __FIRMWARE_VERSION_DEFINE */
    /* enable the CPU cache */
    cache_enable();
    mpu_config();
    /* configure systick */
    systick_config();

    /* initialize debug UART: PD5 = USART1_TX, PD6 = USART1_RX */
    dbg_uart_init();
    printf("\r\n\r\n=== TMM384G SDRAM TEST ===\r\n");
    printf("DBG UART : USART1 / PD5-TX PD6-RX / %lu baud\r\n",
           (unsigned long)DBG_UART_BAUDRATE);

    /* initialize external SDRAM (MT48LC16M16A2B4-6A) */
    sdram_init();
    /* initialize the LEDs, USART and key */
//    gd_eval_led_init(LED1);
//    gd_eval_led_init(LED2);
//    gd_eval_com_init(EVAL_COM);
//    gd_eval_key_init(KEY_WAKEUP, KEY_MODE_GPIO);

#ifdef __FIRMWARE_VERSION_DEFINE
    fw_ver = gd32h73x_75x_firmware_version_get();
    /* print firmware version */
    printf("\r\nGD32H7XX series firmware version: V%d.%d.%d", (uint8_t)(fw_ver >> 24), (uint8_t)(fw_ver >> 16), (uint8_t)(fw_ver >> 8));
#endif /* __FIRMWARE_VERSION_DEFINE */

    /* print out the clock frequency of system, AHB, APB1 and APB2 */
    printf("\r\nCK_SYS is %d", rcu_clock_freq_get(CK_SYS));
    printf("\r\nCK_AHB is %d", rcu_clock_freq_get(CK_AHB));
    printf("\r\nCK_APB1 is %d", rcu_clock_freq_get(CK_APB1));
    printf("\r\nCK_APB2 is %d", rcu_clock_freq_get(CK_APB2));

    sdram_register_dump();
    sdram_base_stability_test();

    /*
     * SDRAM bring-up diagnostic at the first half-word.
     * Print the actual value read back for several patterns before running
     * the wider memory test.  This makes data-bus / command / timing faults
     * visible on DBG_UART instead of reporting only PASS/FAIL.
     */
    {
        static const uint16_t diag_pattern[] = {
            0x0000U, 0xFFFFU, 0xAAAAU, 0x5555U, 0x1234U, 0xA5A5U, 0x5A5AU
        };
        uint32_t diag_i;
        uint16_t diag_read;

        printf("\r\nSDRAM diagnostic @ 0x%08lX\r\n",
               (unsigned long)SDRAM_BASE_ADDR);

        for(diag_i = 0U;
            diag_i < (sizeof(diag_pattern) / sizeof(diag_pattern[0]));
            ++diag_i) {
            sdram_write16(0U, diag_pattern[diag_i]);
            __DSB();
            diag_read = sdram_read16(0U);
            __DSB();

            printf("WRITE 0x%04X -> READ 0x%04X : %s\r\n",
                   (unsigned int)diag_pattern[diag_i],
                   (unsigned int)diag_read,
                   (diag_read == diag_pattern[diag_i]) ? "OK" : "FAIL");
        }
    }

    /*
     * SDRAM geometry diagnostic for MT48LC16M16A2:
     * 512 columns x 8192 rows x 4 banks x 16 bits.
     * CPU byte offsets for a 16-bit SDRAM:
     *   column A0..A8 : 2^(1..9)
     *   row    A0..A12: 2^(10..22)
     *   bank   BA0..BA1: 2^(23..24)
     *
     * Each bit is tested independently against offset 0 so a failing/aliased
     * address cannot contaminate the following test.
     */
    {
        uint32_t bit;
        uint32_t offset;
        uint32_t failures = 0U;
        uint16_t base_read;
        uint16_t target_read;
        const char *group;
        uint32_t signal;

        printf("\r\n=== SDRAM GEOMETRY ADDRESS TEST ===\r\n");

        for(bit = 1U; bit <= 24U; ++bit) {
            offset = (1UL << bit);

            if(bit <= 9U) {
                group = "COLUMN A";
                signal = bit - 1U;
            } else if(bit <= 22U) {
                group = "ROW A";
                signal = bit - 10U;
            } else {
                group = "BANK BA";
                signal = bit - 23U;
            }

            /*
             * First prove base and target can hold opposite values.
             * Restore both locations after every bit test.
             */
            sdram_write16(0U, 0xAAAAU);
            sdram_write16(offset, 0x5555U);
            __DSB();

            base_read = sdram_read16(0U);
            target_read = sdram_read16(offset);
            __DSB();

            if((base_read == 0xAAAAU) && (target_read == 0x5555U)) {
                printf("%s%lu OFFSET=0x%08lX : PASS\r\n",
                       group, (unsigned long)signal, (unsigned long)offset);
            } else {
                printf("%s%lu OFFSET=0x%08lX : FAIL BASE=%04X TARGET=%04X\r\n",
                       group, (unsigned long)signal, (unsigned long)offset,
                       (unsigned int)base_read, (unsigned int)target_read);
                ++failures;
            }

            /* Reverse the patterns to catch stuck-high/stuck-low aliasing. */
            sdram_write16(0U, 0x5555U);
            sdram_write16(offset, 0xAAAAU);
            __DSB();

            base_read = sdram_read16(0U);
            target_read = sdram_read16(offset);
            __DSB();

            if((base_read != 0x5555U) || (target_read != 0xAAAAU)) {
                printf("  REVERSE : FAIL BASE=%04X TARGET=%04X\r\n",
                       (unsigned int)base_read, (unsigned int)target_read);
                ++failures;
            }

            sdram_write16(0U, 0x0000U);
            sdram_write16(offset, 0x0000U);
            __DSB();
        }

        printf("GEOMETRY ADDRESS RESULT : %s (%lu)\r\n",
               (failures == 0U) ? "PASS" : "FAIL",
               (unsigned long)failures);
        printf("===================================\r\n");
    }

    /*
     * Software-only address signature diagnostic.
     * Write a unique value to each power-of-two half-word offset, then
     * read all locations back after every write has completed.  This
     * exposes aliasing/address-collapse without an oscilloscope.
     */
    {
        static const uint32_t sig_offset[] = {
            0x00000000U,
            0x00000002U, 0x00000004U, 0x00000008U, 0x00000010U,
            0x00000020U, 0x00000040U, 0x00000080U, 0x00000100U,
            0x00000200U, 0x00000400U, 0x00000800U, 0x00001000U,
            0x00002000U, 0x00004000U, 0x00008000U, 0x00010000U,
            0x00020000U, 0x00040000U, 0x00080000U, 0x00100000U,
            0x00200000U, 0x00400000U, 0x00800000U, 0x01000000U
        };
        uint32_t i;
        uint32_t count = sizeof(sig_offset) / sizeof(sig_offset[0]);
        uint32_t failures = 0U;
        uint16_t expected;
        uint16_t actual;

        printf("\r\n=== SDRAM ADDRESS SIGNATURE TEST ===\r\n");

        for(i = 0U; i < count; ++i) {
            expected = (uint16_t)(0x6000U + i);
            sdram_write16(sig_offset[i], expected);
            __DSB();
        }

        for(i = 0U; i < count; ++i) {
            expected = (uint16_t)(0x6000U + i);
            actual = sdram_read16(sig_offset[i]);
            printf("OFF=0x%08lX EXP=%04X READ=%04X : %s\r\n",
                   (unsigned long)sig_offset[i],
                   (unsigned int)expected,
                   (unsigned int)actual,
                   (actual == expected) ? "OK" : "FAIL");
            if(actual != expected) {
                ++failures;
            }
        }

        printf("SIGNATURE RESULT : %s (%lu/%lu FAIL)\r\n",
               (failures == 0U) ? "PASS" : "FAIL",
               (unsigned long)failures,
               (unsigned long)count);
        printf("====================================\r\n");
    }

    while(1)
    {
        /* Diagnostic complete. */
    }

}

/*!
    \brief      initialize debug UART (USART1)
    \param[in]  none
    \param[out] none
    \retval     none
*/
static uint32_t gpio_af_read(uint32_t gpio_periph, uint32_t pin)
{
    uint32_t index, shift;
    for(index=0U; index<16U; ++index) {
        if(pin==(1UL<<index)) {
            if(index<8U) { shift=index*4U; return (GPIO_AFSEL0(gpio_periph)>>shift)&0xFU; }
            shift=(index-8U)*4U; return (GPIO_AFSEL1(gpio_periph)>>shift)&0xFU;
        }
    }
    return 0xFFFFFFFFU;
}

static void sdram_register_dump(void)
{
    static const uint32_t pf_pin[]={GPIO_PIN_0,GPIO_PIN_1,GPIO_PIN_2,GPIO_PIN_3,GPIO_PIN_4,GPIO_PIN_5,GPIO_PIN_12,GPIO_PIN_13,GPIO_PIN_14,GPIO_PIN_15};
    static const uint32_t pf_num[]={0U,1U,2U,3U,4U,5U,12U,13U,14U,15U};
    uint32_t i;
    printf("\r\n=== EXMC REGISTER DUMP ===\r\n");
    printf("SDCTL0 = 0x%08lX\r\n",(unsigned long)EXMC_SDCTL0);
    printf("SDTCFG0= 0x%08lX\r\n",(unsigned long)EXMC_SDTCFG0);
    printf("SDCMD  = 0x%08lX\r\n",(unsigned long)EXMC_SDCMD);
    printf("SDARI  = 0x%08lX\r\n",(unsigned long)EXMC_SDARI);
    printf("SDSTAT = 0x%08lX\r\n",(unsigned long)EXMC_SDSTAT);
    printf("SDRSCTL= 0x%08lX\r\n",(unsigned long)EXMC_SDRSCTL);
    printf("\r\n=== GPIO AF CHECK ===\r\n");
    for(i=0U;i<10U;++i) printf("A%lu PF%lu AF=%lu\r\n",(unsigned long)i,(unsigned long)pf_num[i],(unsigned long)gpio_af_read(GPIOF,pf_pin[i]));
    printf("A10 PG0 AF=%lu\r\n",(unsigned long)gpio_af_read(GPIOG,GPIO_PIN_0));
    printf("A11 PG1 AF=%lu\r\n",(unsigned long)gpio_af_read(GPIOG,GPIO_PIN_1));
    printf("A12 PG2 AF=%lu\r\n",(unsigned long)gpio_af_read(GPIOG,GPIO_PIN_2));
    printf("BA0 PG4 AF=%lu BA1 PG5 AF=%lu\r\n",(unsigned long)gpio_af_read(GPIOG,GPIO_PIN_4),(unsigned long)gpio_af_read(GPIOG,GPIO_PIN_5));
    printf("RAS PF11 AF=%lu CAS PG15 AF=%lu WE PH5 AF=%lu\r\n",(unsigned long)gpio_af_read(GPIOF,GPIO_PIN_11),(unsigned long)gpio_af_read(GPIOG,GPIO_PIN_15),(unsigned long)gpio_af_read(GPIOH,GPIO_PIN_5));
    printf("CS PH3 AF=%lu CKE PH2 AF=%lu CLK PG8 AF=%lu\r\n",(unsigned long)gpio_af_read(GPIOH,GPIO_PIN_3),(unsigned long)gpio_af_read(GPIOH,GPIO_PIN_2),(unsigned long)gpio_af_read(GPIOG,GPIO_PIN_8));
}

static void sdram_base_stability_test(void)
{
    uint32_t i, failures=0U;
    uint16_t value;
    printf("\r\n=== SDRAM BASE STABILITY TEST ===\r\n");
    for(i=0U;i<16U;++i) {
        sdram_write16(0U,0x0000U); __DSB();
        value=sdram_read16(0U); __DSB();
        printf("%02lu WRITE=0000 READ=%04X : %s\r\n",(unsigned long)i,(unsigned int)value,(value==0U)?"OK":"FAIL");
        if(value!=0U) ++failures;
    }
    printf("BASE STABILITY RESULT : %s (%lu/16 FAIL)\r\n",(failures==0U)?"PASS":"FAIL",(unsigned long)failures);
}

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

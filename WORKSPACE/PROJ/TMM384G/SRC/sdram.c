/*!
    \file    sdram.c
    \brief   EXMC SDRAM driver for MT48LC16M16A2B4-6A (16M x 16, 256Mbit)
*/

#include "sdram.h"
#include "systick.h"

#define SDRAM_MODE_BURST_LENGTH_1       0x0000U
#define SDRAM_MODE_BURST_TYPE_SEQUENTIAL 0x0000U
#define SDRAM_MODE_CAS_LATENCY_3        0x0030U
#define SDRAM_MODE_STANDARD_OPERATION   0x0000U
#define SDRAM_MODE_WRITE_BURST_SINGLE   0x0200U
#define SDRAM_MODE_REGISTER             (SDRAM_MODE_BURST_LENGTH_1 | \
                                         SDRAM_MODE_BURST_TYPE_SEQUENTIAL | \
                                         SDRAM_MODE_CAS_LATENCY_3 | \
                                         SDRAM_MODE_STANDARD_OPERATION | \
                                         SDRAM_MODE_WRITE_BURST_SINGLE)

/*
 * The board schematic connects:
 * D0..D15   : PD14,PD15,PD0,PD1,PE7..PE15,PD8..PD10
 * A0..A12   : PF0..PF5,PF12..PF15,PG0..PG2
 * BA0/BA1   : PG4/PG5
 * SDNRAS    : PF11
 * NBL0/1    : PE0/PE1
 * SDCKE0    : PH2
 * SDNE0     : PH3
 * SDCLK     : PG8
 * SDNCAS    : PG15
 * SDNWE     : PH5
 *
 * These signals are EXMC alternate function 12 on GD32H737.
 */
static void sdram_gpio_config(void)
{
    const uint32_t af = GPIO_AF_12;
    const uint32_t speed = GPIO_OSPEED_100_220MHZ;

    rcu_periph_clock_enable(RCU_GPIOD);\n    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_GPIOF);
    rcu_periph_clock_enable(RCU_GPIOG);
    rcu_periph_clock_enable(RCU_GPIOH);
    rcu_periph_clock_enable(RCU_EXMC);

    /* All SDRAM pins: AF, push-pull, no pull. */
    gpio_af_set(GPIOD, af, GPIO_PIN_0 | GPIO_PIN_1 |
                         GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                         GPIO_PIN_14 | GPIO_PIN_15);
    gpio_af_set(GPIOE, af, GPIO_PIN_0 | GPIO_PIN_1 |
                         GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                         GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
                         GPIO_PIN_15);
    gpio_af_set(GPIOF, af, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                         GPIO_PIN_4 | GPIO_PIN_5 |
                         GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
                         GPIO_PIN_14 | GPIO_PIN_15);
    gpio_af_set(GPIOG, af, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
                         GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15);
    gpio_af_set(GPIOH, af, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5);

    gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  GPIO_PIN_0 | GPIO_PIN_1 |
                  GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                  GPIO_PIN_14 | GPIO_PIN_15);
    gpio_mode_set(GPIOE, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  GPIO_PIN_0 | GPIO_PIN_1 |
                  GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                  GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
                  GPIO_PIN_15);
    gpio_mode_set(GPIOF, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                  GPIO_PIN_4 | GPIO_PIN_5 |
                  GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
                  GPIO_PIN_14 | GPIO_PIN_15);
    gpio_mode_set(GPIOG, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
                  GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15);
    gpio_mode_set(GPIOH, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5);

    gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, speed,
                            GPIO_PIN_0 | GPIO_PIN_1 |
                            GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                            GPIO_PIN_14 | GPIO_PIN_15);
    gpio_output_options_set(GPIOE, GPIO_OTYPE_PP, speed,
                            GPIO_PIN_0 | GPIO_PIN_1 |
                            GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                            GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
                            GPIO_PIN_15);
    gpio_output_options_set(GPIOF, GPIO_OTYPE_PP, speed,
                            GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |
                            GPIO_PIN_4 | GPIO_PIN_5 |
                            GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |
                            GPIO_PIN_14 | GPIO_PIN_15);
    gpio_output_options_set(GPIOG, GPIO_OTYPE_PP, speed,
                            GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 |
                            GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15);
    gpio_output_options_set(GPIOH, GPIO_OTYPE_PP, speed,
                            GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_5);
}

void sdram_init(void)
{
    exmc_sdram_timing_parameter_struct timing;
    exmc_sdram_parameter_struct sdram;
    exmc_sdram_command_parameter_struct command;

    sdram_gpio_config();

    /*
     * System clock in this project is 600MHz.
     * AHB = 300MHz and EXMC clock is selected from AHB.
     * SDCLK = CK_EXMC / 2 = 150MHz, safely below the SDRAM's 166MHz rating.
     */
    rcu_exmc_clock_config(RCU_EXMCSRC_AHB);

    timing.row_to_column_delay    = 3U;   /* tRCD >= 18ns -> 3 clocks at 150MHz */
    timing.row_precharge_delay    = 3U;   /* tRP  >= 18ns -> 3 clocks */
    timing.write_recovery_delay   = 2U;   /* tWR = 2 clocks */
    timing.auto_refresh_delay     = 9U;   /* tRC  >= 60ns -> 9 clocks */
    timing.row_address_select_delay = 7U; /* tRAS >= 42ns -> 7 clocks */
    timing.exit_selfrefresh_delay = 11U;  /* tXSR >= 72ns -> 11 clocks */
    timing.load_mode_register_delay = 2U; /* tMRD = 2 clocks */

    exmc_sdram_struct_para_init(&sdram);
    sdram.sdram_device        = EXMC_SDRAM_DEVICE0;
    sdram.pipeline_read_delay = EXMC_PIPELINE_DELAY_0_CK_EXMC;
    sdram.burst_read_switch   = DISABLE;
    sdram.sdclock_config      = EXMC_SDCLK_PERIODS_2_CK_EXMC;
    sdram.write_protection    = DISABLE;
    sdram.cas_latency         = EXMC_CAS_LATENCY_3_SDCLK;
    sdram.internal_bank_number = EXMC_SDRAM_4_INTER_BANK;
    sdram.data_width          = EXMC_SDRAM_DATABUS_WIDTH_16B;
    sdram.row_address_width   = EXMC_SDRAM_ROW_ADDRESS_13;
    sdram.column_address_width = EXMC_SDRAM_COW_ADDRESS_9;
    sdram.timing              = &timing;

    exmc_sdram_init(&sdram);

    /* SDRAM power-up stabilization. */
    delay_1ms(1U);

    /* Clock enable command. */
    command.mode_register_content = 0U;
    command.auto_refresh_number = EXMC_SDRAM_AUTO_REFLESH_1_SDCLK;
    command.bank_select = EXMC_SDRAM_DEVICE0_SELECT;
    command.command = EXMC_SDRAM_CLOCK_ENABLE;
    exmc_sdram_command_config(&command);
    delay_1ms(1U);

    /* Precharge all banks. */
    command.command = EXMC_SDRAM_PRECHARGE_ALL;
    exmc_sdram_command_config(&command);

    /* Two auto-refresh commands. */
    command.command = EXMC_SDRAM_AUTO_REFRESH;
    command.auto_refresh_number = EXMC_SDRAM_AUTO_REFLESH_2_SDCLK;
    exmc_sdram_command_config(&command);

    /* Load SDRAM mode register: BL=1, sequential, CL=3, single write burst. */
    command.command = EXMC_SDRAM_LOAD_MODE_REGISTER;
    command.auto_refresh_number = EXMC_SDRAM_AUTO_REFLESH_1_SDCLK;
    command.mode_register_content = SDRAM_MODE_REGISTER;
    exmc_sdram_command_config(&command);

    /* Normal operation. */
    command.command = EXMC_SDRAM_NORMAL_OPERATION;
    command.mode_register_content = 0U;
    exmc_sdram_command_config(&command);

    /*
     * 8192 rows / 64ms. At 150MHz:
     * 150,000,000 * 64ms / 8192 = 1171.875 clocks.
     */
    exmc_sdram_refresh_count_set(1172U);
}

/* Byte address must be half-word aligned. */
void sdram_write16(uint32_t address, uint16_t data)
{
    *(volatile uint16_t *)(SDRAM_BASE_ADDR + address) = data;
}

uint16_t sdram_read16(uint32_t address)
{
    return *(volatile uint16_t *)(SDRAM_BASE_ADDR + address);
}

void sdram_write_buffer(uint32_t address, const uint16_t *buffer, uint32_t count)
{
    volatile uint16_t *dst = (volatile uint16_t *)(SDRAM_BASE_ADDR + address);
    while(count--) {
        *dst++ = *buffer++;
    }
}

void sdram_read_buffer(uint32_t address, uint16_t *buffer, uint32_t count)
{
    volatile uint16_t *src = (volatile uint16_t *)(SDRAM_BASE_ADDR + address);
    while(count--) {
        *buffer++ = *src++;
    }
}

/*
 * Basic hardware test.
 * It checks multiple banks, row/column combinations and the end of memory.
 * Return 0 = PASS.
 */
uint32_t sdram_memory_test(void)
{
    static const uint32_t test_addr[] = {
        0x000000U, 0x000002U, 0x000100U, 0x000400U,
        0x001000U, 0x010000U, 0x100000U, 0x200000U,
        0x400000U, 0x800000U, 0x1000000U, 0x1FFFFFCU
    };
    static const uint16_t pattern[] = {
        0x0000U, 0xFFFFU, 0xAAAAU, 0x5555U,
        0x1234U, 0x5678U, 0xA5A5U, 0x5A5AU,
        0x0F0FU, 0xF0F0U, 0x55AAU, 0xAA55U
    };
    uint32_t i;

    for(i = 0U; i < (sizeof(test_addr) / sizeof(test_addr[0])); ++i) {
        sdram_write16(test_addr[i], pattern[i]);
    }

    for(i = 0U; i < (sizeof(test_addr) / sizeof(test_addr[0])); ++i) {
        if(sdram_read16(test_addr[i]) != pattern[i]) {
            return test_addr[i] + 1U;
        }
    }

    /* Address/data interaction test over 1MB, one word every 256 bytes. */
    for(i = 0U; i < 0x100000U; i += 0x100U) {
        sdram_write16(i, (uint16_t)((i >> 1) ^ 0x5AA5U));
    }
    for(i = 0U; i < 0x100000U; i += 0x100U) {
        if(sdram_read16(i) != (uint16_t)((i >> 1) ^ 0x5AA5U)) {
            return i + 1U;
        }
    }

    return 0U;
}

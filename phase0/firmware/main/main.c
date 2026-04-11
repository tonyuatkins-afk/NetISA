/**
 * NetISA Phase 0 - ESP32-S3 Parallel Bus Handler
 *
 * Minimal firmware for bus interface validation.
 * Handles CPLD parallel bus transactions and provides USB serial logging.
 *
 * Hardware: ESP32-S3-DevKitC-1U connected to ATF1508AS via ribbon cable.
 * Build:    ESP-IDF v5.x, idf.py build flash monitor
 *
 * GPIO Assignments (match CPLD netisa.pld):
 *   PD0-PD7:  GPIO4-GPIO11  (parallel data, bidirectional)
 *   PA0-PA3:  GPIO12-GPIO15 (register address, input from CPLD)
 *   PRW:      GPIO16        (read/write direction, input from CPLD)
 *   PSTROBE:  GPIO17        (data strobe, input from CPLD, active LOW)
 *   PREADY:   GPIO18        (data valid, output to CPLD)
 *   PIRQ:     GPIO38        (interrupt request, output to CPLD)
 *   PBOOT:    GPIO21        (boot complete, output to CPLD)
 *
 *   GPIO19/GPIO20: RESERVED for USB D-/D+ (serial console). Do NOT use.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "soc/gpio_struct.h"

static const char *TAG = "netisa";

/* ===== GPIO Pin Definitions ===== */
#define GPIO_PD0        4
#define GPIO_PD1        5
#define GPIO_PD2        6
#define GPIO_PD3        7
#define GPIO_PD4        8
#define GPIO_PD5        9
#define GPIO_PD6        10
#define GPIO_PD7        11
#define GPIO_PA0        12
#define GPIO_PA1        13
#define GPIO_PA2        14
#define GPIO_PA3        15
#define GPIO_PRW        16
#define GPIO_PSTROBE    17
#define GPIO_PREADY     18
#define GPIO_PIRQ       38      /* NOT GPIO19 (USB D-) or GPIO20 (USB D+) */
#define GPIO_PBOOT      21

/* Data bus pin mask for bulk GPIO operations */
#define PD_PIN_MASK     ((1ULL << GPIO_PD0) | (1ULL << GPIO_PD1) | \
                         (1ULL << GPIO_PD2) | (1ULL << GPIO_PD3) | \
                         (1ULL << GPIO_PD4) | (1ULL << GPIO_PD5) | \
                         (1ULL << GPIO_PD6) | (1ULL << GPIO_PD7))

/* ===== Register File ===== */
/* Mirrors the ISA register map. CPLD reads from / writes to these. */
#define REG_COUNT       16

static volatile uint8_t registers[REG_COUNT] = {
    0x00,   /* 0x00: Status (bit 6=BOOT, bit 5=SAFE) */
    0x00,   /* 0x01: Response Length Lo */
    0x00,   /* 0x02: Response Length Hi */
    0x00,   /* 0x03: Reserved */
    0x00,   /* 0x04: Data Out (bulk) */
    0x00,   /* 0x05: Data Out Hi (16-bit) */
    0x00,   /* 0x06: Error Code */
    0x01,   /* 0x07: FW Version Major */
    0x00,   /* 0x08: FW Version Minor */
    0x01,   /* 0x09: FW Version Patch (v1.0.1) */
    0x00,   /* 0x0A: Session Count */
    0x04,   /* 0x0B: Session Capacity (4 sessions) */
    0x00,   /* 0x0C: Network Status (0=disconnected) */
    0x00,   /* 0x0D: Signal Quality */
    0x00,   /* 0x0E: Reserved */
    0x00,   /* 0x0F: Reserved */
};

/* ===== Statistics for serial console ===== */
static volatile uint32_t stat_writes = 0;
static volatile uint32_t stat_reads = 0;
static volatile uint32_t stat_errors = 0;
static volatile uint32_t last_write_reg = 0;
static volatile uint32_t last_write_val = 0;
static volatile uint32_t last_read_reg = 0;

/* ===== Loopback Test Support ===== */
/* Register 0x04 write stores to loopback buffer, 0x04 read returns it. */
/* This enables the DOS loopback test: write a byte, read it back. */
static volatile uint8_t loopback_byte = 0x00;

/* ===== Driver Mode Scaffolding (Reserved) =====
 *
 * v1 firmware implements Session Mode only. NIC Mode (v2.0) and NIC +
 * kTLS Offload Mode (v2.5) are reserved for future firmware releases on
 * the same hardware. The CMD_SET_MODE command opcode is recognized here
 * in Phase 0 so that future host drivers (Windows NDIS, Linux net_device,
 * BSD network drivers) attempting to probe for advanced-mode support
 * receive a clean ERR_MODE_UNSUPPORTED response instead of silent failure.
 *
 * Protocol (host side):
 *   1. Host writes mode byte to reg 0x04 (Data In port)
 *   2. Host writes CMD_SET_MODE opcode to reg 0x00 (Command Register)
 *   3. Host reads reg 0x06 (Error Code) to check result
 *
 * See architecture spec section 2.6.1 "Driver Modes".
 */
#define CMD_SET_MODE            0x10
#define MODE_SESSION            0x00  /* v1 default */
#define MODE_NIC                0x01  /* v2.0 reserved */
#define MODE_NIC_KTLS           0x02  /* v2.5 reserved */

#define ERR_SUCCESS             0x00
#define ERR_MODE_UNSUPPORTED    0x15

/* Current driver mode. Always MODE_SESSION in v1. */
static volatile uint8_t current_mode = MODE_SESSION;

/* ===== Fast GPIO Read/Write Helpers ===== */

/**
 * Read 8-bit value from PD0-PD7 (GPIO4-GPIO11).
 * Uses direct register access for speed in ISR context.
 */
static inline uint8_t IRAM_ATTR read_parallel_data(void)
{
    uint32_t gpio_in = GPIO.in;
    uint8_t val = 0;
    val |= ((gpio_in >> GPIO_PD0) & 1) << 0;
    val |= ((gpio_in >> GPIO_PD1) & 1) << 1;
    val |= ((gpio_in >> GPIO_PD2) & 1) << 2;
    val |= ((gpio_in >> GPIO_PD3) & 1) << 3;
    val |= ((gpio_in >> GPIO_PD4) & 1) << 4;
    val |= ((gpio_in >> GPIO_PD5) & 1) << 5;
    val |= ((gpio_in >> GPIO_PD6) & 1) << 6;
    val |= ((gpio_in >> GPIO_PD7) & 1) << 7;
    return val;
}

/**
 * Write 8-bit value to PD0-PD7 (GPIO4-GPIO11).
 * Sets direction to output, writes value, then returns to input.
 */
static inline void IRAM_ATTR write_parallel_data(uint8_t val)
{
    uint32_t set_mask = 0;
    uint32_t clr_mask = 0;

    for (int i = 0; i < 8; i++) {
        int pin = GPIO_PD0 + i;
        if (val & (1 << i)) {
            set_mask |= (1 << pin);
        } else {
            clr_mask |= (1 << pin);
        }
    }

    /* Set PD0-PD7 as outputs */
    GPIO.enable_w1ts = PD_PIN_MASK;

    /* Drive the value */
    if (set_mask) GPIO.out_w1ts = set_mask;
    if (clr_mask) GPIO.out_w1tc = clr_mask;
}

/**
 * Release PD0-PD7 back to input mode.
 */
static inline void IRAM_ATTR release_parallel_data(void)
{
    GPIO.enable_w1tc = PD_PIN_MASK;
}

/**
 * Read register address from PA0-PA3 (GPIO12-GPIO15).
 */
static inline uint8_t IRAM_ATTR read_reg_addr(void)
{
    uint32_t gpio_in = GPIO.in;
    uint8_t addr = 0;
    addr |= ((gpio_in >> GPIO_PA0) & 1) << 0;
    addr |= ((gpio_in >> GPIO_PA1) & 1) << 1;
    addr |= ((gpio_in >> GPIO_PA2) & 1) << 2;
    addr |= ((gpio_in >> GPIO_PA3) & 1) << 3;
    return addr;
}

/**
 * Read PRW signal (GPIO16). HIGH = CPLD requesting read, LOW = write.
 */
static inline int IRAM_ATTR read_prw(void)
{
    return (GPIO.in >> GPIO_PRW) & 1;
}

/* ===== PSTROBE ISR: Core 0, highest priority ===== */

static void IRAM_ATTR pstrobe_isr(void *arg)
{
    /* Clear interrupt */
    gpio_intr_disable(GPIO_PSTROBE);

    uint8_t reg = read_reg_addr();
    int is_read = read_prw();

    if (is_read) {
        /* CPLD is requesting data for a non-cached read */
        uint8_t val;

        if (reg == 0x04) {
            /* Data port: return loopback byte (Phase 0) */
            val = loopback_byte;
        } else {
            /* General register read */
            val = (reg < REG_COUNT) ? registers[reg] : 0xFF;
        }

        /* Drive data onto PD0-PD7 */
        write_parallel_data(val);

        /* Assert PREADY (HIGH = data valid) */
        GPIO.out_w1ts = (1 << GPIO_PREADY);

        /* Brief hold for CPLD 2-flop synchronizer (>125ns at 16 MHz) */
        /* At 240 MHz, ~30 NOPs = 125ns. Be generous. */
        for (volatile int i = 0; i < 60; i++) { __asm__ volatile("nop"); }

        /* Deassert PREADY */
        GPIO.out_w1tc = (1 << GPIO_PREADY);

        /* Release data bus */
        release_parallel_data();

        stat_reads++;
        last_read_reg = reg;

    } else {
        /* CPLD is sending write data from host */
        uint8_t val = read_parallel_data();

        if (reg == 0x04) {
            /* Data port: store for loopback AND as command payload.
             * In Phase 0 these are the same byte; in v1+ firmware they
             * live in separate command/data queues. */
            loopback_byte = val;
        } else if (reg == 0x00) {
            /* Command register write. Recognize the CMD_SET_MODE opcode
             * reserved for future driver-mode switching (see spec
             * section 2.6.1). All other opcodes are stored for Phase 0
             * observability and handled by higher-level firmware in v1+.
             *
             * IRAM_ATTR ISR context: NO logging here, only register ops. */
            if (val == CMD_SET_MODE) {
                uint8_t requested = loopback_byte;
                if (requested == MODE_SESSION) {
                    /* Already in session mode; acknowledge success. */
                    current_mode = requested;
                    registers[0x06] = ERR_SUCCESS;
                } else {
                    /* v1 firmware does not implement NIC or NIC+kTLS modes.
                     * Return a defined error so future drivers can probe
                     * for advanced-mode support without silent failure. */
                    registers[0x06] = ERR_MODE_UNSUPPORTED;
                    stat_errors++;
                }
            }
            registers[0] = val;
        } else if (reg == 0x07 && val == 0xFF) {
            /* Reset command: reboot ESP32 */
            esp_restart();
        } else if (reg < REG_COUNT) {
            registers[reg] = val;
        }

        stat_writes++;
        last_write_reg = reg;
        last_write_val = val;
    }

    gpio_intr_enable(GPIO_PSTROBE);
}

/* ===== GPIO Initialization ===== */

static void init_gpio(void)
{
    gpio_config_t io_conf;

    /* PD0-PD7: Start as inputs (ESP32 reads from CPLD by default) */
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = PD_PIN_MASK;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    /* PA0-PA3, PRW: Inputs from CPLD */
    uint64_t addr_mask = (1ULL << GPIO_PA0) | (1ULL << GPIO_PA1) |
                         (1ULL << GPIO_PA2) | (1ULL << GPIO_PA3) |
                         (1ULL << GPIO_PRW);
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = addr_mask;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    /* PSTROBE: Input with interrupt on falling edge */
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_PSTROBE);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;  /* Idle HIGH */
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    /* PREADY, PIRQ, PBOOT: Outputs to CPLD, initially LOW */
    uint64_t out_mask = (1ULL << GPIO_PREADY) | (1ULL << GPIO_PIRQ) |
                        (1ULL << GPIO_PBOOT);
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = out_mask;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level(GPIO_PREADY, 0);
    gpio_set_level(GPIO_PIRQ, 0);
    gpio_set_level(GPIO_PBOOT, 0);

    /* Install ISR service and attach PSTROBE handler on Core 0 */
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM | ESP_INTR_FLAG_LEVEL3);
    gpio_isr_handler_add(GPIO_PSTROBE, pstrobe_isr, NULL);

    ESP_LOGI(TAG, "GPIO initialized. PSTROBE ISR on Core 0.");
}

/* ===== USB Serial Console Task (Core 1) ===== */

static void console_task(void *arg)
{
    uint32_t prev_writes = 0;
    uint32_t prev_reads = 0;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " NetISA Phase 0 - Bus Validation");
    ESP_LOGI(TAG, " Firmware v%d.%d.%d",
             registers[0x07], registers[0x08], registers[0x09]);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Waiting for ISA bus transactions...");
    ESP_LOGI(TAG, "(Use DOS test program: NISATEST.COM)");
    ESP_LOGI(TAG, "");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        uint32_t w = stat_writes;
        uint32_t r = stat_reads;
        uint32_t e = stat_errors;

        /* Only print if activity occurred */
        if (w != prev_writes || r != prev_reads) {
            ESP_LOGI(TAG, "Stats: W=%lu R=%lu E=%lu | "
                     "Last W: reg=0x%02lX val=0x%02lX | "
                     "Last R: reg=0x%02lX | Loopback=0x%02X | Mode=%u",
                     w, r, e,
                     last_write_reg, last_write_val,
                     last_read_reg, loopback_byte, current_mode);
        }

        prev_writes = w;
        prev_reads = r;
    }
}

/* ===== Crashlog: Save backtrace to NVS on panic ===== */

static void init_crashlog(void)
{
    /* Core dump analysis deferred to 'idf.py coredump-info' command.
     * The esp_core_dump_summary API changed significantly in ESP-IDF v5.5.
     * For Phase 0 validation, USB serial console captures all crash output. */
    ESP_LOGI(TAG, "Crashlog: use 'idf.py coredump-info' if a crash occurred.");
}

/* ===== Main Entry Point ===== */

void app_main(void)
{
    /* Initialize NVS (for future config storage) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* Check for previous crash */
    init_crashlog();

    /* Initialize parallel bus GPIO */
    init_gpio();

    /* Assert PBOOT: tell CPLD we are ready */
    gpio_set_level(GPIO_PBOOT, 1);
    registers[0x00] |= 0x40;  /* Set bit 6 = BOOT_COMPLETE */
    ESP_LOGI(TAG, "PBOOT asserted. Card ready for ISA transactions.");

    /* Start console reporting task on Core 1 */
    xTaskCreatePinnedToCore(console_task, "console", 4096, NULL, 5, NULL, 1);

    /* Core 0 is now free for ISR handling (PSTROBE) */
    /* Main task can idle or handle future command processing */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

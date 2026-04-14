/**
 * NetISA v1 Firmware - ESP32-S3 Main Entry Point
 *
 * Preserves the Phase 0 parallel bus ISR (pstrobe_isr) and register file
 * architecture. The ISA bus interface is unchanged. What changes is what
 * happens AFTER a command arrives via the parallel bus: instead of loopback,
 * commands are dispatched to real WiFi, HTTP, DNS, and crypto handlers.
 *
 * Core allocation:
 *   Core 0: PSTROBE ISR (highest priority, handles ISA bus transactions)
 *   Core 1: Command handler task, WiFi, HTTP, web config server
 *
 * Hardware: ESP32-S3-DevKitC-1U connected to ATF1508AS via ribbon cable.
 * Build:    ESP-IDF v5.5.4, idf.py build flash monitor
 *
 * GPIO Assignments (match CPLD netisa.v):
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
#include "esp_rom_sys.h"
#include "soc/gpio_struct.h"
#include "esp_timer.h"

#include "status.h"
#include "cmd_handler.h"
#include "nv_config.h"
#include "wifi_mgr.h"
#include "http_client.h"
#include "web_config.h"

static const char *TAG = "netisa";

/* ===== GPIO Pin Definitions (same as Phase 0) ===== */
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
#define GPIO_PIRQ       38
#define GPIO_PBOOT      21

/* Data bus pin mask for bulk GPIO operations */
#define PD_PIN_MASK     ((1ULL << GPIO_PD0) | (1ULL << GPIO_PD1) | \
                         (1ULL << GPIO_PD2) | (1ULL << GPIO_PD3) | \
                         (1ULL << GPIO_PD4) | (1ULL << GPIO_PD5) | \
                         (1ULL << GPIO_PD6) | (1ULL << GPIO_PD7))

/* ===== Register File ===== */
/* Mirrors the ISA register map. CPLD reads from / writes to these. */
#define REG_COUNT       16

/* Status register bit layout (reg 0x00) per spec section 2.6:
 *   bit 0: CMD_READY   (ESP32-managed)
 *   bit 1: RESP_READY  (ESP32-managed)
 *   bit 2: ASYNC_DATA  (ESP32-managed)
 *   bit 3: SAFE_MODE   (CPLD hardware flag from J3 jumper)
 *   bit 4: reserved (always 0)
 *   bit 5: XFER_TIMEOUT (CPLD hardware flag from watchdog)
 *   bit 6: BOOT_COMPLETE (CPLD hardware flag from PBOOT pin)
 *   bit 7: reserved (always 0)
 */
static volatile uint8_t registers[REG_COUNT] = {
    0x01,   /* 0x00: Status (CMD_READY=1) */
    0x00,   /* 0x01: Response Length Lo */
    0x00,   /* 0x02: Response Length Hi */
    0x00,   /* 0x03: Reserved */
    0x00,   /* 0x04: Data Out (bulk read) */
    0x00,   /* 0x05: Data Out Hi (16-bit) */
    0x00,   /* 0x06: Error Code */
    FW_VERSION_MAJOR,   /* 0x07: FW Version Major */
    FW_VERSION_MINOR,   /* 0x08: FW Version Minor */
    FW_VERSION_PATCH,   /* 0x09: FW Version Patch */
    0x00,   /* 0x0A: Session Count */
    MAX_SESSIONS,       /* 0x0B: Session Capacity */
    0x00,   /* 0x0C: Network Status (0=disconnected) */
    0x00,   /* 0x0D: Signal Quality */
    0x00,   /* 0x0E: Reserved */
    0x00,   /* 0x0F: Reserved */
};

/* ===== Statistics ===== */
static volatile uint32_t stat_writes = 0;
static volatile uint32_t stat_reads = 0;

/* ===== Command staging buffer ===== */
/* Multi-byte data transfer: host writes bytes to reg 0x04 sequentially,
 * building up a command in the staging buffer. A write to reg 0x00
 * (command register) triggers dispatch. */
#define STAGING_BUF_SIZE    256
static volatile uint8_t staging_buf[STAGING_BUF_SIZE];
static volatile int staging_len = 0;

/* ===== Response delivery buffer ===== */
/* After cmd_handler_task fills cmd_response, the ISR delivers response
 * bytes one at a time via register 0x04 reads. */
static volatile int resp_read_pos = 0;

/* Deferred restart flag (F4 fix: ISR sets, task executes) */
static volatile int restart_requested = 0;

/* Driver mode scaffolding (preserved from Phase 0) */
#define CMD_SET_MODE            0x10
#define MODE_SESSION            0x00
#define MODE_NIC                0x01
#define MODE_NIC_KTLS           0x02

static volatile uint8_t current_mode = MODE_SESSION;

/* ===== Fast GPIO Read/Write Helpers (same as Phase 0) ===== */

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

    /* Drive value FIRST, then enable outputs (no glitch window) */
    if (set_mask) GPIO.out_w1ts = set_mask;
    if (clr_mask) GPIO.out_w1tc = clr_mask;
    GPIO.enable_w1ts = PD_PIN_MASK;
}

static inline void IRAM_ATTR release_parallel_data(void)
{
    GPIO.enable_w1tc = PD_PIN_MASK;
}

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

static inline int IRAM_ATTR read_prw(void)
{
    return (GPIO.in >> GPIO_PRW) & 1;
}

/* ===== PSTROBE ISR: Core 0, highest priority ===== */

static void IRAM_ATTR pstrobe_isr(void *arg)
{
    (void)arg;

    gpio_intr_disable(GPIO_PSTROBE);

    uint8_t reg = read_reg_addr();
    int is_read = read_prw();

    if (is_read) {
        /* CPLD is requesting data */
        uint8_t val;

        if (reg == 0x04) {
            /* Data port: deliver response bytes sequentially.
             * S-04 fix: memory barrier BEFORE reading the flag ensures we
             * see the response data written by cmd_handler_task. */
            __sync_synchronize();
            if (cmd_response_ready && resp_read_pos < cmd_response.data_len) {
                val = cmd_response.data[resp_read_pos++];
                /* Update response length remaining in registers */
                int remaining = cmd_response.data_len - resp_read_pos;
                registers[0x01] = (uint8_t)(remaining & 0xFF);
                registers[0x02] = (uint8_t)((remaining >> 8) & 0xFF);
                if (resp_read_pos >= cmd_response.data_len) {
                    /* Response fully delivered */
                    cmd_response_ready = 0;
                    resp_read_pos = 0;
                    registers[0x00] |= 0x01;   /* CMD_READY */
                    registers[0x00] &= ~0x02;  /* Clear RESP_READY */
                }
            } else {
                val = 0x00;
            }
        } else if (reg == 0x00) {
            /* Status register: update RESP_READY bit from flag.
             * S-04 fix: memory barrier BEFORE reading the flag. */
            __sync_synchronize();
            if (cmd_response_ready) {
                registers[0x00] |= 0x02;   /* RESP_READY */
                /* S5 fix: report remaining bytes, not total, consistent
                 * with the reg 0x04 read path */
                int remaining = cmd_response.data_len - resp_read_pos;
                registers[0x01] = (uint8_t)(remaining & 0xFF);
                registers[0x02] = (uint8_t)((remaining >> 8) & 0xFF);
                registers[0x06] = (uint8_t)cmd_response.status;
            }
            val = registers[0x00];
        } else if (reg == 0x06) {
            /* Error code register: return status from last command */
            val = cmd_response_ready ? (uint8_t)cmd_response.status : NI_OK;
        } else {
            val = (reg < REG_COUNT) ? registers[reg] : 0xFF;
        }

        write_parallel_data(val);
        GPIO.out_w1ts = (1 << GPIO_PREADY);
        esp_rom_delay_us(1);
        GPIO.out_w1tc = (1 << GPIO_PREADY);
        release_parallel_data();

        stat_reads++;

    } else {
        /* CPLD is sending write data from host */
        uint8_t val = read_parallel_data();

        if (reg == 0x04) {
            /* Data port: accumulate into staging buffer */
            if (staging_len < STAGING_BUF_SIZE) {
                staging_buf[staging_len++] = val;
            }
        } else if (reg == 0x00) {
            /* Command register write: dispatch command */

            /* Handle driver mode probe (preserved from Phase 0) */
            if (val == CMD_SET_MODE) {
                uint8_t requested = (staging_len > 0) ? staging_buf[0] : MODE_SESSION;
                if (requested == MODE_SESSION) {
                    current_mode = requested;
                    registers[0x06] = NI_OK;
                } else {
                    registers[0x06] = NI_ERR_MODE_UNSUPPORTED;
                }
                staging_len = 0;
            } else {
                /* Build command request and enqueue for handler task.
                 * Copy full staging buffer into per-request data field
                 * for data-heavy commands (S1 fix: URLs can be ~256 bytes). */
                cmd_request_t req;
                req.group = (val >> 4) & 0x0F;
                req.function = val & 0x0F;
                req.param_len = (staging_len > CMD_PARAM_MAX) ?
                                CMD_PARAM_MAX : staging_len;
                for (int i = 0; i < req.param_len; i++) {
                    req.params[i] = staging_buf[i];
                }
                /* F-01 fix: copy full staging data into per-request buffer */
                req.data_len = (staging_len > CMD_DATA_MAX) ?
                               CMD_DATA_MAX : staging_len;
                for (int i = 0; i < req.data_len; i++) {
                    req.data[i] = staging_buf[i];
                }

                /* Clear CMD_READY while processing */
                registers[0x00] &= ~0x01;

                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                if (cmd_queue) {
                    xQueueSendFromISR(cmd_queue, &req,
                                      &xHigherPriorityTaskWoken);
                }

                staging_len = 0;
                resp_read_pos = 0;

                if (xHigherPriorityTaskWoken) {
                    portYIELD_FROM_ISR();
                }
            }
        } else if (reg == 0x07 && val == 0xFF) {
            /* Reset command: defer reboot to task context (F4 fix) */
            restart_requested = 1;
        } else if (reg < REG_COUNT) {
            registers[reg] = val;
        }

        stat_writes++;
    }

    gpio_intr_enable(GPIO_PSTROBE);
}

/* ===== GPIO Initialization (same as Phase 0) ===== */

static void init_gpio(void)
{
    gpio_config_t io_conf;

    /* PD0-PD7: Start as inputs */
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
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
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

/* ===== Status update task ===== */
/* Periodically updates register file with current network status */

static void status_update_task(void *arg)
{
    (void)arg;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        wifi_state_t ws = wifi_mgr_get_state();
        registers[0x0C] = (ws == WIFI_STATE_CONNECTED) ? NI_NETSTAT_CONNECTED :
                           (ws == WIFI_STATE_CONNECTING) ? NI_NETSTAT_CONNECTING :
                           NI_NETSTAT_DISCONNECTED;
        registers[0x0D] = wifi_mgr_get_signal_pct();
        registers[0x0A] = (uint8_t)http_get_active_count();

        /* F2 fix: Response metadata registers (0x00 RESP_READY bit, 0x01,
         * 0x02, 0x06) are managed exclusively by the ISR during response
         * delivery. Do not touch them here to avoid a race. */

        /* F4 fix: Handle deferred restart from ISR */
        if (restart_requested) {
            ESP_LOGI(TAG, "Deferred restart executing...");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
    }
}

/* ===== Console task (debug output on USB serial) ===== */

static void console_task(void *arg)
{
    uint32_t prev_writes = 0;
    uint32_t prev_reads = 0;

    (void)arg;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " NetISA v1 Firmware");
    ESP_LOGI(TAG, " Version %d.%d.%d",
             FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));

        uint32_t w = stat_writes;
        uint32_t r = stat_reads;

        if (w != prev_writes || r != prev_reads) {
            ESP_LOGI(TAG, "Bus: W=%lu R=%lu | Net=%s | Sessions=%d/%d",
                     w, r,
                     registers[0x0C] == NI_NETSTAT_CONNECTED ? "UP" :
                     registers[0x0C] == NI_NETSTAT_CONNECTING ? "CONNECTING" : "DOWN",
                     registers[0x0A], MAX_SESSIONS);
        }

        prev_writes = w;
        prev_reads = r;
    }
}

/* ===== Main Entry Point ===== */

void app_main(void)
{
    ESP_LOGI(TAG, "NetISA v1 firmware starting...");

    /* Initialize NVS */
    nv_config_init();

    /* Initialize command handler (creates queue, inits HTTP client) */
    cmd_handler_init();

    /* Initialize WiFi (auto-connect or AP mode) */
    wifi_mgr_init();

    /* Start web config server */
    web_config_start();

    /* Initialize parallel bus GPIO and PSTROBE ISR on Core 0 */
    init_gpio();

    /* Start command handler task on Core 1 */
    xTaskCreatePinnedToCore(cmd_handler_task, "cmd_handler", 8192,
                            NULL, 5, NULL, 1);

    /* Start status update task on Core 1 */
    xTaskCreatePinnedToCore(status_update_task, "status", 2048,
                            NULL, 3, NULL, 1);

    /* Start console reporting task on Core 1 */
    xTaskCreatePinnedToCore(console_task, "console", 4096,
                            NULL, 2, NULL, 1);

    /* Assert PBOOT: tell CPLD we are ready.
     * The CPLD drives BOOT_COMPLETE (status bit 6) from the synchronized
     * PBOOT pin directly via hardware flag merging. */
    gpio_set_level(GPIO_PBOOT, 1);
    ESP_LOGI(TAG, "PBOOT asserted. Card ready.");
    ESP_LOGI(TAG, "NetISA v1 ready");
}

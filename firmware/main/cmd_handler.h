/**
 * cmd_handler.h - Command dispatcher types and interface
 *
 * The ISR (preserved from Phase 0) latches commands into a FreeRTOS queue.
 * The command handler task pulls from the queue and dispatches to the
 * appropriate handler based on function group.
 */

#ifndef CMD_HANDLER_H
#define CMD_HANDLER_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define CMD_PARAM_MAX   16
#define CMD_RESP_MAX    512

typedef struct {
    uint8_t group;          /* High nibble: 0x00=system, 0x01=wifi, etc */
    uint8_t function;       /* Full AH value from DOS */
    uint8_t params[CMD_PARAM_MAX];
    uint8_t param_len;
} cmd_request_t;

typedef struct {
    uint8_t status;         /* NI_OK or error code */
    uint8_t data[CMD_RESP_MAX];
    int     data_len;
} cmd_response_t;

/* Handler function type */
typedef void (*cmd_handler_fn)(const cmd_request_t *req, cmd_response_t *resp);

/* Global command queue (ISR -> handler task) */
extern QueueHandle_t cmd_queue;

/* Shared response buffer (handler task -> ISR) */
extern volatile cmd_response_t cmd_response;
extern volatile int cmd_response_ready;

void cmd_handler_init(void);
void cmd_handler_task(void *arg);

#endif /* CMD_HANDLER_H */

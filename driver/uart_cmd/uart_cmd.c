/* uart_cmd.c
 * Accepts:
 *   [+]conn,AA:BB:CC:DD:EE:FF     -> whitelist this MAC, only it may connect
 *   [+]disconn                    -> force-disconnect current connection,
 *                                     regardless of connection/transfer state
 */

#include "uart_cmd.h"
#include "ots.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/addr.h>
#include <string.h>

#define UART_CMD_NODE   DT_ALIAS(uart_cmd)
#define UART_RX_BUF_LEN 64

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart30));

static char   rx_buf[UART_RX_BUF_LEN];
static size_t rx_len;

static void uart_cmd_process(const char *line)
{
    if (strncmp(line, "[+]conn,", 8) == 0) {
        const char *mac_str = line + 8;

        if (ots_set_allowed_addr(mac_str) == 0) {
            printk("[UART] Allowed MAC set: %s\n", mac_str);
        } else {
            printk("[UART] Invalid MAC format: %s\n", mac_str);
        }

    } else if (strncmp(line, "[+]disconn", 10) == 0) {
        printk("[UART] Disconnect command received\n");
        ots_force_disconnect_all();

    } else {
        printk("[UART] Unknown command: '%s'\n", line);
    }
}

static void uart_cmd_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);
    uint8_t c;

    if (!uart_irq_update(dev) || !uart_irq_rx_ready(dev)) {
        return;
    }

    while (uart_fifo_read(dev, &c, 1) == 1) {
        if (c == '\n' || c == '\r') {
            if (rx_len > 0) {
                rx_buf[rx_len] = '\0';
                uart_cmd_process(rx_buf);
                rx_len = 0;
            }
        } else if (rx_len < (sizeof(rx_buf) - 1)) {
            rx_buf[rx_len++] = (char)c;
        } else {
            /* overflow -> drop line */
            rx_len = 0;
            printk("[UART] Line too long, dropped\n");
        }
    }
}

int uart_cmd_init(void)
{
    if (!device_is_ready(uart_dev)) {
        printk("[UART] cmd device not ready\n");
        return -ENODEV;
    }

    uart_irq_callback_user_data_set(uart_dev, uart_cmd_isr, NULL);
    uart_irq_rx_enable(uart_dev);

    printk("[UART] command listener ready on uart30\n");
    return 0;
}
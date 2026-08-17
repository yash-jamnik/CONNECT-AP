#include "uart_cmd.h"
#include "ots.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#define UART_CMD_NODE   DT_ALIAS(uart_cmd)
#define UART_RX_BUF_LEN 64

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(uart30));

static char   rx_buf[UART_RX_BUF_LEN];
static size_t rx_len;

/* Line ready to be processed outside ISR context */
static char    pending_line[UART_RX_BUF_LEN];
static bool    pending_valid;
static struct  k_work cmd_work;

static void cmd_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!pending_valid) {
        return;
    }

    char line[UART_RX_BUF_LEN];
    unsigned int key = irq_lock();
    strcpy(line, pending_line);
    pending_valid = false;
    irq_unlock(key);

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

                /* Hand off to workqueue instead of processing here */
                if (!pending_valid) {
                    strcpy(pending_line, rx_buf);
                    pending_valid = true;
                    k_work_submit(&cmd_work);
                } else {
                    printk("[UART] Previous command still pending, dropped\n");
                }

                rx_len = 0;
            }
        } else if (rx_len < (sizeof(rx_buf) - 1)) {
            rx_buf[rx_len++] = (char)c;
        } else {
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

    k_work_init(&cmd_work, cmd_work_handler);

    uart_irq_callback_user_data_set(uart_dev, uart_cmd_isr, NULL);
    uart_irq_rx_enable(uart_dev);

    printk("[UART] command listener ready on uart30\n");
    return 0;
}
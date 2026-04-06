
/*
above code is working
*/

/*
 * Copyright (c) 2023 Nordic Semiconductor
 * Mobile Device Code - Connects to Receiver
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <string.h>
#include <stdint.h>
static K_SEM_DEFINE(sem_disconnected, 0, 1);

static bool ble_connected = false;

// BELOW
// static struct bt_conn *default_conn = NULL;
// static bool connect_in_progress = false;
/*===============zip fuctions====================*/

#define MAX_PACKET_SIZE 30000
// ...existing code...
static uint8_t compressed_image[MAX_PACKET_SIZE]; // RLE can expand in worst case
static size_t compressed_size = 0;
static size_t rle_compress(const uint8_t *in, size_t in_size,
						   uint8_t *out, size_t out_cap)
{
    size_t i = 0, o = 0;

    while (i < in_size)
    {
        /* Check for repeat run */
        size_t run = 1;
        while (i + run < in_size && run < 128 && in[i] == in[i + run])
            run++;

        if (run >= 3)
        {
            if (o + 2 > out_cap)
                return 0;

            out[o++] = (uint8_t)(257 - run);
            out[o++] = in[i];
            i += run;
        }
        else
        {
            /* Literal run */
            size_t start = i;
            run = 0;

            while (i < in_size && run < 128)
            {
                if (i + 2 < in_size &&
                    in[i] == in[i + 1] &&
                    in[i] == in[i + 2])
                    break;

                i++;
                run++;
            }

            if (o + 1 + run > out_cap)
                return 0;

            out[o++] = (uint8_t)(run - 1);
            memcpy(&out[o], &in[start], run);
            o += run;
        }
    }

    return o;
}

static size_t rle_decompress(const uint8_t *in, size_t in_size,
							 uint8_t *out, size_t out_capacity)
{
	if (in == NULL || out == NULL)
	{
		return 0;
	}

	size_t i = 0;
	size_t o = 0;

	while (i + 1 <= in_size - 1)
	{
		uint8_t count = in[i++];
		uint8_t val = in[i++];

		if (o + count > out_capacity)
		{
			return 0; // not enough output space
		}

		for (uint8_t k = 0; k < count; ++k)
		{
			out[o++] = val;
		}
	}

	if (i != in_size)
	{
		return 0;
	}

	return o;
}
/* ===================== CONFIG ===================== */

#define TARGET_NAME_MAX_LEN 32
#define DEFAULT_TARGET_NAME "NRF52_MOBILE"

#define SCAN_DURATION_MS 20000
#define UART_CMD_EXIT "EXIT"

#define IMAGE_CHUNK_SIZE 200

/* ===================== DEBUG ===================== */

#define ENABLE_STATUS_PRINT 1
#define STATUS_PRINT_INTERVAL_MS 2000

/* ===================== UUIDS (AS REQUESTED) ===================== */

static const struct bt_uuid_128 target_svc_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(
		0xAABBCCDD, 0xBEEF, 0xDEAD, 0xFEED, 0x0123456789AB));

static const struct bt_uuid_128 target_char_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(
		0xBADDCAFE, 0xFACE, 0xC0DE, 0xBABE, 0x9876543210FF));

/* ===================== GLOBAL STATE ===================== */

static struct bt_conn *default_conn;

static char target_device_name[TARGET_NAME_MAX_LEN] = DEFAULT_TARGET_NAME;

static bool scan_requested;
static bool scan_running;
static int64_t scan_end_time;

static uint16_t svc_start_handle;
static uint16_t svc_end_handle;
static uint16_t image_char_handle;
static bool char_found;
static bool image_sent;

static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_write_params write_params;

static K_SEM_DEFINE(sem_discovery, 0, 1);
static K_SEM_DEFINE(sem_write, 0, 1);

/* ===================== IMAGE BUFFER ===================== */

struct DataPacket
{
	uint32_t size;
	uint8_t data[MAX_PACKET_SIZE];
};

static struct DataPacket received_packet;

/* ===================== UART ===================== */

#define UART_NODE DT_NODELABEL(uart20)
static const struct device *uart_dev;

/* ===================== BLE WRITE CALLBACK ===================== */

static void write_cb(struct bt_conn *conn, uint8_t err,
					 struct bt_gatt_write_params *params)
{
	if (err)
	{
		printk("[BLE] Write failed (%d)\n", err);
	}
	k_sem_give(&sem_write);
}
static void stop_scan_and_disconnect(void)
{
	int err;

	/* Stop scan if running */
	if (scan_running)
	{
		err = bt_le_scan_stop();
		if (err && err != -EALREADY)
		{
			printk("[SCAN] Stop failed: %d\n", err);
		}

		scan_running = false;
		scan_requested = false;
		printk("[SCAN] Stopped after image transfer\n");
	}

	/* Disconnect BLE */
	if (!default_conn || !ble_connected)
	{
		printk("[BLE] No active connection to disconnect\n");
		k_sem_give(&sem_disconnected);
		return;
	}

	printk("[BLE] Disconnecting after image transfer\n");
	err = bt_conn_disconnect(default_conn,
					 BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	if (err && err != -ENOTCONN)
	{
		printk("[BLE] Disconnect failed: %d\n", err);
		k_sem_give(&sem_disconnected);
	}
}

/* ===================== DISCOVERY CALLBACKS ===================== */

static uint8_t service_discover_cb(struct bt_conn *conn,
								   const struct bt_gatt_attr *attr,
								   struct bt_gatt_discover_params *params)
{
	if (!attr)
	{
		k_sem_give(&sem_discovery);
		return BT_GATT_ITER_STOP;
	}

	const struct bt_gatt_service_val *svc = attr->user_data;
	svc_start_handle = attr->handle;
	svc_end_handle = svc->end_handle;

	printk("[DISC] Service found (%u - %u)\n",
		   svc_start_handle, svc_end_handle);

	k_sem_give(&sem_discovery);
	return BT_GATT_ITER_STOP;
}

static uint8_t char_discover_cb(struct bt_conn *conn,
								const struct bt_gatt_attr *attr,
								struct bt_gatt_discover_params *params)
{
	if (!attr)
	{
		k_sem_give(&sem_discovery);
		return BT_GATT_ITER_STOP;
	}

	const struct bt_gatt_chrc *chrc = attr->user_data;

	if (!bt_uuid_cmp(chrc->uuid, &target_char_uuid.uuid) &&
		(chrc->properties & BT_GATT_CHRC_WRITE))
	{

		image_char_handle = chrc->value_handle;
		char_found = true;

		printk("[DISC] Writable char found, handle=%u\n",
			   image_char_handle);

		k_sem_give(&sem_discovery);
		return BT_GATT_ITER_STOP;
	}

	return BT_GATT_ITER_CONTINUE;
}

/* ===================== DISCOVERY FLOW ===================== */

static int discover_image_char(void)
{
	int err;

	discover_params.uuid = &target_svc_uuid.uuid;
	discover_params.func = service_discover_cb;
	discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	discover_params.type = BT_GATT_DISCOVER_PRIMARY;

	err = bt_gatt_discover(default_conn, &discover_params);
	if (err)
		return err;
	if (k_sem_take(&sem_discovery, K_SECONDS(5)))
		return -ETIMEDOUT;

	discover_params.uuid = &target_char_uuid.uuid;
	discover_params.func = char_discover_cb;
	discover_params.start_handle = svc_start_handle;
	discover_params.end_handle = svc_end_handle;
	discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	err = bt_gatt_discover(default_conn, &discover_params);
	if (err)
		return err;
	if (k_sem_take(&sem_discovery, K_SECONDS(5)))
		return -ETIMEDOUT;

	return char_found ? 0 : -ENOENT;
}

/* ===================== IMAGE SEND ===================== */

static int send_image(void)
{
	if (!char_found || received_packet.size == 0 || image_sent)
		return 0;

	uint8_t size_buf[4] = {
		compressed_size & 0xFF,
		(compressed_size >> 8) & 0xFF,
		(compressed_size >> 16) & 0xFF,
		(compressed_size >> 24) & 0xFF,
	};
	write_params.func = write_cb;
	write_params.handle = image_char_handle;
	write_params.offset = 0;
	write_params.data = size_buf;
	write_params.length = 4;

	k_sem_reset(&sem_write);
	int err = bt_gatt_write(default_conn, &write_params);
	if (err)
	{
		printk("Size write failed: %d\n", err);
		return err;
	}
	k_sem_take(&sem_write, K_SECONDS(5));

	uint32_t off = 0;
	uint32_t last_printed_percent = 0; // <-- DECLARE HERE
	while (off < compressed_size)
	{
		uint32_t chunk = MIN(IMAGE_CHUNK_SIZE, compressed_size - off);

		write_params.data = &compressed_image[off];
		write_params.length = chunk;

		k_sem_reset(&sem_write); // ADD
		int err = bt_gatt_write(default_conn, &write_params);
		if (err)
		{
			printk("Chunk write failed: %d\n", err);
			return err;
		}
		k_sem_take(&sem_write, K_SECONDS(5));

		off += chunk;
		uint32_t percent = (off * 100) / compressed_size;
		if ((percent >= 20 && last_printed_percent < 20) ||
			(percent >= 40 && last_printed_percent < 40) ||
			(percent >= 60 && last_printed_percent < 60) ||
			(percent >= 80 && last_printed_percent < 80) ||
			(percent >= 100 && last_printed_percent < 100))
		{
			printk("[IMG] Sending: %3u%% (%u/%u bytes)\n", percent, off, compressed_size);
			last_printed_percent = percent - (percent % 20);
		}
	}
	if (last_printed_percent < 100)
	{
		printk("[IMG] Sending: 100%% (%u/%u bytes)\n", received_packet.size, received_packet.size);
	}
	// printk("[IMG] Image sent (%u bytes)\n", received_packet.size);
	printk("[IMG] Image sent (%u bytes)\n", compressed_size);
	printk("[+]IMAGE_SENT_SUCCESSFULLY\n"); // <-- Add this line
	strcpy(target_device_name, DEFAULT_TARGET_NAME);
	printk("[SCAN] Target reset to default: %s\n", target_device_name);
	image_sent = true;
	stop_scan_and_disconnect();
	k_sem_take(&sem_disconnected, K_SECONDS(5)); // <-- Add this line
	received_packet.size = 0;
	compressed_size = 0;
	memset(received_packet.data, 0, sizeof(received_packet.data));
	memset(compressed_image, 0, sizeof(compressed_image));
	return 0;
}

/* ===================== SCAN HELPERS ===================== */

static void start_scan(void)
{
	scan_requested = true;
	scan_running = false;
	scan_end_time = k_uptime_get() + SCAN_DURATION_MS;
}

static void scan_timer_check(void)
{
	if (scan_running && k_uptime_get() >= scan_end_time)
	{
		bt_le_scan_stop();
		scan_running = false;
		scan_requested = false;
		printk("[SCAN] Timeout\n");
	}
}

static bool parse_name_cb(struct bt_data *data, void *user_data)
{
	char *name = user_data;
	if (data->type == BT_DATA_NAME_COMPLETE ||
		data->type == BT_DATA_NAME_SHORTENED)
	{
		size_t len = MIN(data->data_len, 31);
		memcpy(name, data->data, len);
		name[len] = '\0';
		return false;
	}
	return true;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi,
						 uint8_t type, struct net_buf_simple *ad)
{
	char name[32] = {0};
	bt_data_parse(ad, parse_name_cb, name);

	if (!strcmp(name, target_device_name))
	{
		bt_le_scan_stop();
		scan_running = false;
		bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
						  BT_LE_CONN_PARAM_DEFAULT,
						  &default_conn);
	}
}

/* ===================== CONNECTION ===================== */
static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_exchange_params *params)
{
	if (err)
	{
		// printk("MTU exchange failed (err %u)\n", err);
		printk("[+]MTU_EXCHANGE_FAILED\n");
		printk("[+]IMAGE_SENT_FAILED\n"); // <-- Add this line to indicate image sent even if MTU exchange fails
	}
	else
	{
		printk("MTU exchange successful\n");
	}
}

static struct bt_gatt_exchange_params mtu_params = {
	.func = mtu_exchange_cb,
};

static void att_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	uint16_t mtu = MIN(tx, rx);
	printk("[DBG] MTU updated: TX=%u RX=%u Final=%u\n", tx, rx, mtu);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = att_mtu_updated,
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (!err)
	{
		if (!default_conn)
		{
			default_conn = bt_conn_ref(conn);
		}
		ble_connected = true;

		int mtu_err = bt_gatt_exchange_mtu(conn, &mtu_params);
		if (mtu_err)
		{
			printk("Failed to initiate MTU exchange (err %d)\n", mtu_err);
		}
		// bt_gatt_cb_register(&gatt_callbacks);
		image_sent = false;
		char_found = false;
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ble_connected = false;
	if (default_conn)
	{
		bt_conn_unref(default_conn);
		default_conn = NULL;
	}
	char_found = false;
	image_sent = false;
	printk("[BLE] Disconnected, reason=%u\n", reason);
	k_sem_give(&sem_disconnected); // <-- Add this line
}

BT_CONN_CB_DEFINE(cb) = {
	.connected = connected,
	.disconnected = disconnected,
};

/* ===================== UART RX ===================== */

static void uart_cb(const struct device *dev, void *user_data)
{
	static size_t rx_count;
	static uint32_t expected;
	uint8_t c;

	while (uart_fifo_read(dev, &c, 1))
	{

		if (rx_count < 4)
		{
			((uint8_t *)&expected)[rx_count++] = c;
			continue;
		}

		size_t idx = rx_count - 4;
		received_packet.data[idx] = c;
		rx_count++;

		if (idx + 1 == expected)
		{

			if (expected > 8 &&
				!memcmp(received_packet.data,
						"SETNAME:", 8))
			{

				size_t len = MIN(expected - 8,
								 TARGET_NAME_MAX_LEN - 1);
				memcpy(target_device_name,
					   &received_packet.data[8], len);
				target_device_name[len] = '\0';

				start_scan();
			}
			else
			{
				received_packet.size = expected;
				image_sent = false;
				start_scan();
				printk("[+]IMAGE_LOADED_SUCCESSFULLY\n"); // <-- Add this line
				compressed_size = rle_compress(received_packet.data, received_packet.size,
											   compressed_image, sizeof(compressed_image));
				if (compressed_size == 0)
				{
					printk("Image compression failed (output too small)\n");
				}
				else
				{
					int saved = (int)received_packet.size - (int)compressed_size;
					unsigned pct = (unsigned)((compressed_size * 100) / received_packet.size);
					printk("Image compressed=%u bytes, saved=%d bytes, compressed=%u%% of original\n",
						   (unsigned)compressed_size, saved, pct);
				}
			}

			rx_count = 0;
		}
	}
}

/* ===================== STATUS ===================== */

#if ENABLE_STATUS_PRINT
static void print_status(void)
{
	static int64_t last;
	int64_t now = k_uptime_get();
	if (now - last < STATUS_PRINT_INTERVAL_MS)
		return;
	last = now;
	// ...existing code...
	printk("[STATUS] Img:%u Target:%s ScanReq:%s ScanAct:%s BLE:%s [END]\n",
		   received_packet.size,
		   target_device_name,
		   scan_requested ? "YES" : "NO",
		   scan_running ? "YES" : "NO",
		   default_conn ? "CONNECTED" : "DISCONNECTED");
	// ...existing code...
}
#endif

/* ===================== MAIN ===================== */

int main(void)
{
	bt_enable(NULL);

	uart_dev = DEVICE_DT_GET(UART_NODE);
	uart_irq_callback_user_data_set(uart_dev, uart_cb, NULL);
	uart_irq_rx_enable(uart_dev);
	bt_gatt_cb_register(&gatt_callbacks);

	while (1)
	{

		scan_timer_check();

		if (scan_requested && !scan_running)
		{
			bt_le_scan_start(BT_LE_SCAN_ACTIVE, device_found);
			scan_running = true;
		}

		if (ble_connected && !char_found)
		{
			if (discover_image_char() == 0)
			{
				send_image();
			}
		}

#if ENABLE_STATUS_PRINT
		print_status();
#endif

		k_sleep(K_MSEC(200));
	}
}

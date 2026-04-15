/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "ots.h"

int main(void)
{
	int err;

	printk("Starting Bluetooth OTS server\n");

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	err = ots_server_init();
	if (err) {
		printk("Failed to init OTS (err %d)\n", err);
		return 0;
	}

	err = ots_server_start();
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return 0;
	}

	printk("Advertising successfully started\n");
	return 0;
}
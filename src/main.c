/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>
#include <pm_config.h>
#include "ots.h"



LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* LittleFS runtime data */
static struct fs_littlefs lfs_data;

/* Manual mount struct */
static struct fs_mount_t littlefs_mnt = {
	.type = FS_LITTLEFS,
	.fs_data = &lfs_data,
	.storage_dev = (void *)PM_LITTLEFS_STORAGE_ID,
	.mnt_point = "/lfs",
};

static int mount_fs(void)
{
	int rc;

	rc = fs_mount(&littlefs_mnt);
	if (rc == 0) {
		LOG_INF("Mounted LittleFS");
		return 0;
	}

	LOG_WRN("Mount failed (%d), formatting...", rc);

	rc = fs_mkfs(FS_LITTLEFS,
		     (uintptr_t)PM_LITTLEFS_STORAGE_ID,
		     NULL, 0);

	if (rc < 0) {
		LOG_ERR("Format failed: %d", rc);
		return rc;
	}

	rc = fs_mount(&littlefs_mnt);

	if (rc == 0) {
		LOG_INF("Mounted after format");
	}

	return rc;
}

int main(void)
{

	int rc;

	LOG_INF("Start");
	LOG_INF("Partition ID: %d", PM_LITTLEFS_STORAGE_ID);
	LOG_INF("Size: 0x%x", PM_LITTLEFS_STORAGE_SIZE);

	rc = mount_fs();
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

	printk("Advertising successfully started for second firmware\n");
	return 0;
}
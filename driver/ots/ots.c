// /*
//  * SPDX-License-Identifier: Apache-2.0
//  */

// #include "ots.h"

// #include <errno.h>
// #include <string.h>
// #include <zephyr/bluetooth/bluetooth.h>
// #include <zephyr/bluetooth/conn.h>
// #include <zephyr/bluetooth/gatt.h>
// #include <zephyr/bluetooth/hci.h>
// #include <zephyr/bluetooth/services/ots.h>
// #include <zephyr/kernel.h>
// #include <zephyr/sys/printk.h>
// #include <zephyr/types.h>
// #include <zephyr/fs/fs.h>
// #include "bitmaps.h"
// static struct bt_ots *ots_instance;
// static uint64_t first_object_id;
// static bool first_object_created;
// static int load_bin_file(const char *path, uint8_t *buf, size_t max_size, size_t *file_size)
// {
// 	struct fs_file_t file;
// 	int ret;

// 	fs_file_t_init(&file);

// 	ret = fs_open(&file, path, FS_O_READ);
// 	if (ret < 0)
// 	{
// 		printk("Open failed %d\n", ret);
// 		return ret;
// 	}

// 	ret = fs_read(&file, buf, max_size);
// 	if (ret < 0)
// 	{
// 		printk("Read failed %d\n", ret);
// 		fs_close(&file);
// 		return ret;
// 	}

// 	*file_size = ret;

// 	fs_close(&file);
// 	return 0;
// }

// #define DEVICE_NAME CONFIG_BT_DEVICE_NAME
// #define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

// #define OBJ_POOL_SIZE CONFIG_BT_OTS_MAX_OBJ_CNT
// #define OBJ_MAX_SIZE 17000

// static const struct bt_data ad[] = {
// 	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
// 	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
// };

// static const struct bt_data sd[] = {
// 	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_OTS_VAL)),
// };

// static struct
// {
// 	uint8_t data[OBJ_MAX_SIZE];
// 	char name[CONFIG_BT_OTS_OBJ_MAX_NAME_LEN + 1];
// } objects[OBJ_POOL_SIZE];

// static uint32_t obj_cnt;

// struct object_creation_data
// {
// 	struct bt_ots_obj_size size;
// 	char *name;
// 	uint32_t props;
// };

// #define OTS_OBJ_ID_TO_OBJ_IDX(id) (((id) - BT_OTS_OBJ_ID_MIN) % ARRAY_SIZE(objects))

// static struct object_creation_data *object_being_created;

// static void connected(struct bt_conn *conn, uint8_t err)
// {
// 	ARG_UNUSED(conn);

// 	if (err)
// 	{
// 		printk("Connection failed, err %u %s\n", err, bt_hci_err_to_str(err));
// 		return;
// 	}

// 	printk("Connected\n");
// }

// static void disconnected(struct bt_conn *conn, uint8_t reason)
// {
// 	ARG_UNUSED(conn);
// 	printk("Disconnected, reason %u %s\n", reason, bt_hci_err_to_str(reason));
// }

// BT_CONN_CB_DEFINE(conn_callbacks) = {
// 	.connected = connected,
// 	.disconnected = disconnected,
// };

// static int ots_obj_created(struct bt_ots *ots, struct bt_conn *conn, uint64_t id,
// 						   const struct bt_ots_obj_add_param *add_param,
// 						   struct bt_ots_obj_created_desc *created_desc)
// {
// 	char id_str[BT_OTS_OBJ_ID_STR_LEN];
// 	uint32_t index;

// 	ARG_UNUSED(ots);
// 	ARG_UNUSED(conn);

// 	bt_ots_obj_id_to_str(id, id_str, sizeof(id_str));

// 	if (obj_cnt >= ARRAY_SIZE(objects))
// 	{
// 		printk("No object pool slot available for object with %s ID\n", id_str);
// 		return -ENOMEM;
// 	}

// 	if (add_param->size > OBJ_MAX_SIZE)
// 	{
// 		printk("Object pool item is too small for object with %s ID\n", id_str);
// 		return -ENOMEM;
// 	}

// 	if (object_being_created)
// 	{
// 		created_desc->name = object_being_created->name;
// 		created_desc->size = object_being_created->size;
// 		created_desc->props = object_being_created->props;
// 	}
// 	else
// 	{
// 		index = OTS_OBJ_ID_TO_OBJ_IDX(id);
// 		objects[index].name[0] = '\0';

// 		created_desc->name = objects[index].name;
// 		created_desc->size.cur = 0;
// 		created_desc->size.alloc = OBJ_MAX_SIZE;
// 		BT_OTS_OBJ_SET_PROP_READ(created_desc->props);
// 		BT_OTS_OBJ_SET_PROP_WRITE(created_desc->props);
// 		BT_OTS_OBJ_SET_PROP_PATCH(created_desc->props);
// 		BT_OTS_OBJ_SET_PROP_DELETE(created_desc->props);
// 	}

// 	printk("Object with %s ID has been created\n", id_str);
// 	obj_cnt++;

// 	return 0;
// }

// static int ots_obj_deleted(struct bt_ots *ots, struct bt_conn *conn, uint64_t id)
// {
// 	char id_str[BT_OTS_OBJ_ID_STR_LEN];

// 	ARG_UNUSED(ots);
// 	ARG_UNUSED(conn);

// 	bt_ots_obj_id_to_str(id, id_str, sizeof(id_str));
// 	printk("Object with %s ID has been deleted\n", id_str);

// 	if (obj_cnt > 0U)
// 	{
// 		obj_cnt--;
// 	}

// 	return 0;
// }

// static void ots_obj_selected(struct bt_ots *ots, struct bt_conn *conn, uint64_t id)
// {
// 	char id_str[BT_OTS_OBJ_ID_STR_LEN];

// 	ARG_UNUSED(ots);
// 	ARG_UNUSED(conn);

// 	bt_ots_obj_id_to_str(id, id_str, sizeof(id_str));
// 	printk("Object with %s ID has been selected\n", id_str);
// }

// static ssize_t ots_obj_read(struct bt_ots *ots, struct bt_conn *conn,
// 							uint64_t id, void **data, size_t len, off_t offset)
// {
// 	char id_str[BT_OTS_OBJ_ID_STR_LEN];
// 	uint32_t obj_index = OTS_OBJ_ID_TO_OBJ_IDX(id);

// 	ARG_UNUSED(ots);
// 	ARG_UNUSED(conn);

// 	bt_ots_obj_id_to_str(id, id_str, sizeof(id_str));

// 	if (!data)
// 	{
// 		printk("Object with %s ID has been successfully read\n", id_str);
// 		return 0;
// 	}

// 	*data = &objects[obj_index].data[offset];

// 	if ((obj_index % 2U) == 0U)
// 	{
// 		len = MIN(len, (size_t)240);
// 	}

// 	printk("Object with %s ID is being read\nOffset = %ld, Length = %zu\n",
// 		   id_str, (long)offset, len);

// 	return len;
// }

// static ssize_t ots_obj_write(struct bt_ots *ots, struct bt_conn *conn,
// 							 uint64_t id, const void *data, size_t len,
// 							 off_t offset, size_t rem)
// {
// 	char id_str[BT_OTS_OBJ_ID_STR_LEN];
// 	uint32_t obj_index = OTS_OBJ_ID_TO_OBJ_IDX(id);

// 	ARG_UNUSED(ots);
// 	ARG_UNUSED(conn);

// 	bt_ots_obj_id_to_str(id, id_str, sizeof(id_str));
// 	printk("Object with %s ID is being written\nOffset = %ld, Length = %zu, Remaining = %zu\n",
// 		   id_str, (long)offset, len, rem);

// 	memcpy(&objects[obj_index].data[offset], data, len);

// 	return len;
// }

// static void ots_obj_name_written(struct bt_ots *ots, struct bt_conn *conn,
// 								 uint64_t id, const char *cur_name, const char *new_name)
// {
// 	char id_str[BT_OTS_OBJ_ID_STR_LEN];

// 	ARG_UNUSED(ots);
// 	ARG_UNUSED(conn);

// 	bt_ots_obj_id_to_str(id, id_str, sizeof(id_str));
// 	printk("Name for object with %s ID is being changed from '%s' to '%s'\n",
// 		   id_str, cur_name, new_name);
// }

// static int ots_obj_cal_checksum(struct bt_ots *ots, struct bt_conn *conn, uint64_t id,
// 								off_t offset, size_t len, void **data)
// {
// 	uint32_t obj_index = OTS_OBJ_ID_TO_OBJ_IDX(id);

// 	ARG_UNUSED(ots);
// 	ARG_UNUSED(conn);
// 	ARG_UNUSED(len);

// 	if (obj_index >= OBJ_POOL_SIZE)
// 	{
// 		return -ENOENT;
// 	}

// 	*data = &objects[obj_index].data[offset];
// 	return 0;
// }

// static struct bt_ots_cb ots_callbacks = {
// 	.obj_created = ots_obj_created,
// 	.obj_deleted = ots_obj_deleted,
// 	.obj_selected = ots_obj_selected,
// 	.obj_read = ots_obj_read,
// 	.obj_write = ots_obj_write,
// 	.obj_name_written = ots_obj_name_written,
// 	.obj_cal_checksum = ots_obj_cal_checksum,
// };

// int ots_server_init(void)
// {
// 	int err;
// 	struct bt_ots *ots;
// 	struct object_creation_data obj_data;
// 	struct bt_ots_init_param ots_init;
// 	struct bt_ots_obj_add_param param;
// 	const char *const first_object_name = "slot0_image";
// 	const char *const bitmap_object_name = "slot1_image";
// 	uint32_t cur_size;
// 	uint32_t alloc_size;
// 	size_t bitmap_size = sizeof(batman);

// 	ots = bt_ots_free_instance_get();
// 	ots_instance = ots;
// 	if (!ots)
// 	{
// 		printk("Failed to retrieve OTS instance\n");
// 		return -ENOMEM;
// 	}

// 	memset(&ots_init, 0, sizeof(ots_init));
// 	BT_OTS_OACP_SET_FEAT_READ(ots_init.features.oacp);
// 	BT_OTS_OACP_SET_FEAT_WRITE(ots_init.features.oacp);
// 	BT_OTS_OACP_SET_FEAT_CREATE(ots_init.features.oacp);
// 	BT_OTS_OACP_SET_FEAT_DELETE(ots_init.features.oacp);
// 	BT_OTS_OACP_SET_FEAT_PATCH(ots_init.features.oacp);
// 	BT_OTS_OLCP_SET_FEAT_GO_TO(ots_init.features.olcp);
// 	ots_init.cb = &ots_callbacks;

// 	err = bt_ots_init(ots, &ots_init);
// 	if (err)
// 	{
// 		printk("Failed to init OTS (err: %d)\n", err);
// 		return err;
// 	}

// 	if (bitmap_size > sizeof(objects[0].data))
// 	{
// 		printk("First object is too large for OTS buffer\n");
//         return -ENOMEM;
//     }

// 	cur_size = bitmap_size;
// 	alloc_size = sizeof(objects[0].data);
// 	memcpy(objects[0].data, batman, cur_size);

//     memset(&obj_data, 0, sizeof(obj_data));
// 	__ASSERT(strlen(first_object_name) <= CONFIG_BT_OTS_OBJ_MAX_NAME_LEN,
// 			 "Object name length is larger than the allowed maximum of %u",
// 			 CONFIG_BT_OTS_OBJ_MAX_NAME_LEN);
// 	strcpy(objects[0].name, first_object_name);
//     obj_data.name = objects[0].name;
// 	obj_data.size.cur = cur_size;
// 	obj_data.size.alloc = alloc_size;
//     BT_OTS_OBJ_SET_PROP_READ(obj_data.props);
//     BT_OTS_OBJ_SET_PROP_WRITE(obj_data.props);
//     BT_OTS_OBJ_SET_PROP_PATCH(obj_data.props);
//     object_being_created = &obj_data;

//     memset(&param, 0, sizeof(param));
// 	param.size = alloc_size;
//     param.type.uuid.type = BT_UUID_TYPE_16;
//     param.type.uuid_16.val = BT_UUID_OTS_TYPE_UNSPECIFIED_VAL;
// 	err = bt_ots_obj_add(ots, &param);
//     object_being_created = NULL;
// 	if (err < 0)
// 	{
// 		printk("Failed to add the first object to OTS (err: %d)\n", err);
// 		return err;
// 	}

// 	if (bitmap_size > sizeof(objects[1].data))
// 	{
// 		printk("Bitmap object is too large for OTS buffer\n");
// 		return -ENOMEM;
// 	}

// 	memcpy(objects[1].data, batman, bitmap_size);

// 	memset(&obj_data, 0, sizeof(obj_data));
// 	__ASSERT(strlen(bitmap_object_name) <= CONFIG_BT_OTS_OBJ_MAX_NAME_LEN,
// 			 "Object name length is larger than the allowed maximum of %u",
// 			 CONFIG_BT_OTS_OBJ_MAX_NAME_LEN);
// 	strcpy(objects[1].name, bitmap_object_name);
// 	obj_data.name = objects[1].name;
// 	obj_data.size.cur = bitmap_size;
// 	obj_data.size.alloc = sizeof(objects[1].data);
// 	BT_OTS_OBJ_SET_PROP_READ(obj_data.props);
// 	object_being_created = &obj_data;

// 	memset(&param, 0, sizeof(param));
// 	param.size = sizeof(objects[1].data);
// 	param.type.uuid.type = BT_UUID_TYPE_16;
// 	param.type.uuid_16.val = BT_UUID_OTS_TYPE_UNSPECIFIED_VAL;
// 	err = bt_ots_obj_add(ots, &param);
// 	object_being_created = NULL;
// 	if (err < 0)
// 	{
// 		printk("Failed to add the bitmap object to OTS (err: %d)\n", err);
//     return err;
// }

// 	return 0;
// }

// int ots_server_start(void)
// {
// 	return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
// }

/*
 * ots.c
 * Updated OTS server:
 * - LittleFS backed objects
 * - Upload progress logs
 * - Delayed advertising restart fix
 * - Better disconnect diagnostics
 * - Read + Write enabled preload objects
 */

#include "ots.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/sys/printk.h>
#include <zephyr/fs/fs.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/ots.h>
#include <zephyr/sys/reboot.h>

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define OBJ_POOL_SIZE CONFIG_BT_OTS_MAX_OBJ_CNT
#define OBJ_MAX_NAME CONFIG_BT_OTS_OBJ_MAX_NAME_LEN

#define OTS_CHUNK_SIZE 240
#define LFS_PATH "/lfs"
static bool recreate_in_progress;
static struct bt_ots *ots_instance;
static bool reset_on_disconnect;

/* ------------------------------------------------------- */
/* Advertising data                                        */
/* ------------------------------------------------------- */

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS,
                  (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE,
            DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
                  BT_UUID_16_ENCODE(BT_UUID_OTS_VAL)),
};

/* ------------------------------------------------------- */
/* Structures                                              */
/* ------------------------------------------------------- */

struct ots_file_obj
{
    bool used;
    uint64_t id;

    char name[OBJ_MAX_NAME + 1];
    char path[96];

    struct bt_ots_obj_size size;
    uint32_t props;

    uint8_t progress_pct;
};

struct preload_obj
{
    struct bt_ots_obj_size size;
    char *name;
    uint32_t props;
    char path[96];
};

static struct ots_file_obj objects[OBJ_POOL_SIZE];
static struct preload_obj *object_being_created;

static uint32_t obj_cnt;

/* ------------------------------------------------------- */
/* Helpers                                                 */
/* ------------------------------------------------------- */

static int get_free_slot(void)
{
    for (int i = 0; i < OBJ_POOL_SIZE; i++)
    {
        if (!objects[i].used)
        {
            return i;
        }
    }
    return -1;
}

static int find_slot_by_id(uint64_t id)
{
    for (int i = 0; i < OBJ_POOL_SIZE; i++)
    {
        if (objects[i].used && objects[i].id == id)
        {
            return i;
        }
    }
    return -1;
}

static int get_file_size(const char *path, size_t *size)
{
    struct fs_dirent entry;
    int ret = fs_stat(path, &entry);

    if (ret < 0)
    {
        return ret;
    }

    *size = entry.size;
    return 0;
}

static int file_read_chunk(const char *path,
                           off_t offset,
                           uint8_t *buf,
                           size_t len)
{
    struct fs_file_t file;
    int ret;

    fs_file_t_init(&file);

    ret = fs_open(&file, path, FS_O_READ);
    if (ret < 0)
    {
        return ret;
    }

    ret = fs_seek(&file, offset, FS_SEEK_SET);
    if (ret < 0)
    {
        fs_close(&file);
        return ret;
    }

    ret = fs_read(&file, buf, len);

    fs_close(&file);
    return ret;
}

static int file_write_chunk(const char *path,
                            off_t offset,
                            const void *data,
                            size_t len)
{
    struct fs_file_t file;
    int ret;

    fs_file_t_init(&file);

    ret = fs_open(&file, path, FS_O_CREATE | FS_O_RDWR);
    if (ret < 0)
    {
        return ret;
    }

    ret = fs_seek(&file, offset, FS_SEEK_SET);
    if (ret < 0)
    {
        fs_close(&file);
        return ret;
    }

    ret = fs_write(&file, data, len);

    fs_close(&file);
    return ret;
}

/* ------------------------------------------------------- */
/* Advertising restart work                                */
/* ------------------------------------------------------- */

static void restart_adv_handler(struct k_work *work);

K_WORK_DEFINE(restart_adv_work, restart_adv_handler);

static void restart_adv_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                              ad, ARRAY_SIZE(ad),
                              sd, ARRAY_SIZE(sd));

    if (err)
    {
        printk("Advertising restart failed: %d\n", err);
    }
    else
    {
        printk("Advertising restarted\n");
    }
}

/* ------------------------------------------------------- */
/* Connection callbacks                                    */
/* ------------------------------------------------------- */

static void connected(struct bt_conn *conn, uint8_t err)
{
    ARG_UNUSED(conn);

    if (err)
    {
        printk("Connection failed %u %s\n",
               err, bt_hci_err_to_str(err));
        return;
    }

    printk("Connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);

    printk("Disconnected: %u %s\n",
           reason, bt_hci_err_to_str(reason));
    if (reset_on_disconnect) {
        reset_on_disconnect = false;
        k_sleep(K_MSEC(100));
        printk("Resetting device due to disconnect after transfer...\n");
        sys_reboot(SYS_REBOOT_COLD);
    }

    k_sleep(K_MSEC(300));

    k_work_submit(&restart_adv_work);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

/* ------------------------------------------------------- */
/* OTS Callbacks                                           */
/* ------------------------------------------------------- */

static int ots_obj_created(struct bt_ots *ots,
                           struct bt_conn *conn,
                           uint64_t id,
                           const struct bt_ots_obj_add_param *add_param,
                           struct bt_ots_obj_created_desc *created_desc)
{
    ARG_UNUSED(ots);
    ARG_UNUSED(conn);
    ARG_UNUSED(add_param);

    int slot = get_free_slot();

    if (slot < 0)
    {
        return -ENOMEM;
    }

    memset(&objects[slot], 0, sizeof(objects[slot]));

    objects[slot].used = true;
    objects[slot].id = id;

    if (object_being_created)
    {

        strcpy(objects[slot].name,
               object_being_created->name);

        strcpy(objects[slot].path,
               object_being_created->path);

        objects[slot].size = object_being_created->size;
        objects[slot].props = object_being_created->props;
    }
    else
    {

        snprintf(objects[slot].name,
                 sizeof(objects[slot].name),
                 "object_%d", slot);

        snprintf(objects[slot].path,
                 sizeof(objects[slot].path),
                 LFS_PATH "/object_%d.bin", slot);

        objects[slot].size.cur = 0;
        objects[slot].size.alloc = 0xffffffff;

        BT_OTS_OBJ_SET_PROP_READ(objects[slot].props);
        BT_OTS_OBJ_SET_PROP_WRITE(objects[slot].props);
        BT_OTS_OBJ_SET_PROP_PATCH(objects[slot].props);
        BT_OTS_OBJ_SET_PROP_DELETE(objects[slot].props);
    }

    created_desc->name = objects[slot].name;
    created_desc->size = objects[slot].size;
    created_desc->props = objects[slot].props;

    obj_cnt++;

    printk("Object created: %s\n", objects[slot].name);

    return 0;
}

static int ots_obj_deleted(struct bt_ots *ots,
                           struct bt_conn *conn,
                           uint64_t id)
{
    ARG_UNUSED(ots);
    ARG_UNUSED(conn);

    int slot = find_slot_by_id(id);

    if (slot < 0)
    {
        return -ENOENT;
    }
    if (!recreate_in_progress)
    {
        fs_unlink(objects[slot].path);
    }
    memset(&objects[slot], 0, sizeof(objects[slot]));

    if (obj_cnt)
    {
        obj_cnt--;
    }

    printk("Object deleted\n");

    return 0;
}

static void ots_obj_selected(struct bt_ots *ots,
                             struct bt_conn *conn,
                             uint64_t id)
{
    ARG_UNUSED(ots);
    ARG_UNUSED(conn);

    printk("Selected object %llu\n", id);
}

static ssize_t ots_obj_read(struct bt_ots *ots,
                            struct bt_conn *conn,
                            uint64_t id,
                            void **data,
                            size_t len,
                            off_t offset)
{

    ARG_UNUSED(ots);
    ARG_UNUSED(conn);

    static uint8_t chunk[OTS_CHUNK_SIZE];

    int slot = find_slot_by_id(id);
    int ret;
    size_t total;
    int percent;

    if (slot < 0)
    {
        return -ENOENT;
    }
    printk("READ size.cur=%u\n", objects[slot].size.cur);
    /* OTS may call with data == NULL to indicate end of read procedure */
    if (!data)
    {
        if (objects[slot].progress_pct < 100U)
        {
            objects[slot].progress_pct = 100U;
            printk("100%%\n");
        }
        printk("[+]IMAGE SENT SUCCESS\n");
        reset_on_disconnect = true;
        return 0;
    }

    if (offset == 0)
    {
        objects[slot].progress_pct = 0;
    }

    len = MIN(len, sizeof(chunk));

    ret = file_read_chunk(objects[slot].path,
                          offset,
                          chunk,
                          len);

    if (ret < 0)
    {
        return ret;
    }

    *data = chunk;

    total = objects[slot].size.cur;
    if (total == 0U)
    {
        return ret;
    }

    percent = (int)(((offset + (size_t)ret) * 100U) / total);

    /* Print only at 20% boundaries and at 100% */
    if (percent >= (objects[slot].progress_pct + 20) || percent == 100)
    {
        int step = (percent >= 100) ? 100 : (percent / 20) * 20;

        if (step > objects[slot].progress_pct)
        {
            objects[slot].progress_pct = (uint8_t)step;
            printk("%d%%\n", step);

            if (step == 100)
            {
                printk("[+]IMAGE SENT SUCCESS\n");
            }
        }
    }

    return ret;
}

static ssize_t ots_obj_write(struct bt_ots *ots,
                             struct bt_conn *conn,
                             uint64_t id,
                             const void *data,
                             size_t len,
                             off_t offset,
                             size_t rem)
{
    ARG_UNUSED(ots);
    ARG_UNUSED(conn);

    int slot = find_slot_by_id(id);
    int ret;
    size_t total;
    int percent;

    if (slot < 0)
    {
        return -ENOENT;
    }

    if (offset == 0)
    {
        objects[slot].progress_pct = 0;
        printk("Upload started: %s\n", objects[slot].name);
    }

    total = offset + len + rem;

    if (total == 0)
    {
        percent = 0;
    }
    else
    {
        percent = (int)(((offset + len) * 100U) / total);
    }

    ret = file_write_chunk(objects[slot].path,
                           offset,
                           data,
                           len);

    if (ret < 0)
    {
        return ret;
    }

    if ((offset + len) > objects[slot].size.cur)
    {
        objects[slot].size.cur = offset + len;
    }

    if (percent >= (objects[slot].progress_pct + 10) ||
        percent == 100)
    {

        objects[slot].progress_pct = percent;

        printk("%s upload %d%%\n",
               objects[slot].name,
               percent);
    }

    if (rem == 0)
    {
        printk("Image %s sent successfully\n",
               objects[slot].name);
    }

    return len;
}

static void ots_obj_name_written(struct bt_ots *ots,
                                 struct bt_conn *conn,
                                 uint64_t id,
                                 const char *cur_name,
                                 const char *new_name)
{
    ARG_UNUSED(ots);
    ARG_UNUSED(conn);
    ARG_UNUSED(cur_name);

    int slot = find_slot_by_id(id);

    if (slot >= 0)
    {
        strncpy(objects[slot].name,
                new_name,
                OBJ_MAX_NAME);
    }
}

static int ots_obj_cal_checksum(struct bt_ots *ots,
                                struct bt_conn *conn,
                                uint64_t id,
                                off_t offset,
                                size_t len,
                                void **data)
{
    ARG_UNUSED(ots);
    ARG_UNUSED(conn);

    static uint8_t chunk[OTS_CHUNK_SIZE];

    int slot = find_slot_by_id(id);
    int ret;

    if (slot < 0)
    {
        return -ENOENT;
    }

    len = MIN(len, sizeof(chunk));

    ret = file_read_chunk(objects[slot].path,
                          offset,
                          chunk,
                          len);

    if (ret < 0)
    {
        return ret;
    }

    *data = chunk;
    return 0;
}

static struct bt_ots_cb ots_callbacks = {
    .obj_created = ots_obj_created,
    .obj_deleted = ots_obj_deleted,
    .obj_selected = ots_obj_selected,
    .obj_read = ots_obj_read,
    .obj_write = ots_obj_write,
    .obj_name_written = ots_obj_name_written,
    .obj_cal_checksum = ots_obj_cal_checksum,
};

/* ------------------------------------------------------- */
/* Init                                                    */
/* ------------------------------------------------------- */

int ots_server_init(void)
{
    int err;
    size_t sz;

    struct bt_ots *ots;
    struct bt_ots_init_param init;
    struct bt_ots_obj_add_param param;

    ots = bt_ots_free_instance_get();

    if (!ots)
    {
        return -ENOMEM;
    }

    ots_instance = ots;

    memset(&init, 0, sizeof(init));

    BT_OTS_OACP_SET_FEAT_READ(init.features.oacp);
    BT_OTS_OACP_SET_FEAT_WRITE(init.features.oacp);
    BT_OTS_OACP_SET_FEAT_CREATE(init.features.oacp);
    BT_OTS_OACP_SET_FEAT_DELETE(init.features.oacp);
    BT_OTS_OACP_SET_FEAT_PATCH(init.features.oacp);

    BT_OTS_OLCP_SET_FEAT_GO_TO(init.features.olcp);

    init.cb = &ots_callbacks;

    err = bt_ots_init(ots, &init);
    if (err)
    {
        return err;
    }

    if (get_file_size(LFS_PATH "/slot0_image.bin", &sz) == 0)
    {

        static struct preload_obj preload0;

        memset(&preload0, 0, sizeof(preload0));

        preload0.name = "slot0_image";
        strcpy(preload0.path,
               LFS_PATH "/slot0_image.bin");

        preload0.size.cur = sz;
        preload0.size.alloc = sz;

        BT_OTS_OBJ_SET_PROP_READ(preload0.props);
        BT_OTS_OBJ_SET_PROP_WRITE(preload0.props);
        BT_OTS_OBJ_SET_PROP_PATCH(preload0.props);

        object_being_created = &preload0;

        memset(&param, 0, sizeof(param));
        param.size = sz;
        param.type.uuid.type = BT_UUID_TYPE_16;
        param.type.uuid_16.val =
            BT_UUID_OTS_TYPE_UNSPECIFIED_VAL;

        bt_ots_obj_add(ots, &param);

        object_being_created = NULL;
    }

    if (get_file_size(LFS_PATH "/slot1_image.bin", &sz) == 0)
    {

        static struct preload_obj preload1;

        memset(&preload1, 0, sizeof(preload1));

        preload1.name = "slot1_image";
        strcpy(preload1.path,
               LFS_PATH "/slot1_image.bin");

        preload1.size.cur = sz;
        preload1.size.alloc = sz;
        printk("OTS metadata size = %u\n",
               (uint32_t)preload1.size.cur);

        BT_OTS_OBJ_SET_PROP_READ(preload1.props);
        BT_OTS_OBJ_SET_PROP_WRITE(preload1.props);
        BT_OTS_OBJ_SET_PROP_PATCH(preload1.props);

        object_being_created = &preload1;

        memset(&param, 0, sizeof(param));
        param.size = sz;
        param.type.uuid.type = BT_UUID_TYPE_16;
        param.type.uuid_16.val =
            BT_UUID_OTS_TYPE_UNSPECIFIED_VAL;

        bt_ots_obj_add(ots, &param);

        object_being_created = NULL;
    }

    printk("OTS server ready\n");

    return 0;
}

/* ------------------------------------------------------- */
/* Start advertising                                       */
/* ------------------------------------------------------- */

int ots_server_start(void)
{
    return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
                           ad, ARRAY_SIZE(ad),
                           sd, ARRAY_SIZE(sd));
}

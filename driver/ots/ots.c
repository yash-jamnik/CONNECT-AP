
// /*
//  * ots.c
//  * Updated OTS server:
//  * - LittleFS backed objects
//  * - Upload progress logs
//  * - Delayed advertising restart fix
//  * - Better disconnect diagnostics
//  * - Read + Write enabled preload objects
//  */

// #include "ots.h"

// #include <errno.h>
// #include <string.h>
// #include <stdio.h>

// #include <zephyr/kernel.h>
// #include <zephyr/types.h>
// #include <zephyr/sys/printk.h>
// #include <zephyr/fs/fs.h>

// #include <zephyr/bluetooth/bluetooth.h>
// #include <zephyr/bluetooth/conn.h>
// #include <zephyr/bluetooth/hci.h>
// #include <zephyr/bluetooth/gatt.h>
// #include <zephyr/bluetooth/services/ots.h>
// #include <zephyr/sys/reboot.h>

// #define DEVICE_NAME CONFIG_BT_DEVICE_NAME
// #define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

// #define OBJ_POOL_SIZE CONFIG_BT_OTS_MAX_OBJ_CNT
// #define OBJ_MAX_NAME CONFIG_BT_OTS_OBJ_MAX_NAME_LEN

// #define OTS_CHUNK_SIZE 240
// #define LFS_PATH "/lfs"
// static bool recreate_in_progress;
// static struct bt_ots *ots_instance;
// static bool reset_on_disconnect;

// /* ------------------------------------------------------- */
// /* Advertising data                                        */
// /* ------------------------------------------------------- */

// static const struct bt_data ad[] = {
//     BT_DATA_BYTES(BT_DATA_FLAGS,
//                   (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
//     BT_DATA(BT_DATA_NAME_COMPLETE,
//             DEVICE_NAME, DEVICE_NAME_LEN),
// };

// static const struct bt_data sd[] = {
//     BT_DATA_BYTES(BT_DATA_UUID16_ALL,
//                   BT_UUID_16_ENCODE(BT_UUID_OTS_VAL)),
// };

// /* ------------------------------------------------------- */
// /* Structures                                              */
// /* ------------------------------------------------------- */

// struct ots_file_obj
// {
//     bool used;
//     uint64_t id;

//     char name[OBJ_MAX_NAME + 1];
//     char path[96];

//     struct bt_ots_obj_size size;
//     uint32_t props;

//     uint8_t progress_pct;
//     uint8_t chunk[OTS_CHUNK_SIZE]; 
// };

// struct preload_obj
// {
//     struct bt_ots_obj_size size;
//     char *name;
//     uint32_t props;
//     char path[96];
// };

// static struct ots_file_obj objects[OBJ_POOL_SIZE];
// static struct preload_obj *object_being_created;

// static uint32_t obj_cnt;

// /* ------------------------------------------------------- */
// /* Helpers                                                 */
// /* ------------------------------------------------------- */

// static int get_free_slot(void)
// {
//     for (int i = 0; i < OBJ_POOL_SIZE; i++)
//     {
//         if (!objects[i].used)
//         {
//             return i;
//         }
//     }
//     return -1;
// }

// static int find_slot_by_id(uint64_t id)
// {
//     for (int i = 0; i < OBJ_POOL_SIZE; i++)
//     {
//         if (objects[i].used && objects[i].id == id)
//         {
//             return i;
//         }
//     }
//     return -1;
// }

// static int get_file_size(const char *path, size_t *size)
// {
//     struct fs_dirent entry;
//     int ret = fs_stat(path, &entry);

//     if (ret < 0)
//     {
//         return ret;
//     }

//     *size = entry.size;
//     return 0;
// }

// static int file_read_chunk(const char *path,
//                            off_t offset,
//                            uint8_t *buf,
//                            size_t len)
// {
//     struct fs_file_t file;
//     int ret;

//     fs_file_t_init(&file);

//     ret = fs_open(&file, path, FS_O_READ);
//     if (ret < 0)
//     {
//         return ret;
//     }

//     ret = fs_seek(&file, offset, FS_SEEK_SET);
//     if (ret < 0)
//     {
//         fs_close(&file);
//         return ret;
//     }

//     ret = fs_read(&file, buf, len);

//     fs_close(&file);
//     return ret;
// }

// static int file_write_chunk(const char *path,
//                             off_t offset,
//                             const void *data,
//                             size_t len)
// {
//     struct fs_file_t file;
//     int ret;

//     fs_file_t_init(&file);

//     ret = fs_open(&file, path, FS_O_CREATE | FS_O_RDWR);
//     if (ret < 0)
//     {
//         return ret;
//     }

//     ret = fs_seek(&file, offset, FS_SEEK_SET);
//     if (ret < 0)
//     {
//         fs_close(&file);
//         return ret;
//     }

//     ret = fs_write(&file, data, len);

//     fs_close(&file);
//     return ret;
// }

// #define TARGET_CONNECTIONS 2

// static struct bt_conn *connected_conns[TARGET_CONNECTIONS];
// static uint8_t connected_count;
// static bool all_connected;
// /* ------------------------------------------------------- */
// /* Advertising restart work                                */
// /* ------------------------------------------------------- */

// static void restart_adv_handler(struct k_work *work);

// K_WORK_DEFINE(restart_adv_work, restart_adv_handler);

// static void restart_adv_handler(struct k_work *work)
// {
//     ARG_UNUSED(work);

//     int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
//                               ad, ARRAY_SIZE(ad),
//                               sd, ARRAY_SIZE(sd));

//     if (err == -EALREADY)
//     {
//         return;
//     }

//     if (err)
//     {
//         printk("Advertising restart failed: %d\n", err);
//     }
//     else
//     {
//         printk("Advertising restarted\n");
//     }
// }

// /* ------------------------------------------------------- */
// /* Connection callbacks                                    */
// /* ------------------------------------------------------- */

// static void connected(struct bt_conn *conn, uint8_t err)
// {
//     if (err) {
//         printk("Connection failed %u %s\n",
//                err, bt_hci_err_to_str(err));
//         return;
//     }

//     printk("Connected\n");

//     if (connected_count < TARGET_CONNECTIONS) {
//         connected_conns[connected_count] = bt_conn_ref(conn);
//         connected_count++;

//         printk("Connected %d/%d\n",
//                connected_count,
//                TARGET_CONNECTIONS);
//     }

//     if (connected_count < TARGET_CONNECTIONS) {

//         /* Keep advertising for second ESL */
//         k_work_submit(&restart_adv_work);

//     } else {

//         all_connected = true;

//         printk("Two ESLs connected\n");

//         /* DO NOT start transfer here.
//          * Just set the flag.
//          */
//     }
// }

// static void disconnected(struct bt_conn *conn, uint8_t reason)
// {
//     for (int i = 0; i < connected_count; i++) {

//         if (connected_conns[i] == conn) {

//             bt_conn_unref(connected_conns[i]);

//             for (int j = i; j < connected_count - 1; j++) {
//                 connected_conns[j] = connected_conns[j + 1];
//             }

//             connected_conns[connected_count - 1] = NULL;
//             connected_count--;

//             break;
//         }
//     }

//     all_connected = false;

//     k_work_submit(&restart_adv_work);
// }

// BT_CONN_CB_DEFINE(conn_callbacks) = {
//     .connected = connected,
//     .disconnected = disconnected,
// };

// /* ------------------------------------------------------- */
// /* OTS Callbacks                                           */
// /* ------------------------------------------------------- */

// static int ots_obj_created(struct bt_ots *ots,
//                            struct bt_conn *conn,
//                            uint64_t id,
//                            const struct bt_ots_obj_add_param *add_param,
//                            struct bt_ots_obj_created_desc *created_desc)
// {
//     ARG_UNUSED(ots);
//     ARG_UNUSED(conn);
//     ARG_UNUSED(add_param);

//     int slot = get_free_slot();

//     if (slot < 0)
//     {
//         return -ENOMEM;
//     }

//     memset(&objects[slot], 0, sizeof(objects[slot]));

//     objects[slot].used = true;
//     objects[slot].id = id;

//     if (object_being_created)
//     {

//         strcpy(objects[slot].name,
//                object_being_created->name);

//         strcpy(objects[slot].path,
//                object_being_created->path);

//         objects[slot].size = object_being_created->size;
//         objects[slot].props = object_being_created->props;
//     }
//     else
//     {

//         snprintf(objects[slot].name,
//                  sizeof(objects[slot].name),
//                  "object_%d", slot);

//         snprintf(objects[slot].path,
//                  sizeof(objects[slot].path),
//                  LFS_PATH "/object_%d.bin", slot);

//         objects[slot].size.cur = 0;
//         objects[slot].size.alloc = 0xffffffff;

//         BT_OTS_OBJ_SET_PROP_READ(objects[slot].props);
//         BT_OTS_OBJ_SET_PROP_WRITE(objects[slot].props);
//         BT_OTS_OBJ_SET_PROP_PATCH(objects[slot].props);
//         BT_OTS_OBJ_SET_PROP_DELETE(objects[slot].props);
//     }

//     created_desc->name = objects[slot].name;
//     created_desc->size = objects[slot].size;
//     created_desc->props = objects[slot].props;

//     obj_cnt++;

//     printk("Object created: %s\n", objects[slot].name);

//     return 0;
// }

// static int ots_obj_deleted(struct bt_ots *ots,
//                            struct bt_conn *conn,
//                            uint64_t id)
// {
//     ARG_UNUSED(ots);
//     ARG_UNUSED(conn);

//     int slot = find_slot_by_id(id);

//     if (slot < 0)
//     {
//         return -ENOENT;
//     }
//     if (!recreate_in_progress)
//     {
//         fs_unlink(objects[slot].path);
//     }
//     memset(&objects[slot], 0, sizeof(objects[slot]));

//     if (obj_cnt)
//     {
//         obj_cnt--;
//     }

//     printk("Object deleted\n");

//     return 0;
// }

// static void ots_obj_selected(struct bt_ots *ots,
//                              struct bt_conn *conn,
//                              uint64_t id)
// {
//     ARG_UNUSED(ots);
//     ARG_UNUSED(conn);

//     printk("Selected object %llu\n", id);
// }

// static ssize_t ots_obj_read(struct bt_ots *ots,
//                             struct bt_conn *conn,
//                             uint64_t id,
//                             void **data,
//                             size_t len,
//                             off_t offset)
// {
//     ARG_UNUSED(ots);
//     ARG_UNUSED(conn);

//     int slot = find_slot_by_id(id);
//     int ret;
//     size_t total;
//     int percent;

//     if (slot < 0)
//     {
//         return -ENOENT;
//     }

//     if (!data)
//     {
//         if (objects[slot].progress_pct < 100U)
//         {
//             objects[slot].progress_pct = 100U;
//             printk("100%%\n");
//         }
//         printk("[+]IMAGE SENT SUCCESS\n");
//         reset_on_disconnect = true;
//         return 0;
//     }

//     if (offset == 0)
//     {
//         objects[slot].progress_pct = 0;
//     }

//     len = MIN(len, sizeof(objects[slot].chunk));

//     ret = file_read_chunk(objects[slot].path,
//                           offset,
//                           objects[slot].chunk,
//                           len);

//     if (ret < 0)
//     {
//         return ret;
//     }

//     *data = objects[slot].chunk;

//     total = objects[slot].size.cur;
//     if (total == 0U)
//     {
//         return ret;
//     }

//     percent = (int)(((offset + (size_t)ret) * 100U) / total);

//     if (percent >= (objects[slot].progress_pct + 20) || percent == 100)
//     {
//         int step = (percent >= 100) ? 100 : (percent / 20) * 20;

//         if (step > objects[slot].progress_pct)
//         {
//             objects[slot].progress_pct = (uint8_t)step;
//             printk("%d%%\n", step);
//         }
//     }

//     return ret;
// }

// static ssize_t ots_obj_write(struct bt_ots *ots,
//                              struct bt_conn *conn,
//                              uint64_t id,
//                              const void *data,
//                              size_t len,
//                              off_t offset,
//                              size_t rem)
// {
//     ARG_UNUSED(ots);
//     ARG_UNUSED(conn);

//     int slot = find_slot_by_id(id);
//     int ret;
//     size_t total;
//     int percent;

//     if (slot < 0)
//     {
//         return -ENOENT;
//     }

//     if (offset == 0)
//     {
//         objects[slot].progress_pct = 0;
//         printk("Upload started: %s\n", objects[slot].name);
//     }

//     total = offset + len + rem;

//     if (total == 0)
//     {
//         percent = 0;
//     }
//     else
//     {
//         percent = (int)(((offset + len) * 100U) / total);
//     }

//     ret = file_write_chunk(objects[slot].path,
//                            offset,
//                            data,
//                            len);

//     if (ret < 0)
//     {
//         return ret;
//     }

//     if ((offset + len) > objects[slot].size.cur)
//     {
//         objects[slot].size.cur = offset + len;
//     }

//     /* Print only at 20% boundaries and at 100% */
//     if (percent >= (objects[slot].progress_pct + 20) || percent == 100)
//     {
//         int step = (percent >= 100) ? 100 : (percent / 20) * 20;

//         if (step > objects[slot].progress_pct)
//         {
//             objects[slot].progress_pct = (uint8_t)step;
//             printk("%s upload %d%%\n",
//                    objects[slot].name,
//                    step);
//         }
//     }


//     if (rem == 0)
//     {
//         printk("Image %s sent successfully\n",
//                objects[slot].name);
//     }

//     return len;
// }

// static void ots_obj_name_written(struct bt_ots *ots,
//                                  struct bt_conn *conn,
//                                  uint64_t id,
//                                  const char *cur_name,
//                                  const char *new_name)
// {
//     ARG_UNUSED(ots);
//     ARG_UNUSED(conn);
//     ARG_UNUSED(cur_name);

//     int slot = find_slot_by_id(id);

//     if (slot >= 0)
//     {
//         strncpy(objects[slot].name,
//                 new_name,
//                 OBJ_MAX_NAME);
//     }
// }

// static int ots_obj_cal_checksum(struct bt_ots *ots,
//                                 struct bt_conn *conn,
//                                 uint64_t id,
//                                 off_t offset,
//                                 size_t len,
//                                 void **data)
// {
//     ARG_UNUSED(ots);
//     ARG_UNUSED(conn);

//     int slot = find_slot_by_id(id);
//     int ret;

//     if (slot < 0)
//     {
//         return -ENOENT;
//     }

//     len = MIN(len, sizeof(objects[slot].chunk));

//     ret = file_read_chunk(objects[slot].path,
//                           offset,
//                           objects[slot].chunk,
//                           len);

//     if (ret < 0)
//     {
//         return ret;
//     }

//     *data = objects[slot].chunk;
//     return 0;
// }

// static struct bt_ots_cb ots_callbacks = {
//     .obj_created = ots_obj_created,
//     .obj_deleted = ots_obj_deleted,
//     .obj_selected = ots_obj_selected,
//     .obj_read = ots_obj_read,
//     .obj_write = ots_obj_write,
//     .obj_name_written = ots_obj_name_written,
//     .obj_cal_checksum = ots_obj_cal_checksum,
// };

// /* ------------------------------------------------------- */
// /* Init                                                    */
// /* ------------------------------------------------------- */

// int ots_server_init(void)
// {
//     int err;
//     size_t sz;

//     struct bt_ots *ots;
//     struct bt_ots_init_param init;
//     struct bt_ots_obj_add_param param;

//     ots = bt_ots_free_instance_get();

//     if (!ots)
//     {
//         return -ENOMEM;
//     }

//     ots_instance = ots;

//     memset(&init, 0, sizeof(init));

//     BT_OTS_OACP_SET_FEAT_READ(init.features.oacp);
//     BT_OTS_OACP_SET_FEAT_WRITE(init.features.oacp);
//     BT_OTS_OACP_SET_FEAT_CREATE(init.features.oacp);
//     BT_OTS_OACP_SET_FEAT_DELETE(init.features.oacp);
//     BT_OTS_OACP_SET_FEAT_PATCH(init.features.oacp);

//     BT_OTS_OLCP_SET_FEAT_GO_TO(init.features.olcp);

//     init.cb = &ots_callbacks;

//     err = bt_ots_init(ots, &init);
//     if (err)
//     {
//         return err;
//     }

//     if (get_file_size(LFS_PATH "/slot0_image.bin", &sz) == 0)
//     {

//         static struct preload_obj preload0;

//         memset(&preload0, 0, sizeof(preload0));

//         preload0.name = "slot0_image";
//         strcpy(preload0.path,
//                LFS_PATH "/slot0_image.bin");

//         preload0.size.cur = sz;
//         preload0.size.alloc = sz;

//         BT_OTS_OBJ_SET_PROP_READ(preload0.props);
//         BT_OTS_OBJ_SET_PROP_WRITE(preload0.props);
//         BT_OTS_OBJ_SET_PROP_PATCH(preload0.props);

//         object_being_created = &preload0;

//         memset(&param, 0, sizeof(param));
//         param.size = sz;
//         param.type.uuid.type = BT_UUID_TYPE_16;
//         param.type.uuid_16.val =
//             BT_UUID_OTS_TYPE_UNSPECIFIED_VAL;

//         bt_ots_obj_add(ots, &param);

//         object_being_created = NULL;
//     }

//     if (get_file_size(LFS_PATH "/slot1_image.bin", &sz) == 0)
//     {

//         static struct preload_obj preload1;

//         memset(&preload1, 0, sizeof(preload1));

//         preload1.name = "slot1_image";
//         strcpy(preload1.path,
//                LFS_PATH "/slot1_image.bin");

//         preload1.size.cur = sz;
//         preload1.size.alloc = sz;
//         printk("OTS metadata size = %u\n",
//                (uint32_t)preload1.size.cur);

//         BT_OTS_OBJ_SET_PROP_READ(preload1.props);
//         BT_OTS_OBJ_SET_PROP_WRITE(preload1.props);
//         BT_OTS_OBJ_SET_PROP_PATCH(preload1.props);

//         object_being_created = &preload1;

//         memset(&param, 0, sizeof(param));
//         param.size = sz;
//         param.type.uuid.type = BT_UUID_TYPE_16;
//         param.type.uuid_16.val =
//             BT_UUID_OTS_TYPE_UNSPECIFIED_VAL;

//         bt_ots_obj_add(ots, &param);

//         object_being_created = NULL;
//     }
//     if (get_file_size(LFS_PATH "/slot2_image.bin", &sz) == 0)
//     {

//         static struct preload_obj preload2;

//         memset(&preload2, 0, sizeof(preload2));

//         preload2.name = "slot2_image";
//         strcpy(preload2.path,
//                LFS_PATH "/slot2_image.bin");

//         preload2.size.cur = sz;
//         preload2.size.alloc = sz;

//         BT_OTS_OBJ_SET_PROP_READ(preload2.props);
//         BT_OTS_OBJ_SET_PROP_WRITE(preload2.props);
//         BT_OTS_OBJ_SET_PROP_PATCH(preload2.props);

//         object_being_created = &preload2;

//         memset(&param, 0, sizeof(param));
//         param.size = sz;
//         param.type.uuid.type = BT_UUID_TYPE_16;
//         param.type.uuid_16.val =
//             BT_UUID_OTS_TYPE_UNSPECIFIED_VAL;

               

//         bt_ots_obj_add(ots, &param);

//         object_being_created = NULL;
//     }
//     if (get_file_size(LFS_PATH "/slot3_image.bin", &sz) == 0)
//     {
//         static struct preload_obj preload3;
//         memset(&preload3, 0, sizeof(preload3));
//         preload3.name = "slot3_image";
//         strcpy(preload3.path,
//                LFS_PATH "/slot3_image.bin");
//         preload3.size.cur = sz;
//         preload3.size.alloc = sz;
//         BT_OTS_OBJ_SET_PROP_READ(preload3.props);
//         BT_OTS_OBJ_SET_PROP_WRITE(preload3.props);
//         BT_OTS_OBJ_SET_PROP_PATCH(preload3.props);
//         object_being_created = &preload3;
//         memset(&param, 0, sizeof(param));
//         param.size = sz;
//         param.type.uuid.type = BT_UUID_TYPE_16;
//         param.type.uuid_16.val =
//             BT_UUID_OTS_TYPE_UNSPECIFIED_VAL;
//         bt_ots_obj_add(ots, &param);
//         object_being_created = NULL;
//     }

//     printk("OTS server ready\n");

//     return 0;
// }

// /* ------------------------------------------------------- */
// /* Start advertising                                       */
// /* ------------------------------------------------------- */

// int ots_server_start(void)
// {
//     return bt_le_adv_start(BT_LE_ADV_CONN_FAST_1,
//                            ad, ARRAY_SIZE(ad),
//                            sd, ARRAY_SIZE(sd));
// }

// void ots_list_objects(void)
// {
//     char id_str[BT_OTS_OBJ_ID_STR_LEN];
//     int count = 0;

//     printk("---- OTS Object List ----\n");

//     for (int i = 0; i < OBJ_POOL_SIZE; i++)
//     {
//         if (!objects[i].used)
//         {
//             continue;
//         }

//         bt_ots_obj_id_to_str(objects[i].id, id_str, sizeof(id_str));

//         printk("Slot %d: name=%-16s id=%s size=%u path=%s\n",
//                i,
//                objects[i].name,
//                id_str,
//                objects[i].size.cur,
//                objects[i].path);

//         count++;
//     }

//     printk("Total objects: %d\n", count);
//     printk("--------------------------\n");
// }
// int meta_data_update(void)
// {
//     size_t sz;
//     int err;

//     if (get_file_size("/lfs/slot1_image.bin", &sz) < 0) {
//         return -ENOENT;
//     }

//     for (int i = 0; i < OBJ_POOL_SIZE; i++) {

//         if (!objects[i].used) {
//             continue;
//         }

//         if (strcmp(objects[i].name, "slot1_image") == 0) {

//             uint64_t old_id = objects[i].id;

//             recreate_in_progress = true;

//             err = bt_ots_obj_delete(ots_instance, old_id);
//             if (err) {
//                 printk("Delete failed %d\n", err);
//                 recreate_in_progress = false;
//                 return err;
//             }

//             recreate_in_progress = false;

//             static struct preload_obj preload1;

//             memset(&preload1, 0, sizeof(preload1));

//             preload1.name = "slot1_image";
//             strcpy(preload1.path,
//                    "/lfs/slot1_image.bin");

//             preload1.size.cur = sz;
//             preload1.size.alloc = sz;

//             BT_OTS_OBJ_SET_PROP_READ(preload1.props);
//             BT_OTS_OBJ_SET_PROP_WRITE(preload1.props);
//             BT_OTS_OBJ_SET_PROP_PATCH(preload1.props);

//             object_being_created = &preload1;

//             struct bt_ots_obj_add_param param = {0};

//             param.size = sz;
//             param.type.uuid.type = BT_UUID_TYPE_16;
//             param.type.uuid_16.val =
//                 BT_UUID_OTS_TYPE_UNSPECIFIED_VAL;

//             err = bt_ots_obj_add(ots_instance, &param);

//             object_being_created = NULL;

//             printk("slot1_image recreated size=%u err=%d\n",
//                    (uint32_t)sz, err);

//             return err;
//         }
//     }

//     return -ENOENT;
// }

/*
 * ots.c
 * Updated OTS server:
 * - LittleFS backed objects
 * - Upload progress logs
 * - Delayed advertising restart fix
 * - Better disconnect diagnostics
 * - Read + Write enabled preload objects
 * - DUAL OTS INSTANCE support for TRUE PARALLEL transfers
 *   to 2 simultaneously connected devices.
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

/* Number of parallel OTS instances == number of simultaneously
 * connected devices we support right now.
 * (Bump this + CONFIG_BT_OTS_MAX_INST_CNT + CONFIG_BT_MAX_CONN +
 *  CONFIG_BT_CTLR_SDC_PERIPHERAL_COUNT together when scaling up.)
 */
#define NUM_OTS_INSTANCES 2
#define TARGET_CONNECTIONS NUM_OTS_INSTANCES

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
    uint8_t chunk[OTS_CHUNK_SIZE];
};

struct preload_obj
{
    struct bt_ots_obj_size size;
    char *name;
    uint32_t props;
    char path[96];
};

/* Per-connection OTS context: one OTS service instance,
 * one object pool, bound to at most one bt_conn at a time.
 */
struct ots_instance_ctx
{
    struct bt_ots *ots;
    struct bt_conn *conn;
    bool recreate_in_progress;
    uint32_t obj_cnt;
    struct ots_file_obj objects[OBJ_POOL_SIZE];
};

static struct ots_instance_ctx ots_ctx[NUM_OTS_INSTANCES];

/* Set only while inside bt_ots_obj_add() during preload / recreate,
 * so ots_obj_created() knows what metadata to attach to the new
 * object. Since preload/recreate happen synchronously and are never
 * interleaved across instances, a single global pointer is fine.
 */
static struct preload_obj *object_being_created;

/* ------------------------------------------------------- */
/* Helpers                                                 */
/* ------------------------------------------------------- */

static struct ots_instance_ctx *ctx_from_ots(struct bt_ots *ots)
{
    for (int i = 0; i < NUM_OTS_INSTANCES; i++)
    {
        if (ots_ctx[i].ots == ots)
        {
            return &ots_ctx[i];
        }
    }
    return NULL;
}

static struct ots_instance_ctx *ctx_from_conn(struct bt_conn *conn)
{
    for (int i = 0; i < NUM_OTS_INSTANCES; i++)
    {
        if (ots_ctx[i].conn == conn)
        {
            return &ots_ctx[i];
        }
    }
    return NULL;
}

static int get_free_slot(struct ots_instance_ctx *ctx)
{
    for (int i = 0; i < OBJ_POOL_SIZE; i++)
    {
        if (!ctx->objects[i].used)
        {
            return i;
        }
    }
    return -1;
}

static int find_slot_by_id(struct ots_instance_ctx *ctx, uint64_t id)
{
    for (int i = 0; i < OBJ_POOL_SIZE; i++)
    {
        if (ctx->objects[i].used && ctx->objects[i].id == id)
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

static struct bt_conn *connected_conns[TARGET_CONNECTIONS];
static uint8_t connected_count;
static bool all_connected;

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

    if (err == -EALREADY)
    {
        return;
    }

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
    if (err) {
        printk("Connection failed %u %s\n",
               err, bt_hci_err_to_str(err));
        return;
    }

    printk("Connected\n");

    if (connected_count < TARGET_CONNECTIONS) {
        connected_conns[connected_count] = bt_conn_ref(conn);
        connected_count++;

        printk("Connected %d/%d\n",
               connected_count,
               TARGET_CONNECTIONS);
    }

    /* Bind this connection to a free OTS instance so its
     * OACP/OLCP callbacks resolve to its own private object pool.
     */
    for (int i = 0; i < NUM_OTS_INSTANCES; i++) {
        if (ots_ctx[i].conn == NULL) {
            ots_ctx[i].conn = conn;
            printk("Bound connection to OTS instance %d\n", i);
            break;
        }
    }

    if (connected_count < TARGET_CONNECTIONS) {

        /* Keep advertising for the next device */
        k_work_submit(&restart_adv_work);

    } else {

        all_connected = true;

        printk("All %d devices connected\n", TARGET_CONNECTIONS);

        /* DO NOT start transfer here.
         * Just set the flag.
         */
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    for (int i = 0; i < connected_count; i++) {

        if (connected_conns[i] == conn) {

            bt_conn_unref(connected_conns[i]);

            for (int j = i; j < connected_count - 1; j++) {
                connected_conns[j] = connected_conns[j + 1];
            }

            connected_conns[connected_count - 1] = NULL;
            connected_count--;

            break;
        }
    }

    /* Free the OTS instance this connection owned so a future
     * connection can reuse it.
     */
    struct ots_instance_ctx *ctx = ctx_from_conn(conn);
    if (ctx) {
        printk("Unbinding connection from OTS instance\n");
        ctx->conn = NULL;
    }

    all_connected = false;

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
    ARG_UNUSED(conn);
    ARG_UNUSED(add_param);

    struct ots_instance_ctx *ctx = ctx_from_ots(ots);

    if (!ctx)
    {
        return -ENOENT;
    }

    int slot = get_free_slot(ctx);

    if (slot < 0)
    {
        return -ENOMEM;
    }

    memset(&ctx->objects[slot], 0, sizeof(ctx->objects[slot]));

    ctx->objects[slot].used = true;
    ctx->objects[slot].id = id;

    if (object_being_created)
    {

        strcpy(ctx->objects[slot].name,
               object_being_created->name);

        strcpy(ctx->objects[slot].path,
               object_being_created->path);

        ctx->objects[slot].size = object_being_created->size;
        ctx->objects[slot].props = object_being_created->props;
    }
    else
    {

        snprintf(ctx->objects[slot].name,
                 sizeof(ctx->objects[slot].name),
                 "object_%d", slot);

        snprintf(ctx->objects[slot].path,
                 sizeof(ctx->objects[slot].path),
                 LFS_PATH "/object_%d.bin", slot);

        ctx->objects[slot].size.cur = 0;
        ctx->objects[slot].size.alloc = 0xffffffff;

        BT_OTS_OBJ_SET_PROP_READ(ctx->objects[slot].props);
        BT_OTS_OBJ_SET_PROP_WRITE(ctx->objects[slot].props);
        BT_OTS_OBJ_SET_PROP_PATCH(ctx->objects[slot].props);
        BT_OTS_OBJ_SET_PROP_DELETE(ctx->objects[slot].props);
    }

    created_desc->name = ctx->objects[slot].name;
    created_desc->size = ctx->objects[slot].size;
    created_desc->props = ctx->objects[slot].props;

    ctx->obj_cnt++;

    printk("Object created: %s\n", ctx->objects[slot].name);

    return 0;
}

static int ots_obj_deleted(struct bt_ots *ots,
                           struct bt_conn *conn,
                           uint64_t id)
{
    ARG_UNUSED(conn);

    struct ots_instance_ctx *ctx = ctx_from_ots(ots);

    if (!ctx)
    {
        return -ENOENT;
    }

    int slot = find_slot_by_id(ctx, id);

    if (slot < 0)
    {
        return -ENOENT;
    }
    if (!ctx->recreate_in_progress)
    {
        fs_unlink(ctx->objects[slot].path);
    }
    memset(&ctx->objects[slot], 0, sizeof(ctx->objects[slot]));

    if (ctx->obj_cnt)
    {
        ctx->obj_cnt--;
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
    ARG_UNUSED(conn);

    struct ots_instance_ctx *ctx = ctx_from_ots(ots);

    if (!ctx)
    {
        return -ENOENT;
    }

    int slot = find_slot_by_id(ctx, id);
    int ret;
    size_t total;
    int percent;

    if (slot < 0)
    {
        return -ENOENT;
    }

    if (!data)
    {
        if (ctx->objects[slot].progress_pct < 100U)
        {
            ctx->objects[slot].progress_pct = 100U;
            printk("100%%\n");
        }
        printk("[+]IMAGE SENT SUCCESS\n");
        reset_on_disconnect = true;
        return 0;
    }

    if (offset == 0)
    {
        ctx->objects[slot].progress_pct = 0;
    }

    len = MIN(len, sizeof(ctx->objects[slot].chunk));

    ret = file_read_chunk(ctx->objects[slot].path,
                          offset,
                          ctx->objects[slot].chunk,
                          len);

    if (ret < 0)
    {
        return ret;
    }

    *data = ctx->objects[slot].chunk;

    total = ctx->objects[slot].size.cur;
    if (total == 0U)
    {
        return ret;
    }

    percent = (int)(((offset + (size_t)ret) * 100U) / total);

    if (percent >= (ctx->objects[slot].progress_pct + 20) || percent == 100)
    {
        int step = (percent >= 100) ? 100 : (percent / 20) * 20;

        if (step > ctx->objects[slot].progress_pct)
        {
            ctx->objects[slot].progress_pct = (uint8_t)step;
            printk("%d%%\n", step);
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
    ARG_UNUSED(conn);

    struct ots_instance_ctx *ctx = ctx_from_ots(ots);

    if (!ctx)
    {
        return -ENOENT;
    }

    int slot = find_slot_by_id(ctx, id);
    int ret;
    size_t total;
    int percent;

    if (slot < 0)
    {
        return -ENOENT;
    }

    if (offset == 0)
    {
        ctx->objects[slot].progress_pct = 0;
        printk("Upload started: %s\n", ctx->objects[slot].name);
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

    ret = file_write_chunk(ctx->objects[slot].path,
                           offset,
                           data,
                           len);

    if (ret < 0)
    {
        return ret;
    }

    if ((offset + len) > ctx->objects[slot].size.cur)
    {
        ctx->objects[slot].size.cur = offset + len;
    }

    /* Print only at 20% boundaries and at 100% */
    if (percent >= (ctx->objects[slot].progress_pct + 20) || percent == 100)
    {
        int step = (percent >= 100) ? 100 : (percent / 20) * 20;

        if (step > ctx->objects[slot].progress_pct)
        {
            ctx->objects[slot].progress_pct = (uint8_t)step;
            printk("%s upload %d%%\n",
                   ctx->objects[slot].name,
                   step);
        }
    }


    if (rem == 0)
    {
        printk("Image %s sent successfully\n",
               ctx->objects[slot].name);
    }

    return len;
}

static void ots_obj_name_written(struct bt_ots *ots,
                                 struct bt_conn *conn,
                                 uint64_t id,
                                 const char *cur_name,
                                 const char *new_name)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(cur_name);

    struct ots_instance_ctx *ctx = ctx_from_ots(ots);

    if (!ctx)
    {
        return;
    }

    int slot = find_slot_by_id(ctx, id);

    if (slot >= 0)
    {
        strncpy(ctx->objects[slot].name,
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
    ARG_UNUSED(conn);

    struct ots_instance_ctx *ctx = ctx_from_ots(ots);

    if (!ctx)
    {
        return -ENOENT;
    }

    int slot = find_slot_by_id(ctx, id);
    int ret;

    if (slot < 0)
    {
        return -ENOENT;
    }

    len = MIN(len, sizeof(ctx->objects[slot].chunk));

    ret = file_read_chunk(ctx->objects[slot].path,
                          offset,
                          ctx->objects[slot].chunk,
                          len);

    if (ret < 0)
    {
        return ret;
    }

    *data = ctx->objects[slot].chunk;
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
/* Preload helper                                          */
/* ------------------------------------------------------- */

static void preload_slot_into_instance(struct ots_instance_ctx *ctx,
                                        const char *fs_path,
                                        const char *obj_name)
{
    size_t sz;

    if (get_file_size(fs_path, &sz) != 0)
    {
        return;
    }

    static struct preload_obj preload;
    struct bt_ots_obj_add_param param;

    memset(&preload, 0, sizeof(preload));

    preload.name = (char *)obj_name;
    strncpy(preload.path, fs_path, sizeof(preload.path) - 1);

    preload.size.cur = sz;
    preload.size.alloc = sz;

    BT_OTS_OBJ_SET_PROP_READ(preload.props);
    BT_OTS_OBJ_SET_PROP_WRITE(preload.props);
    BT_OTS_OBJ_SET_PROP_PATCH(preload.props);

    object_being_created = &preload;

    memset(&param, 0, sizeof(param));
    param.size = sz;
    param.type.uuid.type = BT_UUID_TYPE_16;
    param.type.uuid_16.val = BT_UUID_OTS_TYPE_UNSPECIFIED_VAL;

    bt_ots_obj_add(ctx->ots, &param);

    object_being_created = NULL;
}

/* ------------------------------------------------------- */
/* Init                                                    */
/* ------------------------------------------------------- */

int ots_server_init(void)
{
    int err;

    for (int i = 0; i < NUM_OTS_INSTANCES; i++)
    {
        struct bt_ots_init_param init;

        ots_ctx[i].ots = bt_ots_free_instance_get();

        if (!ots_ctx[i].ots)
        {
            return -ENOMEM;
        }

        memset(&init, 0, sizeof(init));

        BT_OTS_OACP_SET_FEAT_READ(init.features.oacp);
        BT_OTS_OACP_SET_FEAT_WRITE(init.features.oacp);
        BT_OTS_OACP_SET_FEAT_CREATE(init.features.oacp);
        BT_OTS_OACP_SET_FEAT_DELETE(init.features.oacp);
        BT_OTS_OACP_SET_FEAT_PATCH(init.features.oacp);

        BT_OTS_OLCP_SET_FEAT_GO_TO(init.features.olcp);

        init.cb = &ots_callbacks;

        err = bt_ots_init(ots_ctx[i].ots, &init);
        if (err)
        {
            return err;
        }

        /* Same slot0-3 images preloaded into EVERY instance, so
         * either connected device can read/write any of them
         * independently and in parallel (each instance has its
         * own object pool + L2CAP channel).
         */
        preload_slot_into_instance(&ots_ctx[i], LFS_PATH "/slot0_image.bin", "slot0_image");
        preload_slot_into_instance(&ots_ctx[i], LFS_PATH "/slot1_image.bin", "slot1_image");
        preload_slot_into_instance(&ots_ctx[i], LFS_PATH "/slot2_image.bin", "slot2_image");
        preload_slot_into_instance(&ots_ctx[i], LFS_PATH "/slot3_image.bin", "slot3_image");

        printk("OTS instance %d ready\n", i);
    }

    printk("OTS server ready (%d parallel instances)\n", NUM_OTS_INSTANCES);

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

void ots_list_objects(void)
{
    char id_str[BT_OTS_OBJ_ID_STR_LEN];
    int count = 0;

    printk("---- OTS Object List ----\n");

    for (int i = 0; i < NUM_OTS_INSTANCES; i++)
    {
        printk("-- Instance %d --\n", i);

        for (int s = 0; s < OBJ_POOL_SIZE; s++)
        {
            if (!ots_ctx[i].objects[s].used)
            {
                continue;
            }

            bt_ots_obj_id_to_str(ots_ctx[i].objects[s].id, id_str, sizeof(id_str));

            printk("  Slot %d: name=%-16s id=%s size=%u path=%s\n",
                   s,
                   ots_ctx[i].objects[s].name,
                   id_str,
                   ots_ctx[i].objects[s].size.cur,
                   ots_ctx[i].objects[s].path);

            count++;
        }
    }

    printk("Total objects: %d\n", count);
    printk("--------------------------\n");
}

/* Recreates "slot1_image" (after it has been rewritten on flash)
 * inside EVERY OTS instance, so every connected device's copy of
 * the metadata/object gets refreshed - not just one connection's.
 */
int meta_data_update(void)
{
    size_t sz;
    int err = -ENOENT;

    if (get_file_size(LFS_PATH "/slot1_image.bin", &sz) < 0)
    {
        return -ENOENT;
    }

    for (int i = 0; i < NUM_OTS_INSTANCES; i++)
    {
        struct ots_instance_ctx *ctx = &ots_ctx[i];

        for (int s = 0; s < OBJ_POOL_SIZE; s++)
        {
            if (!ctx->objects[s].used)
            {
                continue;
            }

            if (strcmp(ctx->objects[s].name, "slot1_image") != 0)
            {
                continue;
            }

            uint64_t old_id = ctx->objects[s].id;

            ctx->recreate_in_progress = true;

            err = bt_ots_obj_delete(ctx->ots, old_id);
            if (err)
            {
                printk("Instance %d: delete failed %d\n", i, err);
                ctx->recreate_in_progress = false;
                continue;
            }

            ctx->recreate_in_progress = false;

            preload_slot_into_instance(ctx, LFS_PATH "/slot1_image.bin", "slot1_image");

            printk("Instance %d: slot1_image recreated size=%u\n",
                   i, (uint32_t)sz);

            break;
        }
    }

    return err;
}
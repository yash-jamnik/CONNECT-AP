/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NRF54L15_XIAO_AP_CONNECT_DRIVER_OTS_H_
#define NRF54L15_XIAO_AP_CONNECT_DRIVER_OTS_H_

#include <stdint.h>
#include <stddef.h>

int ots_server_init(void);
int ots_server_start(void);
int ots_server_stop(void);
int meta_data_update(void);
int ots_delete_first_object(void);
void reload_latest_image(void);
void ots_set_first_object_data(const uint8_t *data, size_t size);
int ots_create_first_object(const uint8_t *data, size_t size);
#endif

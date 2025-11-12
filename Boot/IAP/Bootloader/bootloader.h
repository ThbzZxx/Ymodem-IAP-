#ifndef __BOOTLOARDER_H
#define __BOOTLOARDER_H

#include "string.h"
#include "stm32f10x.h"                  // Device header
#include "iap_config.h"  // 引入新的配置定义


 /*
+-------------------+  0x08000000
|   Bootloader      |  16KB  (0x08000000 - 0x08004000)
|   包含升级逻辑    |
+-------------------+  0x08004000
|   配置区(Config)  |  2KB   (0x08004000 - 0x08004800)
|   存储版本/状态   |
+-------------------+  0x08004800
|   APP A区         |  20KB  (0x08004800 - 0x08009800)
|   主应用程序      |
+-------------------+  0x08009800
|   APP B区         |  20KB  (0x08009800 - 0x0800E800)
|   备份应用程序    |
+-------------------+  0x0800E800
|   日志区(Log)     |  2KB   (0x0800E800 - 0x0800F000)
|   升级日志记录    |
+-------------------+  0x0800F000 (60KB使用，留4KB余量) ✅

*/
// ========== 默认定义（指向A区） ==========
#define APP_SECTOR_ADDR         APP_A_SECTOR_ADDR      // 默认指向A区
#define APP_SECTOR_SIZE         APP_A_SECTOR_SIZE      // 默认20KB
#define APP_ERASE_SECTORS       APP_A_ERASE_SECTORS    // 默认20扇区

// ========== 新定义已在iap_config.h中 ==========
// BOOT_SECTOR_ADDR, CONFIG_AREA_ADDR
// APP_A_SECTOR_ADDR, APP_B_SECTOR_ADDR
// LOG_AREA_ADDR

typedef void (*iapfun)(void);

// ========== 核心功能函数 ==========
uint8_t iap_load_app(uint32_t appxaddr);
uint8_t mcu_flash_erase(uint32_t addr, uint16_t sector_num);
uint8_t mcu_flash_write(uint32_t addr, uint8_t *buffer, uint32_t length);
void mcu_flash_read(uint32_t addr, uint8_t *buffer, uint32_t length);

// ========== Bootloader工具函数 ==========
void jump_to_app(uint8_t bank);
void upgrade_process(void);
uint8_t try_boot_firmware(void);
void enter_upgrade_wait_mode(void);

#endif


#ifndef __BOOTLOARDER_H
#define __BOOTLOARDER_H

#include "string.h" 
#include "stm32f10x.h"                  // Device header

/*****************************************
| 0x08000000 |  0x08003000  | 
------------------------------------------
|    BOOT    |      APP     |
------------------------------------------
|    12k     |      52K     |
******************************************/

#define FLASH_SECTOR_SIZE       1024   //MCU sector size
#define FLASH_SECTOR_NUM        64     // 64k for STM32F103C8T6
#define FLASH_START_ADDR        ((uint32_t)0x08000000)
#define FLASH_END_ADDR          ((uint32_t)(0x08000000 + FLASH_SECTOR_NUM * FLASH_SECTOR_SIZE))

#define BOOT_SECTOR_ADDR        0x08000000      // BOOT sector start address
#define BOOT_SECTOR_SIZE        0x3000         // 12KB Bootloader
#define APP_SECTOR_ADDR         0x08003000      // APP sector start address 
#define APP_SECTOR_SIZE         0xD000         // APP sector size// (52KB)
#define APP_ERASE_SECTORS       (APP_SECTOR_SIZE / FLASH_SECTOR_SIZE)   //52KB/1024=52

typedef void (*iapfun)(void);

// 核心功能函数保留
uint8_t iap_load_app(uint32_t appxaddr);
uint8_t mcu_flash_erase(uint32_t addr, uint16_t sector_num);
uint8_t mcu_flash_write(uint32_t addr, uint8_t *buffer, uint32_t length);
void mcu_flash_read(uint32_t addr, uint8_t *buffer, uint32_t length);

#endif

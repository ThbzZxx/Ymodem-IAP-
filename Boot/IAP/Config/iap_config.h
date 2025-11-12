#ifndef __IAP_CONFIG_H
#define __IAP_CONFIG_H

#include "stdint.h"

// ==================== Flash分区定义 ====================
// 注意：STM32F103C8T6总Flash 64KB

#define FLASH_SECTOR_SIZE       1024   // MCU扇区大小
#define FLASH_SECTOR_NUM        64     // 64KB for STM32F103C8T6
#define FLASH_START_ADDR        ((uint32_t)0x08000000)
#define FLASH_END_ADDR          ((uint32_t)(0x08000000 + FLASH_SECTOR_NUM * FLASH_SECTOR_SIZE))

// Bootloader区域
#define BOOT_SECTOR_ADDR        0x08000000
#define BOOT_SECTOR_SIZE        0x4000         // 16KB

// 配置区（存储版本信息、状态等）
#define CONFIG_AREA_ADDR        0x08004000
#define CONFIG_AREA_SIZE        0x800          // 2KB

// APP A区（主应用程序分区）
#define APP_A_SECTOR_ADDR       0x08004800
#define APP_A_SECTOR_SIZE       0x5000         // 20KB
#define APP_A_ERASE_SECTORS     (APP_A_SECTOR_SIZE / FLASH_SECTOR_SIZE)  // 20

// APP B区（备份应用程序分区）
#define APP_B_SECTOR_ADDR       0x08009800
#define APP_B_SECTOR_SIZE       0x5000         // 20KB
#define APP_B_ERASE_SECTORS     (APP_B_SECTOR_SIZE / FLASH_SECTOR_SIZE)  // 20

// 日志区（升级日志）
#define LOG_AREA_ADDR           0x0800E800
#define LOG_AREA_SIZE           0x800          // 2KB

// APP Bank大小
#define APP_BANK_SIZE           APP_A_SECTOR_SIZE

// ==================== 固件信息结构体 ====================

// 固件信息  24字节
typedef struct __attribute__((packed)) {
    uint32_t magic;           // 魔术字 0x5AA5F00F
    uint8_t  version_major;   // 主版本号
    uint8_t  version_minor;   // 次版本号
    uint8_t  version_patch;   // 补丁版本号
    uint8_t  reserved1;
    uint32_t firmware_size;   // 固件大小（字节）
    uint32_t firmware_crc32;  // 固件CRC32校验值
    uint32_t build_timestamp; // 编译时间戳
    uint8_t  is_valid;        // 固件有效标志 0xAA=有效
    uint8_t  reserved2[3];
} firmware_info_t;

// 魔术字定义
#define FIRMWARE_MAGIC          0x5AA5F00F
#define CONFIG_MAGIC            0xA5A5A5A5
#define FIRMWARE_VALID_FLAG     0xAA

// ==================== 系统配置结构体 ====================

// 升级状态定义
#define UPGRADE_STATUS_IDLE          0x00  // 空闲
#define UPGRADE_STATUS_DOWNLOADING   0x01  // 下载中
#define UPGRADE_STATUS_VERIFYING     0x02  // 校验中
#define UPGRADE_STATUS_INSTALLING    0x03  // 安装中
#define UPGRADE_STATUS_SUCCESS       0x04  // 成功
#define UPGRADE_STATUS_FAILED        0x05  // 失败

// 系统配置结构体	60字节
typedef struct __attribute__((packed)) {
    uint32_t magic;              // 魔术字 0xA5A5A5A5
    uint8_t  active_bank;        // 当前激活分区 0=A区 1=B区
    uint8_t  upgrade_status;     // 升级状态
    uint8_t  boot_count;         // 启动计数器
    uint8_t  max_boot_retry;     // 最大启动重试次数（默认3）
    firmware_info_t bank_a_info; // A区固件信息
    firmware_info_t bank_b_info; // B区固件信息
    uint32_t config_crc32;       // 配置区CRC32校验
} system_config_t;

// ==================== 日志相关定义 ====================

// 升级日志条目（16字节）
typedef struct {
    uint32_t timestamp;       // 时间戳（系统tick或秒数）
    uint8_t  event_type;      // 事件类型
    uint8_t  from_version[3]; // 源版本[major, minor, patch]
    uint8_t  to_version[3];   // 目标版本[major, minor, patch]
    uint8_t  result;          // 结果 0=失败 1=成功
    uint8_t  error_code;      // 错误码
    uint8_t  reserved[2];
} upgrade_log_t;

// 日志事件类型
#define LOG_EVENT_UPGRADE_START   0x01
#define LOG_EVENT_UPGRADE_SUCCESS 0x02
#define LOG_EVENT_UPGRADE_FAILED  0x03
#define LOG_EVENT_ROLLBACK        0x04
#define LOG_EVENT_BOOT_FAIL       0x05

// 最大日志条目数（2KB / 16B = 128条）
#define MAX_LOG_ENTRIES  128

#endif // __IAP_CONFIG_H

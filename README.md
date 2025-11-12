# 双Bank IAP固件升级系统

基于 Ymodem 协议的双分区（Dual-Bank）固件在线升级方案，支持固件备份、自动回滚和完整性验证。

---

## 目录

1. [系统架构设计](#1-系统架构设计)
2. [分区功能详解](#2-分区功能详解)
3. [固件升级流程](#3-固件升级流程)
4. [开发与调试](#4-开发与调试)

---

## 1. 系统架构设计

### 1.1 Flash 分区布局

STM32F103C8T6 拥有 64KB Flash，采用如下分区方案:

```
╔═══════════════════════════════════════╗
║  0x08000000                           ║
╠═══════════════════════════════════════╣
║                                       ║
║       Bootloader (16KB)               ║  硬件初始化
║       系统引导程序                     ║  Ymodem协议
║                                       ║  固件验证与跳转
╠═══════════════════════════════════════╣
║  0x08004000                           ║
╠═══════════════════════════════════════╣
║                                       ║  系统配置
║   Config Area 2KB                     ║  固件版本信息
╠═══════════════════════════════════════╣  启动计数器
║  0x08004800                           ║
╠═══════════════════════════════════════╣
║                                       ║
║       APP1 Area 20KB                  ║  主应用程序
║                                       ║  [固件头 24B] + [代码]
║                                       ║
╠═══════════════════════════════════════╣
║  0x08009800                           ║
╠═══════════════════════════════════════╣
║                                       ║
║       APP2 Area 20KB                  ║  备份应用程序
║                                       ║  [固件头 24B] + [代码]
║                                       ║
╠═══════════════════════════════════════╣
║  0x0800E800                           ║
╠═══════════════════════════════════════╣
║   LOG Area 2KB                        ║  升级历史记录
║                                       ║  回滚事件日志
╠═══════════════════════════════════════╣
║  0x0800F000                           ║
╠═══════════════════════════════════════╣
║   reserve 4KB                         ║  预留空间
╠═══════════════════════════════════════╣
║  0x08010000 (64KB)                    ║
╚═══════════════════════════════════════╝
```

### 1.2 核心设计思想

#### 1.2.1 双Bank架构

- **主分区（A区）**：当前运行的固件
- **备份分区（B区）**：用于升级新固件或作为备份
- **原子切换**：通过修改配置区的 `active_bank` 标志实现分区切换
- **零风险升级**：升级过程写入非激活分区，不影响当前运行固件

#### 1.2.2 容错机制

**启动计数器（Boot Counter）**

```c
启动流程:
1. boot_count++
2. if (boot_count > max_boot_retry)  // 默认 max_boot_retry = 3
3.     切换到备份分区
4.     boot_count = 0
5. else
6.     尝试启动当前分区固件
7. APP启动成功后清零 boot_count
```

**自动回滚场景**:
- 固件运行崩溃，无法清零计数器
- 连续重启3次后自动回滚到上一个稳定版本
- 系统恢复到可用状态

#### 1.2.3 双重校验机制

| 校验层级 | 类型 | 时机 | 作用 |
|---------|------|------|------|
| **传输层** | Ymodem CRC16 | 每个数据包 | 检测传输错误、支持重传 |
| **应用层** | 固件整体 CRC32 | 传输完成后 | 验证Flash完整性、防篡改 |

**为什么需要两层校验？**

```
场景1: 传输成功但Flash写入失败
  Ymodem CRC16 ✓ → 所有数据包校验通过
  Flash写入   ✗ → 某扇区写入失败
  固件CRC32   ✗ → 检测到固件损坏，拒绝使用

场景2: Flash位翻转（长期运行）
  运行1年后某个Flash bit因老化翻转
  下次启动时固件CRC32校验失败
  自动切换到备份分区 ✓
```

### 1.3 关键数据结构

#### 固件信息 (24字节)

```c
typedef struct {
    uint32_t magic;           // 0x5AA5F00F (魔术字)
    uint8_t  version_major;   // 主版本号
    uint8_t  version_minor;   // 次版本号
    uint8_t  version_patch;   // 补丁版本号
    uint8_t  reserved1;       // 保留
    uint32_t firmware_size;   // 固件大小（字节）
    uint32_t firmware_crc32;  // 固件CRC32校验值
    uint32_t build_timestamp; // 编译时间戳
    uint8_t  is_valid;        // 0xAA=有效
    uint8_t  reserved2[3];    // 保留
} firmware_info_t;
```

#### 系统配置 (60字节)

```c
typedef struct {
    uint32_t magic;              // 0xA5A5A5A5
    uint8_t  active_bank;        // 0=A区, 1=B区
    uint8_t  upgrade_status;     // 升级状态
    uint8_t  boot_count;         // 启动计数器
    uint8_t  max_boot_retry;     // 最大重试次数(默认3)
    firmware_info_t bank_a_info; // A区固件信息
    firmware_info_t bank_b_info; // B区固件信息
    uint32_t config_crc32;       // 配置CRC32
} system_config_t;
```

#### 升级状态定义

```c
#define UPGRADE_STATUS_IDLE          0x00  // 空闲
#define UPGRADE_STATUS_DOWNLOADING   0x01  // 下载中
#define UPGRADE_STATUS_VERIFYING     0x02  // 校验中
#define UPGRADE_STATUS_INSTALLING    0x03  // 安装中
#define UPGRADE_STATUS_SUCCESS       0x04  // 成功
#define UPGRADE_STATUS_FAILED        0x05  // 失败
```

---

## 2. 分区功能详解

### 2.1 Bootloader 区 (16KB)

**职责**: 系统引导、固件升级、完整性验证、分区管理

#### 2.1.1 核心模块

**配置管理 (config_manager.c)**

```c
// 读取配置区
uint8_t config_read(system_config_t *config)
{
    // 从Flash读取配置
    mcu_flash_read(CONFIG_AREA_ADDR, (uint8_t*)config, sizeof(system_config_t));

    // 验证魔术字
    if (config->magic != CONFIG_MAGIC) {
        return 0;  // 配置无效
    }

    // 验证CRC32
    uint32_t crc = crc32_calculate((uint8_t*)config, sizeof(system_config_t) - 4);
    if (crc != config->config_crc32) {
        return 0;  // CRC校验失败
    }

    return 1;  // 配置有效
}

// 保存配置到Flash
uint8_t config_save(system_config_t *config)
{
    // 计算CRC32
    config->config_crc32 = crc32_calculate((uint8_t*)config,
                                           sizeof(system_config_t) - 4);

    // 擦除配置区（2KB = 2个扇区）
    mcu_flash_erase(CONFIG_AREA_ADDR, CONFIG_AREA_SIZE / FLASH_SECTOR_SIZE);

    // 写入Flash
    return mcu_flash_write(CONFIG_AREA_ADDR, (uint8_t*)config,
                          sizeof(system_config_t));
}

// 启动计数器管理
uint8_t handle_boot_counter(void)
{
    // 检查是否有任何有效固件
    if (!has_valid_firmware()) {
        return 0;  // 需要进入升级模式
    }

    // 递增启动计数器
    g_config.boot_count++;

    // 检查是否超过最大重试次数
    if (g_config.boot_count > g_config.max_boot_retry) {
        // 超过最大重试次数，切换到备份分区
        g_config.active_bank = !g_config.active_bank;
        g_config.boot_count = 0;
        config_save(&g_config);

        led_status_indicate(4); // 4次闪烁：分区回退
    } else {
        config_save(&g_config);
    }

    return 1;  // 继续启动流程
}
```

**固件验证 (firmware_verify.c)**

```c
// 验证指定分区的固件
uint8_t firmware_verify(uint8_t bank)
{
    firmware_info_t *fw_info;
    uint32_t app_addr;

    // 确定分区地址
    if (bank == 0) {
        fw_info = &g_config.bank_a_info;
        app_addr = APP_A_SECTOR_ADDR;
    } else {
        fw_info = &g_config.bank_b_info;
        app_addr = APP_B_SECTOR_ADDR;
    }

    // 验证魔术字
    if (fw_info->magic != FIRMWARE_MAGIC) {
        return 0;
    }

    // 验证有效标志
    if (fw_info->is_valid != FIRMWARE_VALID_FLAG) {
        return 0;
    }

    // 验证固件大小
    if (fw_info->firmware_size == 0 ||
        fw_info->firmware_size > APP_BANK_SIZE) {
        return 0;
    }

    // 计算实际CRC32（跳过24字节固件头）
    uint32_t calculated_crc = crc32_calculate_flash(app_addr + 24,
                                                     fw_info->firmware_size);

    // 比对CRC32
    if (calculated_crc != fw_info->firmware_crc32) {
        return 0;  // CRC校验失败
    }

    // 验证栈指针有效性
    uint32_t stack_ptr = *(__IO uint32_t*)(app_addr + 24);
    if ((stack_ptr & 0x2FFF0000) != 0x20000000) {
        return 0;  // 栈指针无效
    }

    return 1;  // 固件有效
}
```

**跳转到应用程序 (bootloader.c)**

```c
void jump_to_app(uint8_t bank)
{
    uint32_t app_addr;

    // 确定跳转地址（跳过24字节固件头）
    if (bank == 0) {
        app_addr = APP_A_SECTOR_ADDR + 24;
    } else {
        app_addr = APP_B_SECTOR_ADDR + 24;
    }

    LED1_OFF();

    // 执行跳转
    iap_load_app(app_addr);
}

// IAP跳转实现
uint8_t iap_load_app(uint32_t appxaddr)
{
    uint32_t jump_addr;
    uint32_t stack_ptr = *(__IO uint32_t*)appxaddr;

    // 验证栈指针
    if ((stack_ptr & 0x2FFF0000) == 0x20000000) {
        jump_addr = *(__IO uint32_t*)(appxaddr + 4);
        jump2app = (iapfun)jump_addr;

        // 关闭全局中断
        __disable_irq();

        // 关闭滴答定时器
        SysTick->CTRL = 0;
        SysTick->LOAD = 0;
        SysTick->VAL  = 0;

        // 关闭并清除所有中断
        for (i = 0; i < 8; i++) {
            NVIC->ICER[i] = 0xFFFFFFFF;
            NVIC->ICPR[i] = 0xFFFFFFFF;
        }

        // 设置向量表偏移
        SCB->VTOR = appxaddr;

        // 设置栈指针
        __set_MSP(stack_ptr);

        // 设置为特权模式
        __set_CONTROL(0);
        __ISB();  // 指令同步屏障

        // 跳转到APP
        jump2app();
    }

    return 0;
}
```

#### 2.1.2 Bootloader 主流程

```c
int main(void)
{
    // ========== 步骤1：硬件初始化 ==========
    LED_GPIO_Config();
    Key_GPIO_Config();
    ymodem_init();

    // ========== 步骤2：读取并初始化配置 ==========
    if (!init_system_config()) {
        // 配置初始化失败
        while (1) {
            led_fast_blink(1, 100);  // 持续快闪表示严重错误
        }
    }

    // ========== 步骤3：检查按键强制升级 ==========
    if (Key_Scan(KEY1_GPIO_PORT, KEY1_GPIO_PIN) == 1) {
        upgrade_process();  // 进入升级流程
        NVIC_SystemReset();
    }

    // ========== 步骤4：检查升级标志 ==========
    if (g_config.upgrade_status == UPGRADE_STATUS_DOWNLOADING) {
        upgrade_process();  // 重新进入升级流程
        NVIC_SystemReset();
    }

    // ========== 步骤5：容错机制 ==========
    if (!handle_boot_counter()) {
        enter_upgrade_wait_mode();  // 无有效固件，等待升级
    }

    // ========== 步骤6：验证并启动固件 ==========
    if (!try_boot_firmware()) {
        enter_upgrade_wait_mode();  // 两个分区都无效
    }

    // 跳转失败，严重错误
    led_status_indicate(9);
    NVIC_SystemReset();
}
```

### 2.2 配置区 (2KB)

**职责**: 持久化存储系统状态和固件信息

#### 2.2.1 存储内容

- 当前激活分区标志 (`active_bank`)
- 两个分区的固件版本和CRC信息
- 启动计数器（用于容错回滚）
- 升级状态（用于断电恢复）

#### 2.2.2 读写保护

```c
// 配置区的写入操作都经过CRC32保护
config_save():
  1. 计算新配置的CRC32
  2. 擦除配置区Flash
  3. 写入新配置
  4. 读回验证

// 配置区的读取操作都进行完整性验证
config_read():
  1. 读取配置数据
  2. 验证魔术字
  3. 验证CRC32
  4. 返回验证结果
```

### 2.3 应用分区 (A区/B区各20KB)

**职责**: 存储应用程序代码

#### 2.3.1 固件格式

```
+------------------------+ <-- APP_X_SECTOR_ADDR
|  固件头 (24 Bytes)      |
|  - 版本信息             |
|  - CRC32校验值          |
|  - 编译时间戳           |
+------------------------+ <-- APP_X_SECTOR_ADDR + 24
|  中断向量表             |
|  (0x00: 栈顶指针)       |
|  (0x04: Reset_Handler) |
+------------------------+
|  应用程序代码           |
|  ...                   |
+------------------------+
```

#### 2.3.2 固件打包流程

```python
# 使用 firmware_packer.py 工具

1. 读取原始 .bin 文件
2. 计算固件大小和 CRC32
3. 构建固件头（24字节）
4. 输出：[固件头] + [原始代码]

示例:
python firmware_packer.py app.bin 1.2.3 app_v1.2.3.bin

输出:
  版本: 1.2.3
  大小: 18432 字节
  CRC32: 0x12345678
  输出: app_v1.2.3.bin
```

### 2.4 日志区 (2KB)

**职责**: 记录升级历史和系统事件

#### 2.4.1 日志结构

```c
typedef struct {
    uint32_t timestamp;       // 时间戳
    uint8_t  event_type;      // 事件类型
    uint8_t  from_version[3]; // 源版本
    uint8_t  to_version[3];   // 目标版本
    uint8_t  result;          // 0=失败 1=成功
    uint8_t  error_code;      // 错误码
    uint8_t  reserved[2];     // 保留
} upgrade_log_t;  // 16字节

// 最多存储 2048 / 16 = 128 条日志
```

#### 2.4.2 事件类型

```c
#define LOG_EVENT_UPGRADE_START   0x01  // 升级开始
#define LOG_EVENT_UPGRADE_SUCCESS 0x02  // 升级成功
#define LOG_EVENT_UPGRADE_FAILED  0x03  // 升级失败
#define LOG_EVENT_ROLLBACK        0x04  // 分区回退
#define LOG_EVENT_BOOT_FAIL       0x05  // 启动失败
```

---

## 3. 固件升级流程

### 3.1 完整升级流程图

```
┌─────────────┐
  用户触发       按键 / 串口命令
└──────┬──────┘
       ↓
┌─────────────────────────────────────────────────┐
  步骤1: 确定目标分区                              
  target_bank = !active_bank                     
  设置 upgrade_status = DOWNLOADING               
└──────┬──────────────────────────────────────────┘
       ↓
┌─────────────────────────────────────────────────┐
  步骤2: Ymodem 接收固件                          
  - 发送 'C' 字符启动传输                           
  - 逐包接收数据（CRC16校验）                     
  - 写入目标分区 Flash                            
  - LED 指示传输进度                              
└──────┬──────────────────────────────────────────┘
       ↓
┌─────────────────────────────────────────────────┐
  步骤3: 固件验证                                 
  设置 upgrade_status = VERIFYING               
  - 解析固件头部（版本、大小、CRC32）              
  - 计算实际 CRC32                                
  - 比对两个 CRC32 值                             
└──────┬──────────────────────────────────────────┘
       │
       ├──[CRC错误]--> LED指示错误 -> 回退
       │
       ↓ [CRC正确]
┌─────────────────────────────────────────────────┐
  步骤4: 固件安装                                 
  设置 upgrade_status = INSTALLING               
  - 更新目标分区固件信息                          
  - 切换 active_bank = target_bank               
  - boot_count = 0                               
  - 保存配置区                                    
└──────┬──────────────────────────────────────────┘
       ↓
┌─────────────────────────────────────────────────┐
  步骤5: 完成升级                                 
  设置 upgrade_status = SUCCESS                  
  - LED 快速闪烁指示成功                          
  - 跳转到新固件运行                              
└─────────────────────────────────────────────────┘
```

### 3.2 核心代码实现

```c
void upgrade_process(void)
{
    uint8_t target_bank;
    firmware_info_t fw_info;
    uint32_t target_addr;
    uint32_t calculated_crc;

    // ========== 步骤1：确定目标分区 ==========
    target_bank = !g_config.active_bank;
    target_addr = (target_bank == 0) ? APP_A_SECTOR_ADDR : APP_B_SECTOR_ADDR;

    g_config.upgrade_status = UPGRADE_STATUS_DOWNLOADING;
    config_save(&g_config);

    led_fast_blink(6, 100);  // 进入升级模式指示

    // ========== 步骤2：通过Ymodem接收固件 ==========
    LED1_ON();
    ymodem_reset();
    g_ymodem_target_addr = target_addr;
    ymodem_c();

    // 等待传输完成
    while (g_ymodem_success == 0) {
    }

    // ========== 步骤3：验证固件 ==========
    g_config.upgrade_status = UPGRADE_STATUS_VERIFYING;
    config_save(&g_config);

    // 解析固件头部
    if (!firmware_parse_header(target_addr, &fw_info)) {
        LED1_OFF();
        led_status_indicate(5);  // 固件格式错误
        g_config.upgrade_status = UPGRADE_STATUS_FAILED;
        config_save(&g_config);
        return;
    }

    // 计算固件CRC32（跳过24字节头部）
    calculated_crc = crc32_calculate_flash(target_addr + 24, fw_info.firmware_size);

    // 验证CRC32
    if (calculated_crc != fw_info.firmware_crc32) {
        LED1_OFF();
        led_status_indicate(2);  // CRC错误
        g_config.upgrade_status = UPGRADE_STATUS_FAILED;
        config_save(&g_config);
        return;
    }

    // ========== 步骤4：固件更新 ==========
    g_config.upgrade_status = UPGRADE_STATUS_INSTALLING;

    // 更新目标分区的固件信息
    if (target_bank == 0) {
        g_config.bank_a_info = fw_info;
        g_config.bank_a_info.is_valid = FIRMWARE_VALID_FLAG;
    } else {
        g_config.bank_b_info = fw_info;
        g_config.bank_b_info.is_valid = FIRMWARE_VALID_FLAG;
    }

    // 切换激活分区
    g_config.active_bank = target_bank;
    g_config.boot_count = 0;
    g_config.upgrade_status = UPGRADE_STATUS_SUCCESS;
    config_save(&g_config);

    // ========== 步骤5：跳转到新固件 ==========
    LED1_OFF();
    led_fast_blink(10, 100);  // 升级成功指示

    jump_to_app(g_config.active_bank);
}
```

### 3.3 容错与回滚机制

#### 3.3.1 启动计数器回滚

```c
场景: 新固件有bug，启动后立即崩溃

第1次启动:
  boot_count = 1
  尝试启动新固件 → 崩溃

第2次启动:
  boot_count = 2
  尝试启动新固件 → 崩溃

第3次启动:
  boot_count = 3
  尝试启动新固件 → 崩溃

第4次启动:
  boot_count > max_boot_retry (3)
  自动切换到备份分区
  boot_count = 0
  系统恢复正常 ✓
```

#### 3.3.2 CRC校验回滚

```c
场景: 升级过程Flash写入错误

升级流程:
  1. Ymodem传输完成 ✓
  2. 所有数据包CRC16校验通过 ✓
  3. 固件CRC32校验失败 ✗
  4. 标记升级失败
  5. 保持原有激活分区不变
  6. 系统继续使用旧固件运行 ✓
```

#### 3.3.3 断电恢复

```c
场景: 升级过程中断电

Bootloader检测逻辑:
  if (upgrade_status == DOWNLOADING) {
      // 升级未完成
      // 选项1: 重新进入升级模式
      // 选项2: 使用原有固件启动
  }

策略:
  - 升级写入非激活分区，断电不影响当前固件
  - 下次启动继续使用旧版本
  - 提示用户重新升级
```

---

## 4. 开发与调试

### 4.1 文件结构

```
Boot/
├── BSP/
│   ├── LED/           # LED驱动
│   ├── KEY/           # 按键驱动
│   └── USART/         # 串口驱动
├── IAP/
│   ├── Bootloader/    # IAP跳转逻辑
│   ├── Config/        # 配置管理
│   └── Verify/        # 固件验证
├── Protocol/
│   └── YModem/        # Ymodem协议
└── Core/
    └── Src/
        └── main.c     # Bootloader主程序

Tools/
├── UpdateUI.py        # 上位机升级工具
└── firmware_packer.py # 固件打包工具
```

// 后续
---

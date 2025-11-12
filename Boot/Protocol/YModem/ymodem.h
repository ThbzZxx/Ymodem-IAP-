#ifndef __YMODEM_H
#define __YMODEM_H

#include "stm32f10x_conf.h"
#include <string.h>
#include "bsp_usart.h"

// YMODEM协议常量定义
#define YMODEM_SOH		0x01  // 开始128字节数据块
#define YMODEM_STX		0x02  // 开始1024字节数据块
#define YMODEM_EOT		0x04  // 传输结束
#define YMODEM_ACK		0x06  // 肯定应答
#define YMODEM_NAK		0x15  // 否定应答
#define YMODEM_CA		0x18  // 取消传输
#define YMODEM_C		0x43  // 控制字符'C'
#define YMODEM_END      0x4F  // 控制字符'O'关闭传输

// 队列相关定义
#define MAX_QUEUE_SIZE  1200

// 顺序循环队列的结构体定义
typedef struct
{
	uint8_t queue[MAX_QUEUE_SIZE];
	int rear;  //队尾指针
	int front;  //队头指针
	int count;  //计数器
} seq_queue_t;

// 下载缓冲区结构体
typedef struct
{
	uint8_t data[1200];
	uint16_t len;
} download_buf_t;

// ==== 新增：Ymodem接收结果结构体 ====
typedef struct
{
	uint8_t  success;         // 1=成功 0=失败
	uint32_t bytes_received;  // 接收的字节数
	uint32_t write_addr;      // 最终写入地址
} ymodem_result_t;

// 全局变量声明
extern seq_queue_t rx_queue;
extern download_buf_t recvBuf;
extern volatile uint8_t g_ymodem_success;
extern uint8_t type;
// ==== 新增：设置Ymodem写入的目标地址 ====
extern uint32_t g_ymodem_target_addr;
extern uint32_t g_ymodem_file_size;
// YMODEM协议相关函数
extern void ymodem_c(void);       // 发送'C'字符开始接收
extern uint8_t ymodem_c_ex(uint32_t target_addr, ymodem_result_t *result);  // 新增：增强版，返回结果
//extern uint32_t g_ymodem_file_size;
void ymodem_init(void);           // 初始化YMODEM协议
void ymodem_reset(void);          // 重置YMODEM接收状态

// 队列操作函数
void queue_initiate(seq_queue_t *Q);
int queue_not_empty(seq_queue_t *Q);
int queue_delete(seq_queue_t *Q, uint8_t *d);
int queue_append(seq_queue_t *Q, uint8_t x);

// YMODEM控制函数
void ymodem_ack(void);
void ymodem_nack(void);
void ymodem_end(void);

// 定时器初始化
void timer_init(void);

#endif


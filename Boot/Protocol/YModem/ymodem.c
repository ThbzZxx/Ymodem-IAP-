#include "ymodem.h"
#include "bootloader.h"
#include "bsp_led.h"
#include "sysTick.h"

// 全局变量定义
seq_queue_t rx_queue;
download_buf_t recvBuf;

// YMODEM状态和地址管理
uint8_t ymodem_status = 0;		  // YMODEM接收状态
static uint32_t ymodem_addr = 0;  // 当前写入Flash的地址
uint16_t ymodem_packet_count = 0; // ==== 新增：数据包计数 ====

// ==== 新增：目标地址和结果管理 ====
uint32_t g_ymodem_target_addr = APP_SECTOR_ADDR; // 默认写入地址（可被修改）
volatile uint8_t g_ymodem_success = 0;			 // 接收成功标志
uint32_t g_ymodem_byte_count = 0;				 // 接收字节计数
uint32_t g_ymodem_file_size = 0;				 // ==== 新增：文件总大小 ====

// 初始化队列
void queue_initiate(seq_queue_t *Q)
{
	Q->rear = 0;  // 队尾指针
	Q->front = 0; // 队头指针
	Q->count = 0; // 计数器
}

// 判断队列是否非空
int queue_not_empty(seq_queue_t *Q)
{
	if (Q->count != 0)
		return 1;
	else
		return 0;
}

// 入队列
int queue_append(seq_queue_t *Q, uint8_t x)
{
	if (Q->count > 0 && Q->rear == Q->front)
	{
		// 队列已满，直接返回失败
		return 0;
	}
	else
	{
		Q->queue[Q->rear] = x;
		// 循环队列取模运算确保指针不会越界
		Q->rear = (Q->rear + 1) % MAX_QUEUE_SIZE;
		Q->count++;
		return 1;
	}
}

// 出队列
int queue_delete(seq_queue_t *Q, uint8_t *d)
{
	if (Q->count == 0)
	{
		return 0;
	}
	else
	{
		*d = Q->queue[Q->front];
		Q->front = (Q->front + 1) % MAX_QUEUE_SIZE;
		Q->count--;
		return 1;
	}
}

// YMODEM协议响应函数
void ymodem_ack(void)
{
	uint8_t buf = YMODEM_ACK;
	Usart_Send_Data(&buf, 1);
}

void ymodem_nack(void)
{
	uint8_t buf = YMODEM_NAK;
	Usart_Send_Data(&buf, 1);
}

void ymodem_c(void)
{
	uint8_t buf = YMODEM_C;
	Usart_Send_Data(&buf, 1);
	// 移除printf提示
}

void ymodem_end(void)
{
	uint8_t buf = YMODEM_END;
	Usart_Send_Data(&buf, 1);
}
uint8_t type;
// YMODEM数据接收处理函数
static void ymodem_recv(download_buf_t *p)
{
	if (p->len == 0)
		return;

	type = p->data[0];

	switch (ymodem_status)
	{
	case 0: // 等待起始帧
		if (type == YMODEM_SOH)
		{
			ymodem_addr = g_ymodem_target_addr;
			g_ymodem_byte_count = 0; // 重置计数器
			ymodem_packet_count = 0; // 重置数据包计数

			// 解析文件大小（Ymodem第一个数据包包含文件名和大小）
			// 跳过文件名，找到文件大小
			uint8_t *size_str = &p->data[3];
			// 跳过文件名（找到第一个\0）
			while (*size_str != 0 && size_str < &p->data[3 + 128])
			{
				size_str++;
			}
			size_str++; // 跳过\0

			// 解析文件大小（ASCII十进制）
			g_ymodem_file_size = 0;
			while (*size_str >= '0' && *size_str <= '9')
			{
				g_ymodem_file_size = g_ymodem_file_size * 10 + (*size_str - '0');
				size_str++;
			}
			// 擦除应用程序区域（根据目标地址计算擦除扇区数）
			uint16_t erase_sectors;
			if (ymodem_addr == APP_A_SECTOR_ADDR)
			{
				erase_sectors = APP_A_ERASE_SECTORS; // 20KB
			}
			else if (ymodem_addr == APP_B_SECTOR_ADDR)
			{
				erase_sectors = APP_B_ERASE_SECTORS; // 20KB
			}
			else
			{
				erase_sectors = APP_ERASE_SECTORS; // 默认20KB
			}
			mcu_flash_erase(ymodem_addr, erase_sectors);

			ymodem_ack();
			ymodem_c();
			ymodem_status++;
		}
		break;

	case 1: // 接收数据帧
		if (type == YMODEM_SOH || type == YMODEM_STX)
		{
			uint16_t block_size = (type == YMODEM_SOH) ? 128 : 1024;

			// 计算实际需要写入的字节数
			uint32_t bytes_to_write = block_size;
			uint32_t remaining = g_ymodem_file_size - g_ymodem_byte_count;

			if (remaining < block_size)
			{
				bytes_to_write = remaining; // 最后一个数据块，只写入剩余字节
			}

			if (bytes_to_write > 0)
			{
				mcu_flash_write(ymodem_addr, &p->data[3], bytes_to_write);
				ymodem_addr += bytes_to_write;
				g_ymodem_byte_count += bytes_to_write;
			}

			// ==== 调试： ====
			//			ymodem_packet_count++;

			ymodem_ack();
		}
		else if (type == YMODEM_EOT) // 传输结束
		{
			ymodem_nack();
			ymodem_status++;
		}
		else
		{
			ymodem_status = 0;
			g_ymodem_success = 0; // 标记失败
		}
		break;

	case 2: // 确认传输结束
		if (type == YMODEM_EOT)
		{
			ymodem_ack();
			ymodem_c();
			g_ymodem_success = 1; // 标记成功

			ymodem_status++;
		}
		break;

	case 3: // 接收结束帧
		if (type == YMODEM_SOH)
		{
			ymodem_ack();
			ymodem_end();
			ymodem_status = 0;
			// 固件更新完成，LED闪烁指示
		}
		else
		{
			ymodem_status = 0;
		}
		break;
	}

	// 清空缓冲区
	p->len = 0;
}

// YMODEM初始化
void ymodem_init(void)
{
	USART_Config();
	timer_init();
	queue_initiate(&rx_queue);
	ymodem_status = 0;
}

// ==== 新增：重置Ymodem接收状态 ====
void ymodem_reset(void)
{
	ymodem_status = 0;
	g_ymodem_success = 0;
	g_ymodem_byte_count = 0;
	queue_initiate(&rx_queue); // 清空接收队列
}

// 定时器初始化
void timer_init(void)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE); // 时钟使能

	// 定时器TIM3初始化 (20ms定时 - 增加超时以适应115200波特率)
	// 115200bps下133字节需要约11.5ms，20ms超时足够安全
	TIM_TimeBaseStructure.TIM_Period = 1999;  // 自动重装载值 (改为20ms)
	TIM_TimeBaseStructure.TIM_Prescaler = 71; // 预分频值 72M/(71+1)=1MHz
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

	TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE); // 使能更新中断

	// 中断优先级设置
	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TIM3, ENABLE); // 使能定时器
}

// USART1中断处理函数
void USART1_IRQHandler(void)
{
	uint8_t res;

	if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
	{
		res = USART_ReceiveData(USART1);
		queue_append(&rx_queue, res);
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}

	// 重置定时器计数，用于检测数据包边界
	TIM3->CNT = 0;
	TIM_Cmd(TIM3, ENABLE);
}

// TIM3中断处理函数 - 用于处理接收完一帧数据的情况
void TIM3_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET)
	{
		int result = 1;
		TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
		TIM_Cmd(TIM3, DISABLE);

		// 处理队列中的数据
		if (queue_not_empty(&rx_queue))
		{
			recvBuf.len = 0;
			do
			{
				result = queue_delete(&rx_queue, &recvBuf.data[recvBuf.len]);
				if (result == 1)
				{
					recvBuf.len++;
				}
			} while (result);

			// 调用YMODEM接收处理函数
			ymodem_recv(&recvBuf);
		}
	}
}

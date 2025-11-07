#include "ymodem.h"
#include "sysTick.h"
#include "bootloader.h"
#include "bsp_led.h"   

// 全局变量定义
seq_queue_t rx_queue;
download_buf_t recvBuf;

// YMODEM状态和地址管理
static uint8_t ymodem_status = 0;  // YMODEM接收状态
static uint32_t ymodem_addr = 0;   // 当前写入Flash的地址

// 初始化队列
void queue_initiate(seq_queue_t *Q)
{
    Q->rear = 0;         //队尾指针 
    Q->front = 0;        //队头指针
    Q->count = 0;        //计数器
}

// 判断队列是否非空
int queue_not_empty(seq_queue_t *Q)
{
    if(Q->count != 0)
        return 1;
    else 
        return 0;
}

// 入队列
int queue_append(seq_queue_t *Q, uint8_t x)
{
    if(Q->count > 0 && Q->rear == Q->front)
    {    
        // 队列已满，直接返回失败
        return 0;
    }
    else
    {    
        Q->queue[Q->rear] = x;
        //循环队列取模运算确保指针不会越界
        Q->rear = (Q->rear + 1) % MAX_QUEUE_SIZE;
        Q->count++;
        return 1;
    }
}

// 出队列
int queue_delete(seq_queue_t *Q, uint8_t *d)
{
    if(Q->count == 0)
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

// YMODEM数据接收处理函数
static void ymodem_recv(download_buf_t *p) 
{
    if (p->len == 0) return;
    
    uint8_t type = p->data[0];
    
    switch (ymodem_status) 
    {
        case 0: // 等待起始帧
            if (type == YMODEM_SOH) 
            {
                // 初始化地址并擦除应用程序区域
                ymodem_addr = APP_SECTOR_ADDR;
                mcu_flash_erase(ymodem_addr, APP_ERASE_SECTORS);
                ymodem_ack();
                ymodem_c();
                ymodem_status++;
            }
            break;
        
        case 1: // 接收数据帧
            if (type == YMODEM_SOH || type == YMODEM_STX) 
            {
                if (type == YMODEM_SOH) // 128字节数据块
                {
                    mcu_flash_write(ymodem_addr, &p->data[3], 128);
                    ymodem_addr += 128;
                }
                else // 1024字节数据块
                {
                    mcu_flash_write(ymodem_addr, &p->data[3], 1024);
                    ymodem_addr += 1024;
                }
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
            }
            break;
        
        case 2: // 确认传输结束
            if (type == YMODEM_EOT) 
            {
                ymodem_ack();
                ymodem_c();
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
                uint8_t i;
                for(i = 0; i < 5; i++)
                {
                    LED1_ON();
                    delay_ms(200);
                    LED1_OFF();
                    delay_ms(200);
                }
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

// 定时器初始化
void timer_init(void) 
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE); //时钟使能
    
    //定时器TIM3初始化 (10ms定时)
    TIM_TimeBaseStructure.TIM_Period = 999; //自动重装载值
    TIM_TimeBaseStructure.TIM_Prescaler = 71; //预分频值 72M/(71+1)=1MHz
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
 
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE); //使能更新中断

    //中断优先级设置
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM3, ENABLE);  //使能定时器
}

// USART1中断处理函数
void USART1_IRQHandler(void)
{
    uint8_t res;
    
    if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
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
    if(TIM_GetITStatus(TIM3, TIM_IT_Update) == SET) 
    {
        int result = 1;
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        TIM_Cmd(TIM3, DISABLE);
        
        // 处理队列中的数据
        if(queue_not_empty(&rx_queue))
        {
            recvBuf.len = 0;
            do
            {
                result = queue_delete(&rx_queue, &recvBuf.data[recvBuf.len]);
                if(result == 1)
                {
                    recvBuf.len++;
                }
            }
            while(result);
            
            // 调用YMODEM接收处理函数
            ymodem_recv(&recvBuf);
        }
    }
}

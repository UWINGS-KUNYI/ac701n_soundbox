#include "app_config.h"
#include "uart_1t2_serial_send.h"

#if TCFG_UART_1T2_EN && (UART_1T2_ROLE == UART_1T2_ROLE_MASTER)
#include "os/os_api.h"
#include "asm/uart_dev.h"

/* #define UART_1T2_DEBUG_EN */

#ifdef UART_1T2_DEBUG_EN
#define uart_1t2_putchar(x)                putchar(x)
#define uart_1t2_printf                    printf
#define uart_1t2_put_buf(x, len)           put_buf(x, len)
#else
#define uart_1t2_putchar(...)
#define uart_1t2_printf(...)
#define uart_1t2_printf_buf(...)
#endif

#define UART_1T2_SERIAL_SEND_TASK_NAME "uart_1t2_s"
static OS_MUTEX	 _mutex;
static OS_SEM _serial_send_task_run_sem;
static OS_SEM _serial_send_tx_sem;

static uart_bus_t *_udev;			// 串口控制器
struct list_head _serial_send_list;		// 发送链表

#define SERIAL_SEND_RETRY_COUNT 					0 // 命令重发次数
#define SERIAL_SEND_LIST_MAX_COUNT 					10 // 发送buf链表最大buf包数量

static int _send_list_count = 0;

struct serial_send_list_send_buf {
    struct list_head entry;
    u8 *send_buf;
    u16 send_len;
};

/**
 * @brief 清除消息分发的链表内容
 */
void uart_1t2_send_list_flush()
{
    uart_1t2_printf("-----------%s------------\n", __FUNCTION__);
    os_mutex_pend(&_mutex, 0);
    struct serial_send_list_send_buf *send_buf_hd, *next;
    list_for_each_entry_safe(send_buf_hd, next, &_serial_send_list, entry) {
        free(send_buf_hd->send_buf);
        list_del(&send_buf_hd->entry);
        free(send_buf_hd);
    }
    _send_list_count = 0;
    os_mutex_post(&_mutex);
}


/**
 * @brief 添加发送消息到分布式发送链表
 */
Uart1t2SendResult uart_1t2_send_buf_list_add(char *buf, u16 len)
{
    if (_send_list_count >= SERIAL_SEND_LIST_MAX_COUNT) {
        /* printf("%s uart 1t2 send buf list full! Need check slave no response's reason!\n", __FUNCTION__); */
        // 需要检查从机为什么没有回复命令
        return Uart1t2SendResultFailMaxCount;
    }
    struct serial_send_list_send_buf *send_buf_hd = malloc(sizeof(struct serial_send_list_send_buf));
    u8 *send_buf = malloc(len);
    if ((!send_buf_hd) || (!send_buf)) {
        printf("%s malloc err!\n", __FUNCTION__);
        return Uart1t2SendResultMallocFail;
    }
    send_buf_hd->send_buf = send_buf;
    send_buf_hd->send_len = len;
    memcpy(send_buf, buf, len);
    os_mutex_pend(&_mutex, 0);
    list_add_tail(&send_buf_hd->entry, &_serial_send_list);
    _send_list_count++;
    os_mutex_post(&_mutex);
    os_sem_post(&_serial_send_task_run_sem);
    return Uart1t2SendResultSuccess;
}

/**
 * @brief 从机收到消息
 */
void uart_1t2_recieve_msg_from_slave()
{
    uart_1t2_printf("-----------%s------------\n", __FUNCTION__);
    os_sem_post(&_serial_send_tx_sem);
}

void uart_1t2_serial_send_task(void *p)
{
    uart_1t2_printf("create %s task success!\n", uart_1t2_SERIAL_SEND_TASK_NAME);
    INIT_LIST_HEAD(&_serial_send_list);

    os_mutex_create(&_mutex);
    os_sem_create(&_serial_send_task_run_sem, 0);
    os_sem_create(&_serial_send_tx_sem, 0);
    u8 send_retry_count = 0;

    while (1) {
        os_sem_pend(&_serial_send_task_run_sem, 0);

        // 如果链表有数据，则一直发送
        while (!list_empty(&_serial_send_list)) {
            // 取出链表的第一个发送buf
            os_mutex_pend(&_mutex, 0);
            struct serial_send_list_send_buf *send_buf_hd = NULL;
            send_buf_hd = list_first_entry(&_serial_send_list, struct serial_send_list_send_buf, entry);
            u8 *send_buf = send_buf_hd->send_buf;
            u16 send_len = send_buf_hd->send_len;
            list_del(&send_buf_hd->entry);
            free(send_buf_hd);
            _send_list_count--;
            // 取出数据到发送缓存后，删除
            os_mutex_post(&_mutex);

            while (send_buf != NULL) {
                if (_udev) {
                    /* printf("uart tx-------------\n"); */
                    /* put_buf(send_buf, send_len); */
                    _udev->write(send_buf, send_len);
                } else {
                    ASSERT(_udev, "serial send tx err, udev is null!\n");
                }

                u32 err = os_sem_pend(&_serial_send_tx_sem, 200);
                if (OS_NO_ERR != err) {
                    // 正常来说不应该会超时，要看看从机为什么回复那么慢或者不回复
                    printf("[Warning]uart 1t2 serial_send_tx_sem wait rx timeout! Need check slave no response's reason!\n");
                }
                if ((OS_NO_ERR != err) && (send_retry_count < SERIAL_SEND_RETRY_COUNT)) {
                    send_retry_count++;
                    continue;
                }
                // 发送成功或重发一定次数, 则删除
                if ((OS_NO_ERR == err) || (send_retry_count >= SERIAL_SEND_RETRY_COUNT)) {
                    send_retry_count = 0;
                    free(send_buf);
                    send_buf = NULL;
                }
            }
        }
    }
}

/**
 * @brief 初始化单io串行发数
 */
void uart_1t2_serial_send_init(uart_bus_t *udev)
{
    ASSERT(udev, "uart 1t2 serial send init udev err!\n");
    _udev = udev;
    task_create(uart_1t2_serial_send_task, NULL, UART_1T2_SERIAL_SEND_TASK_NAME);
}

#endif


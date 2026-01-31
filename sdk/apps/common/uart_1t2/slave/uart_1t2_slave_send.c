#include "uart_1t2_slave_send.h"
#include "app_config.h"
#include "uart_1t2.h"
#include "os/os_api.h"
#include "uart_1t2_define.h"

#if TCFG_UART_1T2_EN && (UART_1T2_ROLE != UART_1T2_ROLE_MASTER)

struct list_head _single_io_send_buf_list;		// 发送链表

struct single_io_send_buf {
    struct list_head entry;
    u8 *send_buf;
    u16 send_len;
};

static OS_MUTEX _mutex;
struct list_head _single_io_send_buf_list;		// 发送链表

void uart_1t2_slave_send_init()
{
    os_mutex_create(&_mutex);

    INIT_LIST_HEAD(&_single_io_send_buf_list);
}

static int uart_1t2_single_io_send_buf_list_add(u8 *buf, u16 len)
{
    struct single_io_send_buf *send_buf_hd = malloc(sizeof(struct single_io_send_buf));
    u8 *send_buf = malloc(len);
    if ((!send_buf_hd) || (!send_buf)) {
        printf("%s malloc err!\n", __FUNCTION__);
        return -1;
    }
    send_buf_hd->send_buf = send_buf;
    send_buf_hd->send_len = len;
    memcpy(send_buf, buf, len);
    os_mutex_pend(&_mutex, 0);
    list_add_tail(&send_buf_hd->entry, &_single_io_send_buf_list);
    os_mutex_post(&_mutex);
    return 0;
}

/**
 * @brief uart 1t2从机消息发送
 */
void uart_1t2_slave_msg_send(u8 *buf, u16 len)
{
    uart_1t2_single_io_send_buf_list_add(buf, len);
}

#define UART_1T2_SINGLE_IO_SEND_BUF_MAX_SIZE 230
static void uart_1t2_check_single_io_send(u8 *buf, u32 buf_len)
{
    u8 send_buf[UART_1T2_SINGLE_IO_SEND_BUF_MAX_SIZE];
    send_buf[0] = UART_1T2_SINGLE_IO_SEND_ID;
    send_buf[1] = 0x00;
    UART_1T2_WRITE_LIT_U32(&send_buf[0] + 2, UART_1T2_SUB_OP_CHECK_SINGLE_IO_SEND_BUF);//cmd
    memcpy(send_buf + 6, buf, (buf_len > UART_1T2_SINGLE_IO_SEND_BUF_MAX_SIZE) ? UART_1T2_SINGLE_IO_SEND_BUF_MAX_SIZE : buf_len);
    all_assemble_package_send_for_uart_1t2(UART_1T2_ROLE_MASTER, send_buf, 6 + buf_len);
}

static void uart_1t2_check_single_io_send_no_data()
{
    u8 send_buf[6];
    send_buf[0] = UART_1T2_SINGLE_IO_SEND_ID;
    send_buf[1] = 0x00;
    UART_1T2_WRITE_LIT_U32(&send_buf[0] + 2, UART_1T2_SUB_OP_CHECK_SINGLE_IO_SEND_BUF_NO_DATA);//cmd
    all_assemble_package_send_for_uart_1t2(UART_1T2_ROLE_MASTER, send_buf, sizeof(send_buf));
}

static void uart_1t2_user_data_response()
{
    u8 send_buf[6];
    send_buf[0] = UART_1T2_SINGLE_IO_SEND_ID;
    send_buf[1] = 0x00;
    UART_1T2_WRITE_LIT_U32(&send_buf[0] + 2, UART_1T2_SUB_OP_USER_DATA);//cmd
    all_assemble_package_send_for_uart_1t2(UART_1T2_ROLE_MASTER, send_buf, sizeof(send_buf));
}

static void uart_1t2_callback(u8 msg_from, u8 *_packet, u32 size)
{
    /* printf("slave msg rec:\n"); */
    /* put_buf(_packet, size); */
    u8 *ptr = _packet;
    u8 id = ptr[0];
    u8 sq = ptr[1];
    u32 cmd = UART_1T2_READ_LIT_U32(ptr + 2);
    /* printf("cmd:%d", cmd); */
    switch (cmd) {
    case UART_1T2_SUB_OP_CHECK_SINGLE_IO_SEND_BUF:
        /* printf("msg_from:%d, ============UART_1T2_SUB_OP_CHECK_SINGLE_IO_SEND_BUF==========\n", msg_from); */
        // 获取链表的buf，并回复
        os_mutex_pend(&_mutex, 0);
        if (!list_empty(&_single_io_send_buf_list)) {
            struct single_io_send_buf *send_buf_hd = NULL;
            send_buf_hd = list_first_entry(&_single_io_send_buf_list, struct single_io_send_buf, entry);
            u8 *send_buf = send_buf_hd->send_buf;
            u16 send_len = send_buf_hd->send_len;
            list_del(&send_buf_hd->entry);
            free(send_buf_hd);
            uart_1t2_check_single_io_send(send_buf, send_len);
            // 取出数据到发送缓存后，删除
            free(send_buf);
            send_buf = NULL;
        } else {
            uart_1t2_check_single_io_send_no_data();
        }
        os_mutex_post(&_mutex);

        /* u8 test_buf[5] = {0x11, 0x22, 0x33, 0x44, 0x55}; */
        /* uart_1t2_slave_msg_send(test_buf, sizeof(test_buf)); */
        break;
    case UART_1T2_SUB_OP_USER_DATA:
        printf("msg_from:%d, ===========UART_1T2_SUB_OP_USER_DATA==========\n", msg_from);
        // 打印对端发过来的buf
        put_buf(ptr + 6, size - 6);
        uart_1t2_user_data_response();
        break;
    default:
        break;
    }
}

REGISTER_UART_1T2_DETECT_TARGET(uart_1t2_slave_send_target) = {
    .id = UART_1T2_SINGLE_IO_SEND_ID,
    .uart_1t2_msg_deal = uart_1t2_callback,
};

#endif


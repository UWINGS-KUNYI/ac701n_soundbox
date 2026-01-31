#include "share_osc_uart_single_io_send.h"
#include "app_config.h"
#include "share_osc_uart.h"
#include "system/timer.h"
#include "os/os_api.h"

#if TCFG_SHARE_OSC_EN

#define SHARE_OSC_SINGLE_IO_SEND_ID 0X32

#define SHARE_OSC_SUB_OP_CHECK_SINGLE_IO_SEND_BUF				0x00000045	//查询单io命令
#define SHARE_OSC_SUB_OP_CHECK_SINGLE_IO_SEND_BUF_NO_DATA		0x00000046	//查询单io但无数据命令
#define SHARE_OSC_SUB_OP_CHECK_SINGLE_IO_USER_DATA				0x00000047	//用户可通过这个命令发送自定义数据

static u16 _check_timer_id = 0;

#define SHARE_OSC_SINGLE_IO_SEND_BUF_MAX_SIZE 230
static void share_osc_user_data_send(u8 *buf, u32 buf_len)
{
    u8 send_buf[SHARE_OSC_SINGLE_IO_SEND_BUF_MAX_SIZE];
    send_buf[0] = SHARE_OSC_SINGLE_IO_SEND_ID;
    send_buf[1] = 0x00;
    SHARE_OSC_WRITE_LIT_U32(&send_buf[0] + 2, SHARE_OSC_SUB_OP_CHECK_SINGLE_IO_USER_DATA);//cmd
    memcpy(send_buf + 6, buf, (buf_len > SHARE_OSC_SINGLE_IO_SEND_BUF_MAX_SIZE) ? SHARE_OSC_SINGLE_IO_SEND_BUF_MAX_SIZE : buf_len);
    all_assemble_package_send_for_share_osc(send_buf, 6 + buf_len);
}

/**
 * @brief 共享晶振消息发送
 */
void share_osc_msg_send(u8 *buf, u16 len)
{
    share_osc_user_data_send(buf, len);
}

static void share_osc_check_single_io_send()
{
    u8 send_buf[6];
    send_buf[0] = SHARE_OSC_SINGLE_IO_SEND_ID;
    send_buf[1] = 0x00;
    SHARE_OSC_WRITE_LIT_U32(&send_buf[0] + 2, SHARE_OSC_SUB_OP_CHECK_SINGLE_IO_SEND_BUF);//cmd
    all_assemble_package_send_for_share_osc(send_buf, sizeof(send_buf));

    /* u8 test_buf[5] = {0x11, 0x22, 0x33, 0x44, 0x55}; */
    /* share_osc_msg_send(test_buf, sizeof(test_buf)); */
}

void share_osc_check_single_io_send_init()
{
    if (!_check_timer_id) {
        _check_timer_id = sys_timer_add(NULL, share_osc_check_single_io_send, 100);
    }
}

void share_osc_check_single_io_send_exit()
{
    if (_check_timer_id) {
        sys_timer_del(_check_timer_id);
        _check_timer_id = 0;
    }
}

static void share_osc_callback(u8 *_packet, u32 size)
{
    /* printf("master msg rec:\n"); */
    /* put_buf(_packet, size); */
    u8 *ptr = _packet;
    u8 id = ptr[0];
    u8 sq = ptr[1];
    u32 cmd = SHARE_OSC_READ_LIT_U32(ptr + 2);
    switch (cmd) {
    case SHARE_OSC_SUB_OP_CHECK_SINGLE_IO_SEND_BUF:
        printf("============SHARE_OSC_SUB_OP_CHECK_SINGLE_IO_SEND_BUF==========\n");
        // 打印对端发过来的buf
        put_buf(ptr + 6, size - 6);
        break;
    case SHARE_OSC_SUB_OP_CHECK_SINGLE_IO_SEND_BUF_NO_DATA:
        /* printf("============SHARE_OSC_SUB_OP_CHECK_SINGLE_IO_SEND_BUF_NO_DATA==========\n"); */
        // 对端无数据
        break;
    default:
        break;
    }
}

REGISTER_SHARE_OSC_DETECT_TARGET(share_osc_single_io_send_target) = {
    .id = SHARE_OSC_SINGLE_IO_SEND_ID,
    .share_osc_message_deal = share_osc_callback,
};

#endif


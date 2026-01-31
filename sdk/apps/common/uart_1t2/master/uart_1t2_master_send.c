#include "uart_1t2_master_send.h"
#include "app_config.h"
#include "uart_1t2.h"
#include "system/timer.h"
#include "os/os_api.h"
#include "uart_1t2_define.h"
#include "uart_1t2_serial_send.h"

#if TCFG_UART_1T2_EN && (UART_1T2_ROLE == UART_1T2_ROLE_MASTER)

static u16 _check_timer_id = 0; // 主机查询从机的数据
static u8 __power_off_retry_count = 0;
static u8 uart_1t2_power_off_flag[2] = {0};
static OS_SEM uart_1t2_sem;
static u8 power_off_role = UART_1T2_ROLE_SLAVE1;
static bool is_uart_1t2_power_off = false;

#define UART_1T2_SINGLE_IO_SEND_BUF_MAX_SIZE 230
static void uart_1t2_user_data_send(u8 role, u8 *buf, u32 buf_len)
{
    u8 send_buf[UART_1T2_SINGLE_IO_SEND_BUF_MAX_SIZE];
    send_buf[0] = UART_1T2_SINGLE_IO_SEND_ID;
    send_buf[1] = 0x00;
    UART_1T2_WRITE_LIT_U32(&send_buf[0] + 2, UART_1T2_SUB_OP_USER_DATA);//cmd
    memcpy(send_buf + 6, buf, (buf_len > UART_1T2_SINGLE_IO_SEND_BUF_MAX_SIZE) ? UART_1T2_SINGLE_IO_SEND_BUF_MAX_SIZE : buf_len);
    all_assemble_package_send_for_uart_1t2(role, send_buf, 6 + buf_len);
}

/**
 * @brief uart 1t2主机消息发送
 */
void uart_1t2_master_msg_send(u8 role, u8 *buf, u16 len)
{
    uart_1t2_user_data_send(role, buf, len);
}

static u8 last_check_role = UART_1T2_ROLE_SLAVE1;
static void uart_1t2_master_send_check()
{
    u8 send_buf[6];
    send_buf[0] = UART_1T2_SINGLE_IO_SEND_ID;
    send_buf[1] = 0x00;
    UART_1T2_WRITE_LIT_U32(&send_buf[0] + 2, UART_1T2_SUB_OP_CHECK_SINGLE_IO_SEND_BUF);//cmd
    // 定时器分别查询slave1和slave2
#if (UART_1T2_SLAVE_NUM == 2)
    last_check_role = (last_check_role == UART_1T2_ROLE_SLAVE2) ? UART_1T2_ROLE_SLAVE1 : UART_1T2_ROLE_SLAVE2;
#endif
    all_assemble_package_send_for_uart_1t2(last_check_role, send_buf, sizeof(send_buf));

    /* u8 test_buf[5] = {0x11, 0x22, 0x33, 0x44, 0x55}; */
    /* uart_1t2_master_msg_send(1, test_buf, sizeof(test_buf)); */
}

void uart_1t2_master_send_init()
{
    if (!_check_timer_id) {
        _check_timer_id = sys_timer_add(NULL, uart_1t2_master_send_check, 100);
    }
}

void uart_1t2_master_send_exit()
{
    if (_check_timer_id) {
        sys_timer_del(_check_timer_id);
        _check_timer_id = 0;
    }
}

void uart_1t2_command_init()
{
    is_uart_1t2_power_off = true;
    os_sem_create(&uart_1t2_sem, 0);
}
void uart_1t2_power_off()
{
    u8 send_buf[UART_1T2_SINGLE_IO_SEND_BUF_MAX_SIZE];
    send_buf[0] = UART_1T2_SINGLE_IO_SEND_ID;
    send_buf[1] = 0x00;
    UART_1T2_WRITE_LIT_U32(&send_buf[0] + 2, UART_1T2_SUB_OP_POWER_OFF);//cmd
    power_off_role = (power_off_role == UART_1T2_ROLE_SLAVE2) ? UART_1T2_ROLE_SLAVE1 : UART_1T2_ROLE_SLAVE2;
    all_assemble_package_send_for_uart_1t2(power_off_role, send_buf, sizeof(send_buf));
}

void uart_1t2_soft_poweroff_enter()
{
    if (is_uart_1t2_power_off) {
        uart_1t2_master_send_exit(); // 关闭单io定时器
        uart_1t2_send_list_flush(); // 清除发送列表的待发送消息，如果br30跑不过来，可能发送列表有很多查询30是否要发送消息
        while (__power_off_retry_count <= 3) {
            uart_1t2_power_off();
            u32 err = os_sem_pend(&uart_1t2_sem, 250);
            if (err != OS_NO_ERR) {
                printf("%s fail, err:%d, retry count:%d\n", __FUNCTION__, err, __power_off_retry_count);
                __power_off_retry_count++;
                if (__power_off_retry_count > 3) {
                    // 超过一定重发次数了，自己关机
                    printf("!!!no receive RX data,write RESET flag");
                    os_time_dly(1);
                    break;
                }
            } else {
                // 接收成功
                if (uart_1t2_power_off_flag[0] && uart_1t2_power_off_flag[1]) {
                    printf("all slave power off\n");
                    os_time_dly(50);
                    break;
                }
            }
        }
    }
}

static void uart_1t2_callback(u8 msg_from, u8 *_packet, u32 size)
{
    /* printf("master msg rec:\n"); */
    /* put_buf(_packet, size); */
    u8 *ptr = _packet;
    u8 id = ptr[0];
    u8 sq = ptr[1];
    u32 cmd = UART_1T2_READ_LIT_U32(ptr + 2);
    /* printf("cmd:%d", cmd); */
    switch (cmd) {
    case UART_1T2_SUB_OP_CHECK_SINGLE_IO_SEND_BUF:
        printf("msg_from:%d, ============UART_1T2_SUB_OP_CHECK_SINGLE_IO_SEND_BUF==========\n", msg_from);
        // 打印对端发过来的buf
        put_buf(ptr + 6, size - 6);
        break;
    case UART_1T2_SUB_OP_CHECK_SINGLE_IO_SEND_BUF_NO_DATA:
        /* printf("msg_from:%d, ============UART_1T2_SUB_OP_CHECK_SINGLE_IO_SEND_BUF_NO_DATA==========\n", msg_from); */
        // 对端无数据
        break;
    case UART_1T2_SUB_OP_POWER_OFF:
        printf("msg_from:%d, ============UART_1T2_SUB_OP_POWER_OFF==========\n", msg_from);
        os_sem_post(&uart_1t2_sem);
        if (msg_from == UART_1T2_ROLE_SLAVE1) {
            uart_1t2_power_off_flag[0] = 1;
        } else if (msg_from == UART_1T2_ROLE_SLAVE2) {
            uart_1t2_power_off_flag[1] = 1;
        }
        break;
    default:
        break;
    }
}

REGISTER_UART_1T2_DETECT_TARGET(uart_1t2_master_send_target) = {
    .id = UART_1T2_SINGLE_IO_SEND_ID,
    .uart_1t2_msg_deal = uart_1t2_callback,
};

static void uart_1t2_port_keep_low_timer()
{
    gpio_set_pull_down(TCFG_UART_1T2_PORT, 0);
    gpio_set_pull_up(TCFG_UART_1T2_PORT, 0);
    gpio_set_direction(TCFG_UART_1T2_PORT, 1);
    gpio_set_die(TCFG_UART_1T2_PORT, 0);
    uart_1t2_init();
}

void uart_1t2_pull_down_io()
{
    gpio_set_pull_down(TCFG_UART_1T2_PORT, 0);
    gpio_set_pull_up(TCFG_UART_1T2_PORT, 0);
    gpio_set_direction(TCFG_UART_1T2_PORT, 0);
    gpio_set_output_value(TCFG_UART_1T2_PORT, 0);
    sys_hi_timeout_add(NULL, uart_1t2_port_keep_low_timer, 200);
}

#endif


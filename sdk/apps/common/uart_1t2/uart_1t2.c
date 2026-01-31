#include "uart_1t2.h"
#include "app_config.h"
#include "uart_1t2_serial_send.h"
#include "uart_1t2_master_send.h"
#include "uart_1t2_slave_send.h"

#if TCFG_UART_1T2_EN

#define	UART_1T2_TASK_NAME		"uart_1t2"

#define UART_RX_SIZE        0x100
#define UART_DB_SIZE        0x100
#define UART_BAUD_RATE      115200
#define TEMP_BUF_SIZE		256

u8 uart_cbuf[UART_DB_SIZE] __attribute__((aligned(4)));
u8 uart_rxbuf[UART_RX_SIZE] __attribute__((aligned(4)));

typedef struct {
    u16 preamble0;
    u8  preamble1;
    u8	msg_to;
    u8	msg_from;
    u16 crc16;
    u8  length;
    u8 	type;
    u8  sq;
    u8 	payload[0];
} _GNU_PACKED_	uart_packet_t;

#define UART_TOOL_FORMAT_HEAD  sizeof(uart_packet_t)

#define UART_NEW_TOOL_PREAMBLE0      0xAA5A
#define UART_NEW_TOOL_PREAMBLE1      0xA5

uart_bus_t *uart_bus;
volatile u8 flag_uart_write_busy = 0;
static u16 check_io_timer_id = 0;
static u16 data_length = 0;
static u8 local_packet[TEMP_BUF_SIZE];

#ifdef ALIGN
#undef ALIGN
#endif

#define ALIGN(a, b) \
	({ \
	 int m = (u32)(a) & ((b)-1); \
	 int ret = (u32)(a) + (m?((b)-m):0);	 \
	 ret;\
	 })

void uart_1t2_get_data(void)
{
    u16 crc16;
    uart_packet_t *p_newtool;

    data_length += uart_bus->read(&uart_rxbuf[data_length], (UART_RX_SIZE - data_length), 10);//串口读取buf剩余空间的长度，实际长度比buf长，导致越界改写问题

    /* printf("uart_1t2 rx : %d", data_length); */
    /* put_buf(uart_rxbuf, data_length); */
    if (data_length > UART_RX_SIZE) {
        printf("Wired");
    }

    u8 *tmp_buf = NULL;
    tmp_buf = uart_rxbuf;
    if (data_length >= 2) {
        unsigned i = 0;
        for (i = 0; i < data_length - 1; ++i) {
            if ((tmp_buf[i] == 0x5A) && (tmp_buf[i + 1] == 0xAA) && (tmp_buf[i + 2] == 0xA5)) {
                break;
            }
        }
        if (i != 0) {
            data_length -= i;
            /* printf("data_length %d i %d\n", data_length, i); */
            if (data_length > 0) {
                memmove(&uart_rxbuf[0], &tmp_buf[i], data_length);
            }
        }
    }

    if (data_length <= UART_TOOL_FORMAT_HEAD) {
        return;
    }

    p_newtool = (uart_packet_t *)uart_rxbuf;

    if (p_newtool->msg_to != UART_1T2_ROLE) {
        /* printf("current role dont handle, msg_to:%d\n", p_newtool->msg_to); */
        goto reset_buf;
    }

    if ((p_newtool->preamble0 != UART_NEW_TOOL_PREAMBLE0) || (p_newtool->preamble1 != UART_NEW_TOOL_PREAMBLE1)) {
        printf("preamble err\n");
        put_buf(uart_rxbuf, data_length);
        goto reset_buf;
    }

    if (data_length >= p_newtool->length + 8) {
        crc16 = CRC16(&p_newtool->length, p_newtool->length + 1);
        /* printf("CRC16 0x%x / 0x%x", crc16, p_newtool->crc16); */
        if (p_newtool->crc16 != crc16) {
            printf("crc16 err\n");
            put_buf(uart_rxbuf, data_length);
            goto reset_buf;
        }

#if (UART_1T2_ROLE == UART_1T2_ROLE_MASTER)
        uart_1t2_recieve_msg_from_slave();
#endif

        /* printf("uart rx-------------\n"); */
        /* put_buf(p_newtool, p_newtool->length + 8); */

        u8 *command_data = uart_rxbuf + 8;
        u8 *buf_temp = (u8 *)malloc(256);
        buf_temp = (u8 *)ALIGN(buf_temp, 4);
        memset(buf_temp, 0, 256);
        memcpy(buf_temp + 1, command_data, p_newtool->length);
        /* printf("uart distributed-------------\n"); */
        /* put_buf(buf_temp + 1, p_newtool->length - 1); */

        /*数据进行分发处理*/
        const struct uart_1t2_interface *p;
        list_for_each_uart_1t2_interface(p) {
            if (p->id == p_newtool->type) {
                p->uart_1t2_msg_deal(p_newtool->msg_from, buf_temp + 1, p_newtool->length);
                if (buf_temp) {
                    free(buf_temp);
                    buf_temp = NULL;
                }
                break;
            }
        }
        if (buf_temp) {
            free(buf_temp);
        }
    } else {
        return;
    }

reset_buf:
    data_length = 0;
}

static void uart_isr_hook(void *arg, u32 status)
{
    if (status == UT_TX) {
        return;
    }
    int argv[2];
    argv[0] = (int)uart_1t2_get_data;      // Function
    argv[1] = 0;                            // 参数个数
    int ret = os_taskq_post_type(UART_1T2_TASK_NAME, Q_CALLBACK, sizeof(argv) / sizeof(int), argv);
    if (ret != OS_NO_ERR) {
        printf("%s post taskq err:%d!", __FUNCTION__, ret);
    }
}

static int write_callback_handle(int msg1, int msg2)
{
    u8 *send_buf = (u8 *)msg1;
    u16 len = (u16)msg2;
    if (uart_bus) {
        flag_uart_write_busy = 1;
        uart_bus->write(send_buf, len);
        flag_uart_write_busy = 0;
    } else {
        putchar('E');
    }
    free(send_buf);
    return 0;
}

/**
 * @brief 串口发送数据
 */
void uart_1t2_send_data(u8 *buf, u32 len)
{
    // 发送到指定任务中发送uart数据
    u8 *send_buf = zalloc(len);
    if (send_buf == NULL) {
        printf("%s malloc err!\n", __FUNCTION__);
        return;
    }
    memcpy(send_buf, buf, len);

    int argv[4];
    argv[0] = (int)write_callback_handle;      // Function
    argv[1] = 2;                            // 参数个数
    argv[2] = (int)send_buf;                     // 参数1，可以是任意类型强转成int
    argv[3] = (int)len;                     // 参数2，可以是任意类型强转成int
    int ret = os_taskq_post_type(UART_1T2_TASK_NAME, Q_CALLBACK, sizeof(argv) / sizeof(int), argv);
    if (ret != OS_NO_ERR) {
        free(send_buf);
    }
}

/**
 * @brief 发送uart 1t2协议内容
 *
 * @pragma msg_to 接收角色
 * @pragma buf 数据buf
 * @pragma len 数据长度
 */
void all_assemble_package_send_for_uart_1t2(u8 msg_to, u8 *buf, u32 len)
{
    u8 send_buf[TEMP_BUF_SIZE];
    u16 crc16_data;

    send_buf[0] = 0x5A;
    send_buf[1] = 0xAA;
    send_buf[2] = 0xA5;
    send_buf[3] = msg_to;
    send_buf[4] = UART_1T2_ROLE;
    send_buf[7] = len;
    memcpy(send_buf + 8, buf, len);
    crc16_data = CRC16(&send_buf[7], len + 1);
    send_buf[5] = crc16_data & 0xff;
    send_buf[6] = (crc16_data >> 8) & 0xff;

#if (UART_1T2_ROLE == UART_1T2_ROLE_MASTER)
    uart_1t2_send_buf_list_add(send_buf, len + 8);
#else
    if (uart_bus) {
        /* printf("uart_1t2 slave tx-------------\n"); */
        /* put_buf(send_buf, len + 8); */
        uart_bus->write(send_buf, len + 8);
    }
#endif
}

static void uart_1t2_task(void *p)
{
    printf("create %s task\n", __FUNCTION__);
#if (UART_1T2_ROLE == UART_1T2_ROLE_MASTER)
    uart_1t2_master_send_init();
#else
    uart_1t2_slave_send_init();
#endif
    int msg[8];
    while (1) {
        if (os_taskq_pend(NULL, msg, ARRAY_SIZE(msg)) != OS_TASKQ) {
            continue;
        }
    }
}

#define IO_CHECK_CNT	200
static void check_uart_io_level()
{
    /* static u16 check_cnt = 0; */
    /* if (!flag_uart_write_busy && gpio_read_direction(TCFG_uart_1t2_PORT)) { */
    /*     if (gpio_read(TCFG_uart_1t2_PORT) == 0) { */
    /*         putchar('-'); */
    /*         if (check_cnt < IO_CHECK_CNT) { */
    /*             check_cnt++; */
    /*             if (check_cnt == IO_CHECK_CNT) { */
    /*                 printf("%s always low level, DSP already shut down, enter poweroff!\n", __FUNCTION__); */
    /* 				// TODO */
    /*             } */
    /*         } */
    /*     } else { */
    /*         check_cnt = 0; */
    /*         return ; */
    /*     } */
    /* } */
}

/**
 * @brief uart 1t2串口关闭
 */
void uart_1t2_close(void)
{
    printf("----------%s--------------\n", __FUNCTION__);
    if (uart_bus) {
        uart_dev_close(uart_bus);
    }
    if (check_io_timer_id) {
        sys_hi_timer_del(check_io_timer_id);
        check_io_timer_id = 0;
    }
    gpio_set_direction(TCFG_UART_1T2_PORT, 1);
    gpio_set_die(TCFG_UART_1T2_PORT, 1);
    gpio_set_pull_up(TCFG_UART_1T2_PORT, 0);
    gpio_set_pull_down(TCFG_UART_1T2_PORT, 0);
#if (UART_1T2_ROLE == UART_1T2_ROLE_MASTER)
    uart_1t2_master_send_exit();
#endif
}

/**
 * @brief uart 1t2串口初始化
 */
void uart_1t2_init(void)
{
    struct uart_platform_data_t ut = {0};
    ut.tx_pin = TCFG_UART_1T2_PORT;
    ut.rx_pin = TCFG_UART_1T2_PORT;
    ut.baud = UART_BAUD_RATE;
    ut.rx_timeout = 1;
    ut.isr_cbfun = uart_isr_hook;
    ut.rx_cbuf = uart_cbuf;
    ut.rx_cbuf_size = UART_DB_SIZE;
    ut.frame_length = UART_DB_SIZE;
    uart_bus = (uart_bus_t *)uart_dev_open(&ut);
    if (uart_bus == NULL) {
        printf("%s UART OPEN ERR!!!", __FUNCTION__);
        return;
    }
    //没有发数据的时候，判断uart io是否低电平，DSP是否已关机，是的话跟着一起关机
    if (!check_io_timer_id) {
        /* check_io_timer_id = sys_hi_timer_add(NULL, check_uart_io_level, 5); */
    }
#if (UART_1T2_ROLE == UART_1T2_ROLE_MASTER)
    uart_1t2_serial_send_init(uart_bus);
    uart_1t2_command_init();
#endif

    task_create(uart_1t2_task, NULL, UART_1T2_TASK_NAME);
}

#endif

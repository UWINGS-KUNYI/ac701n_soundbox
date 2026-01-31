#include "share_osc_uart_command.h"
#include "app_config.h"
#include "asm/power_interface.h"
#include "system/timer.h"
#include "os/os_api.h"
#include "asm/power/p33.h"
#include "share_osc_uart.h"
#include "share_osc_uart_serial_send.h"
#include "share_osc_uart_single_io_send.h"

#if TCFG_SHARE_OSC_EN

#define SHARE_OSC_ID 									0X31
#define RESET_CODE										0x55
#define SHARE_OSC_POWER_OFF_RETRY_COUNT					1
static u16 _power_off_retry_count = 0;

static bool is_share_osc_power_off = false;
static OS_SEM _share_osc_sem;

void share_osc_vm_write_reset_flag(void)
{
    printf("%s", __func__);
    u8 reset_code = RESET_CODE;
    //ota升级成功，要写vm，下次音箱开机要复位一下wlm_rx
    int vm_ret = syscfg_write(SHARE_OSC_RESET_VM_ID, &reset_code, sizeof(reset_code));
    if (vm_ret == sizeof(reset_code)) {
        //printf("write_vm_ok");
    } else {
        printf("write_vm_fail!!!");
    }
}

static u8 share_osc_vm_read_reset_flag(void)
{
    u8 reset_code = 0;
    int vm_ret = 0;
    printf("%s", __func__);
    if (sizeof(reset_code) == syscfg_read(SHARE_OSC_RESET_VM_ID, &reset_code, sizeof(reset_code))) {
        if (reset_code == RESET_CODE) {
            //reset vm
            reset_code = 0;
            vm_ret = syscfg_write(SHARE_OSC_RESET_VM_ID, &reset_code, sizeof(reset_code));
            if (vm_ret == sizeof(reset_code)) {
                //printf("write_vm_ok");
            } else {
                printf("write_vm_fail!!!");
            }
            printf("read reset code success");
            return 1;
        }
    }
    printf("read reset code fail!!!");
    return 0;
}
/**
 * @brief share_osc命令相关初始化
 */
void share_osc_command_init()
{
    is_share_osc_power_off = true;
    os_sem_create(&_share_osc_sem, 0);
}

void share_osc_power_off()
{
    u8 send_buf[6];
    send_buf[0] = SHARE_OSC_ID;
    send_buf[1] = 0x00;
    SHARE_OSC_WRITE_LIT_U32(&send_buf[0] + 2, SHARE_OSC_SUB_OP_POWER_OFF);//cmd
    all_assemble_package_send_for_share_osc(send_buf, sizeof(send_buf));
}

/**
 * @brief share_osc命令执行共晶振关机流程
 */
void share_osc_soft_poweroff_enter_post()
{
    if (is_share_osc_power_off) {
        share_osc_check_single_io_send_exit(); // 关闭单io定时器
        share_osc_uart_send_list_flush(); // 清除发送列表的待发送消息，如果br30跑不过来，可能发送列表有很多查询30是否要发送消息
        while (_power_off_retry_count <= SHARE_OSC_POWER_OFF_RETRY_COUNT) {
            share_osc_power_off();
            u32 err = os_sem_pend(&_share_osc_sem, 250);
            if (err != OS_NO_ERR) {
                printf("%s fail, err:%d, retry count:%d\n", __FUNCTION__, err, _power_off_retry_count);
                _power_off_retry_count++;
                if (_power_off_retry_count > SHARE_OSC_POWER_OFF_RETRY_COUNT) {
                    // 超过一定重发次数了，自己关机
                    printf("!!!no receive RX data,write RESET flag");
                    share_osc_vm_write_reset_flag();
                    os_time_dly(1);
                    break;
                }
            } else {
                // 接收成功
                os_time_dly(50);
                break;
            }
        }
    }
}

static void share_osc_callback(u8 *_packet, u32 size)
{
    /* printf("master msg rec power_off:\n"); */
    /* put_buf(_packet, size); */
    u8 *ptr = _packet;
    u8 id = ptr[0];
    u8 sq = ptr[1];
    u32 cmd = SHARE_OSC_READ_LIT_U32(ptr + 2);
    switch (cmd) {
    case SHARE_OSC_SUB_OP_POWER_OFF:
        //收到从机回复，从机已进入关机流程
        printf("=============SHARE_OSC_SUB_OP_POWER_OFF==========\n");
        os_sem_post(&_share_osc_sem);
        break;
    default:
        break;
    }

}

REGISTER_SHARE_OSC_DETECT_TARGET(share_osc_target) = {
    .id = SHARE_OSC_ID,
    .share_osc_message_deal = share_osc_callback,
};

static u8 share_osc_power_reset_src;
static void share_osc_uart_port_keep_low_timer()
{
    gpio_set_pull_down(TCFG_SHARE_OSC_UART_PORT, 0);
    gpio_set_pull_up(TCFG_SHARE_OSC_UART_PORT, 0);
    gpio_set_direction(TCFG_SHARE_OSC_UART_PORT, 1);
    gpio_set_die(TCFG_SHARE_OSC_UART_PORT, 0);
    share_osc_uart_init();
}



void share_osc_pull_down_io()
{
    u16 delay_time = 0;
    u8 flag_wkp = 0;
    if ((is_reset_source(MSYS_P2M_RST) && is_reset_source(P11_POWER_RETURN) && is_reset_source(P33_POWER_RETURN)) || \
        (is_reset_source(MSYS_P11_RST) && is_reset_source(P11_IVS_RST) && is_reset_source(P33_POWER_RETURN))) {
        flag_wkp = 1;
    }
    if (!flag_wkp || (share_osc_vm_read_reset_flag())) {
        //复位起来的/ota升级成功的,拉低uart port 2s,让rx PPINR
        delay_time = 2000;
        if (is_reset_source(P33_VDDIO_POR_RST)) {
            //第一次或者硬开关
            printf("first poweron");
            delay_time = 50;
        }
    } else {
        //软关机唤醒起来的,拉低uart port 50ms,让rx正常唤醒
        printf("wake up");
        delay_time = 50;
    }
    printf("delay_time = %d", delay_time);
    gpio_set_pull_down(TCFG_SHARE_OSC_UART_PORT, 0);
    gpio_set_pull_up(TCFG_SHARE_OSC_UART_PORT, 0);
    gpio_set_direction(TCFG_SHARE_OSC_UART_PORT, 0);
    gpio_set_output_value(TCFG_SHARE_OSC_UART_PORT, 0);
    sys_hi_timeout_add(NULL, share_osc_uart_port_keep_low_timer, delay_time);
}

#endif


#include "system/app_core.h"
#include "system/includes.h"
#include "app_config.h"
#include "asm/usb.h"
#include "usb/otg.h"
#include "usb/host/usb_host.h"
#include "usb/host/hid.h"
#include "task_usb_host.h"

#define LOG_TAG_CONST       USB
#define LOG_TAG             "[task_host]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

#if TCFG_HID_HOST_ENABLE

#define USB_HOST_TASK_NAME "usb_host_task"

static void device_insertion(const char *dev_name)
{
    usb_dev usb_id = dev_name[strlen(dev_name) - 1] - '0';
    log_info("device_insertion:%s, usb_id:%d\n", dev_name, usb_id);
    if (strncmp(dev_name, "hid", 3) == 0) {
        hid_process(usb_id);
    } else {
        /* xxx_process(usb_id); //其他类型interface解析 */
    }
}

static void usb_host_event_handler(struct sys_event *event, void *priv)
{
    const char *usb_msg;
    usb_dev usb_id;

    switch ((u32)event->arg) {
    case DEVICE_EVENT_FROM_OTG:
        usb_msg = (const char *)event->u.dev.value;
        usb_id = usb_msg[2] - '0';
        log_debug("usb_host_event_handler: %x DEVICE_EVENT_FROM_OTG %s", event->u.dev.event, usb_msg);
        if (usb_msg[0] == 'h') { //usb_host
            if (event->u.dev.event == DEVICE_EVENT_IN) {
                log_info("usb %c DEVICE_EVENT_IN", usb_msg[2]);
                if (usb_host_mount(usb_id, 3, 30, 300)) {
                    usb_h_force_reset(usb_id);
                    usb_otg_suspend(usb_id, OTG_UNINSTALL);
                    usb_otg_resume(usb_id);
                }
            } else if (event->u.dev.event == DEVICE_EVENT_OUT) {
                log_info("usb %c DEVICE_EVENT_OUT", usb_msg[2]);
                usb_host_unmount(usb_id);
            }
        } else if (usb_msg[0] == 's') { //usb_slave
        }
        break;
    case DEVICE_EVENT_FROM_USB_HOST:
        log_debug("usb_host_event_handler: %x DEVICE_EVENT_FROM_USB_HOST", event->u.dev.event);
        if ((event->u.dev.event == DEVICE_EVENT_IN) || (event->u.dev.event == DEVICE_EVENT_CHANGE)) {
            int err = os_taskq_post_msg(USB_HOST_TASK_NAME, 2, DEVICE_EVENT_IN, event->u.dev.value);
            if (err) {
                r_printf("err %x\n", err);
                log_error("func:%s(), line:%d\n", __func__, __LINE__);
            }
        } else if (event->u.dev.event == DEVICE_EVENT_OUT) {
            int err = os_taskq_post_msg(USB_HOST_TASK_NAME, 2, DEVICE_EVENT_OUT, event->u.dev.value);
            if (err) {
                r_printf("err %x\n", err);
                log_error("func:%s(), line:%d\n", __func__, __LINE__);
            }
        }
        break;
    default:
        break;
    }
    return;
}

static void usb_host_task(void *arg)
{
    log_info("func:%s()\n", __func__);
    int ret = 0;
    int msg[16];
    while (1) {
        ret = os_taskq_pend("taskq", msg, ARRAY_SIZE(msg));
        if (ret != OS_TASKQ) {
            continue;
        }
        if (msg[0] != Q_MSG) {
            continue;
        }
        switch (msg[1]) {
        case DEVICE_EVENT_IN:
            log_info("fucn:%s(), DEVICE_EVENT_IN\n", __func__);
            device_insertion((const char *)msg[2]);
            break;
        case DEVICE_EVENT_OUT:
            log_info("func:%s(), DEVICE_EVENT_OUT\n", __func__);
            break;
        }

    }
    return;
}

void usb_host_stack_init()
{
    log_info("func:%s()\n", __func__);
    int err;
    err = register_sys_event_handler(SYS_DEVICE_EVENT, 0, 2, (void (*)(struct sys_event *))usb_host_event_handler);
    if (err) {
        log_error("func:%s(), line:%d\n", __func__, __LINE__);
    }

    err = task_create(usb_host_task, NULL, USB_HOST_TASK_NAME);
    if (err) {
        log_error("func:%s(), line:%d\n", __func__, __LINE__);
    }
}

void usb_host_stack_exit()
{
    log_info("func:%s()\n", __func__);
    unregister_sys_event_handler((void (*)(struct sys_event *))usb_host_event_handler);
    task_delete(USB_HOST_TASK_NAME);
}

#endif

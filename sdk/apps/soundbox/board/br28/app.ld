#include "app_config.h"

SECTIONS
{
    .text : ALIGN(4)
    {
        gsensor_dev_begin = .;
        KEEP(*(.gsensor_dev))
        gsensor_dev_end = .;

        fm_dev_begin = .;
        KEEP(*(.fm_dev))
        fm_dev_end = .;

        fm_emitter_dev_begin = .;
        KEEP(*(.fm_emitter_dev))
        fm_emitter_dev_end = .;

        storage_device_begin = .;
        KEEP(*(.storage_device))
        storage_device_end = .;

        wireless_dev_ops_begin = .;
        KEEP(*(.wireless_dev))
        wireless_dev_ops_end = .;

        connected_sync_channel_begin = .;
        KEEP(*(.connected_call_sync))
        connected_sync_channel_end = .;

        conn_sync_call_func_begin = .;
        KEEP(*(.connected_sync_call_func))
        conn_sync_call_func_end = .;

        conn_data_trans_stub_begin = .;
        KEEP(*(.conn_data_trans_stub))
        conn_data_trans_stub_end = .;

#if (!TCFG_CODE_RUN_RAM_FM_MODE)
 	    *(.usr_timer_const)
		*(.usr_timer_code)
    	*(.timer_const)
		*(.timer_code)
    	*(.cbuf_const)
		*(.cbuf_code)
		*(.fm_data_code)
		*(.fm_data_const)
		. = ALIGN(4);
#endif

#if  (!TCFG_LED7_RUN_RAM)
		. = ALIGN(4);
        *(.gpio_ram)
		. = ALIGN(4);
        *(.LED_code)
		. = ALIGN(4);
        *(.LED_const)
#endif

		. = ALIGN(4);
        *(.usb.text)
        *(.usb.text.const)

		. = ALIGN(4);
        broadcast_text_begin = . ;
        *(.broadcast_text)
        *(.broadcast_const)
        *(.app_broadcast_text)
        *(.app_broadcast_const)
        broadcast_text_end = . ;

		. = ALIGN(32);
    } > code0

	.data ALIGN(32):
	{
		. = ALIGN(4);
        *(.usb.data)

        . = ALIGN(4);
        broadcast_data_begin = . ;
        *(.broadcast_data)
        *(.app_broadcast_data)
        broadcast_data_end = . ;

        . = ALIGN(4);
	} > ram0

	.bss ALIGN(32):
    {
        *(.usb.data.bss.host_mem)  //由于usb有个bug，会导致dma写的数据超出预设的buf，最长可能写超1k，为了避免死机，所以usb dma buffer后面放一些其他模块的buff来避免死机
        *(.usb.data.bss)
#if (!USB_MEM_USE_OVERLAY)
        *(.usb.data.bss.exchange)
#endif

        . = ALIGN(4);
        broadcast_bss_begin = . ;
        *(.broadcast_bss)
        *(.app_broadcast_bss)
        broadcast_bss_end = . ;

        . = ALIGN(32);
    } > ram0

	.data_code ALIGN(32):
	{
#if (TCFG_CODE_RUN_RAM_FM_MODE)
 	    *(.usr_timer_const)
		*(.usr_timer_code)
    	*(.timer_const)
		*(.timer_code)
    	*(.cbuf_const)
		*(.cbuf_code)
		*(.fm_data_code)
		*(.fm_data_const)
		. = ALIGN(4);
#endif

#if  (TCFG_LED7_RUN_RAM)
		. = ALIGN(4);
        *(.gpio_ram)
		. = ALIGN(4);
        *(.LED_code)
		. = ALIGN(4);
        *(.LED_const)
#endif
		. = ALIGN(4);
	} > ram0

}

broadcast_text_size = broadcast_text_end - broadcast_text_begin;
broadcast_bss_size = broadcast_bss_end - broadcast_bss_begin;
broadcast_data_size = broadcast_data_end - broadcast_data_begin;
app_broadcast_total_size = broadcast_text_size + broadcast_data_size + broadcast_bss_size;




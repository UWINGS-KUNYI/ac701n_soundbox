#include "app_config.h"
#include "includes.h"
#include "typedef.h"
#include "clock_cfg.h"

#ifdef CONFIG_CPU_BR23
#include "asm/pap.h"
#include "generic/gpio.h"
#include "asm/clock.h"
#include "asm/cpu.h"

#undef LOG_TAG_CONST
#define LOG_TAG     "[lcd1602a]"
#define LOG_ERROR_ENABLE
#define LOG_INFO_ENABLE
#include "debug.h"

#ifndef CONFIG_CPU_BR28

#define LCD1602_RS_IO        IO_PORTA_08
#define LCD1602_RW_IO        IO_PORTA_12//PAP_WR: IO_PORTA_12
#define LCD1602_E_IO         IO_PORTA_11//PAP_RD: IO_PORTA_11 //高有效
#define LCD1602_E_PA_NUM     11//PAP_RD: IO_PORTA_11 //高有效
/* JL_IOMAP->CON0 &=~ BIT(14);//PAP_Dx */
/* JL_IOMAP->CON0 &=~ BIT(13);//PAP_RD */
/* JL_IOMAP->CON0 &=~ BIT(12);//PAP_WR */

extern void delay_2ms(int cnt);//fm
/********************lcd1602 pap driver***********************/
u8 lcd1602_pap_tx_byte(u16 byte)
{
    pap_port_dir_set(0);//pap d0~d7 输入输出状态
    pap_dir_out(JL_PAP);
    pap_buf_reg(JL_PAP) = byte;
    /* gpio_write(LCD1602_E_IO,1); */
    JL_PORTA->OUT |= BIT(LCD1602_E_PA_NUM);
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    /* gpio_write(LCD1602_E_IO,0); */
    JL_PORTA->OUT &= ~BIT(LCD1602_E_PA_NUM);
    /* putchar('a'); */
    while (!pap_pnd(JL_PAP));
    pap_pnd_clr(JL_PAP);
    /* putchar('b'); */
    return 1;//ok
}

u16 lcd1602_pap_rx_byte()
{
    pap_data_ext_disable(JL_PAP);
    pap_port_dir_set(1);//pap d0~d7 输入输出状态
    pap_dir_in(JL_PAP);
    pap_buf_reg(JL_PAP) = 0xff;
    /* gpio_write(LCD1602_E_IO,1); */
    JL_PORTA->OUT |= BIT(LCD1602_E_PA_NUM);
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    /* putchar('c'); */
    while (!pap_pnd(JL_PAP));
    pap_pnd_clr(JL_PAP);
    /* gpio_write(LCD1602_E_IO,0); */
    JL_PORTA->OUT &= ~BIT(LCD1602_E_PA_NUM);
    /* putchar('d'); */
    return pap_buf_reg(JL_PAP);
}

u8 lcd1602_check_busy()
{
    u8 read_data = 0;
    gpio_write(LCD1602_RS_IO, 0);
    read_data = lcd1602_pap_rx_byte();
    /* log_info("read lcd:0x%x",read_data); */
    return (read_data & 0x80);
}
void lcd1602_write_com(u8 com, u8 check_busy)
{
    if (check_busy)while (lcd1602_check_busy());
    /* log_info("write com"); */
    gpio_write(LCD1602_RS_IO, 0);
    lcd1602_pap_tx_byte(com);
}
void lcd1602_write_data(u8 data)
{
    while (lcd1602_check_busy());
    /* log_info("write dat"); */
    gpio_write(LCD1602_RS_IO, 1);
    lcd1602_pap_tx_byte(data);
}

void lcd1602_pap_init()
{
    hw_pap_init(0);
    JL_IOMAP->CON0 &= ~ BIT(13); //PAP_RD: IO_PORTA_11 不占用
    gpio_set_die(LCD1602_E_IO, 1);
    gpio_set_direction(LCD1602_E_IO, 0);
    gpio_write(LCD1602_E_IO, 0);
    gpio_set_die(LCD1602_RS_IO, 1);
    gpio_set_direction(LCD1602_RS_IO, 0);
    gpio_write(LCD1602_RS_IO, 0);
}

void lcd1602_init(void)
{
    delay_2ms(6);//上电等待lcd稳定  //10ms
    lcd1602_pap_init();
    lcd1602_write_com(0x38, 0); 	//三次模式设置，不检测忙信号
    delay_2ms(3);  //5ms
    lcd1602_write_com(0x38, 0);
    delay_2ms(3);  //5ms
    lcd1602_write_com(0x38, 0);
    delay_2ms(3);  //5ms

    lcd1602_write_com(0x38, 1); 	//显示模式设置,开始要求每次检测忙信号
    lcd1602_write_com(0x08, 1); 	//关闭显示
    lcd1602_write_com(0x01, 1); 	//显示清屏
    lcd1602_write_com(0x06, 1); 	//显示光标移动设置
    lcd1602_write_com(0x0C, 1); 	//显示开及光标设置
}
/***********按指定位置显示字符********************/
void lcd1602_display_char(u8 x, u8 y, u8 *data, u8 len)
{
    y &= 0x1;
    x &= 0xF; 			//限制x不能大于15，y不能大于1
    if (y) {
        x |= 0x40;    //当要显示第二行时地址码+0x40;
    }
    x |= 0x80; 			//算出指令码
    lcd1602_write_com(x, 1); //这里不检测忙信号，发送地址码
    while (len--) {
        delay(700);//延时350us
        lcd1602_write_data(*data++);
    }
}

/********************lcd1602 soft driver***********************/
//软件与硬件共用同一组IO
void lcd1602_soft_io_init()
{
    JL_IOMAP->CON0 &= ~ BIT(14); //PAP_Dx
    JL_IOMAP->CON0 &= ~ BIT(12); //PAP_WR
    JL_IOMAP->CON0 &= ~ BIT(13); //PAP_RD: IO_PORTA_11 不占用
    gpio_set_die(LCD1602_E_IO, 1);
    gpio_set_direction(LCD1602_E_IO, 0);
    gpio_write(LCD1602_E_IO, 0);
    gpio_set_die(LCD1602_RS_IO, 1);
    gpio_set_direction(LCD1602_RS_IO, 0);
    gpio_write(LCD1602_RS_IO, 0);
    gpio_set_die(LCD1602_RW_IO, 1);
    gpio_set_direction(LCD1602_RW_IO, 0);
    gpio_write(LCD1602_RW_IO, 0);
    gpio_set_die(IO_PORTA_09, 1);
    gpio_set_die(IO_PORTA_10, 1);
    gpio_set_die(IO_PORTC_00, 1);
    gpio_set_die(IO_PORTC_01, 1);
    gpio_set_die(IO_PORTC_02, 1);
    gpio_set_die(IO_PORTC_03, 1);
    gpio_set_die(IO_PORTC_04, 1);
    gpio_set_die(IO_PORTC_05, 1);
    gpio_set_pull_down(IO_PORTA_09, 1);
    gpio_set_pull_down(IO_PORTA_10, 1);
    gpio_set_pull_down(IO_PORTC_00, 1);
    gpio_set_pull_down(IO_PORTC_01, 1);
    gpio_set_pull_down(IO_PORTC_02, 1);
    gpio_set_pull_down(IO_PORTC_03, 1);
    gpio_set_pull_down(IO_PORTC_04, 1);
    gpio_set_pull_down(IO_PORTC_05, 1);
    pap_port_dir_set(0);//pap d0~d7 输入输出状态
}
u8 lcd1602_soft_check_busy()
{
    /* P0=0xff; */
    pap_port_dir_set(0);//pap d0~d7 输入输出状态
    JL_PORTA->OUT |= (BIT(9) | BIT(10));
    JL_PORTC->OUT |= 0x3f;
    gpio_write(LCD1602_RS_IO, 0);
    gpio_write(LCD1602_RW_IO, 1);
    gpio_write(LCD1602_E_IO, 0);
    asm("nop");
    gpio_write(LCD1602_E_IO, 1);
    pap_port_dir_set(1);//pap d0~d7 输入输出状态
    return gpio_read(IO_PORTC_05);//busy bit(bit7 / d7)
}
void lcd1602_soft_write_com(u8 com, u8 check_busy)
{
    if (check_busy)while (lcd1602_soft_check_busy());
    pap_port_dir_set(0);//pap d0~d7 输入输出状态
    gpio_write(LCD1602_RS_IO, 0);
    gpio_write(LCD1602_RW_IO, 0);
    gpio_write(LCD1602_E_IO, 1);
    SFR(JL_PORTA->OUT, 9, 2, (com & 0x03));
    SFR(JL_PORTC->OUT, 0, 6, (com >> 2));
    delay(10);
    gpio_write(LCD1602_E_IO, 0);
    /* log_info("write com"); */
}
void lcd1602_soft_write_data(u8 data)
{
    while (lcd1602_soft_check_busy());
    pap_port_dir_set(0);//pap d0~d7 输入输出状态
    gpio_write(LCD1602_RS_IO, 1);
    gpio_write(LCD1602_RW_IO, 0);
    gpio_write(LCD1602_E_IO, 1);
    SFR(JL_PORTA->OUT, 9, 2, (data & 0x03));
    SFR(JL_PORTC->OUT, 0, 6, (data >> 2));
    delay(10);
    gpio_write(LCD1602_E_IO, 0);
    /* log_info("write dat"); */
}

void lcd1602_soft_init(void)
{
    delay_2ms(6);//上电等待lcd稳定  //10ms
    lcd1602_soft_io_init();
    lcd1602_soft_write_com(0x38, 0); 	//三次模式设置，不检测忙信号
    delay_2ms(3);  //5ms
    lcd1602_soft_write_com(0x38, 0);
    delay_2ms(3);  //5ms
    lcd1602_soft_write_com(0x38, 0);
    delay_2ms(3);  //5ms

    lcd1602_soft_write_com(0x38, 1); 	//显示模式设置,开始要求每次检测忙信号
    lcd1602_soft_write_com(0x08, 1); 	//关闭显示
    lcd1602_soft_write_com(0x01, 1); 	//显示清屏
    lcd1602_soft_write_com(0x06, 1); 	//显示光标移动设置
    lcd1602_soft_write_com(0x0C, 1); 	//显示开及光标设置
}

/***********按指定位置显示字符********************/
void lcd1602_soft_display_char(u8 x, u8 y, u8 *data, u8 len)
{
    y &= 0x1;
    x &= 0xF; 			//限制x不能大于15，y不能大于1
    if (y) {
        x |= 0x40;    //当要显示第二行时地址码+0x40;
    }
    x |= 0x80; 			//算出指令码
    lcd1602_soft_write_com(x, 1); //这里不检测忙信号，发送地址码
    while (len--) {
        delay(700);//延时350us
        lcd1602_soft_write_data(*data++);
    }
}

/**************************test***************************/
//pap使用lsb时钟，时钟改变时需测试TS/TW/TH是否满足时序
//延时需保证符合注释
#if 0
void lcd1602_test()
{
#if 1//硬件
    char data[] = "0123456789-pap--";
    /* char data[]="01"; */
    lcd1602_init();
    lcd1602_display_char(0, 0, data, sizeof(data) - 1);
    lcd1602_display_char(0, 1, data, sizeof(data) - 1);
    /* lcd1602_write_com(0x80,1); // */
    /* lcd1602_write_data('a');//1 */
#else//软件
//软件与硬件共用同一组IO
    char data[] = "0123456789-soft-";
    lcd1602_soft_init();
    lcd1602_soft_display_char(0, 0, data, sizeof(data) - 1);
    lcd1602_soft_display_char(0, 1, data, sizeof(data) - 1);
    /* lcd1602_soft_write_com(0x80,1); // */
    /* lcd1602_soft_write_data('1');//0 */
#endif
}
#endif
#endif //#ifndef CONFIG_CPU_BR28
#endif //#ifndef CONFIG_CPU_BR28


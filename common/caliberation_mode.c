#include <config.h>
#include <common.h>
#include <linux/types.h>
#include <asm/arch/bits.h>
#include <linux/string.h>
#include <android_bootimg.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <nand.h>
#include <android_boot.h>
#include <environment.h>
#include <jffs2/jffs2.h>
#include <boot_mode.h>

extern int dwc_otg_driver_init(void);
extern void dwc_otg_driver_cleanup(void);
extern int usb_fastboot_initialize(void);
extern int usb_serial_init(void);
extern void  usb_serial_cleanup(void);
extern int usb_serial_configed;
extern int usb_read_done;
extern int usb_write_done;
extern int usb_trans_status;
extern int usb_gadget_handle_interrupts(void);

extern int gs_open(void);
extern int gs_close(void);
extern int gs_read(const unsigned char *buf, int *count);
extern int gs_write(const unsigned char *buf, int count);
extern void usb_wait_trans_done(int direct);
extern void udc_power_on(void);
extern void udc_power_off(void);

//#define DEBUG
#ifdef DEBUG
#define dprintf(fmt, args...) printf(fmt, ##args)
#else
#define dprintf(fmt, args...) 
#endif

#ifdef DEBUG
#define IO_DEBUG 1     // open write and read debug
#define WRITE_DEBUG 1  // 0: for read debug, 1 for write debug
#else
#define IO_DEBUG 0
#endif


#define CALIBERATE_STRING_LEN 10
#define CALIBERATE_HEAD 0x7e
#define CALIBERATE_COMMOND_T 0xfe

unsigned int caliberate_mode = 0; 

int check_caliberate(char * buf, int len)
{
	unsigned int command = 0;
	if(len != CALIBERATE_STRING_LEN)
		return 0;

	if((*buf == CALIBERATE_HEAD) && (*(buf + len -1) == CALIBERATE_HEAD)){
		if((*(buf+7)==CALIBERATE_COMMOND_T) && (*(buf+len-2) != 0x1)){
			command = *(buf+len-2);
			command &= 0xf;
		}
	}
	return command;
}
void caliberation_mode(void)
{
	int ret;
	int i ;
	loff_t off = 0;
    printf("%s\n", __func__);

    extern lcd_display(void);
    extern void set_backlight(uint32_t value);
    lcd_printf("   caliberation mode");
    lcd_display();
    set_backlight(50);

#ifdef CONFIG_MODEM_CALIBERATE
	char buf[20];
	for(i = 0; i<20; i++)
		buf[i] = i+'a';
	dwc_otg_driver_init();
	usb_serial_init();
	while(!usb_serial_configed)
		usb_gadget_handle_interrupts();
	printf("USB SERIAL CONFIGED\n");
	gs_open();
#if IO_DEBUG 
#if WRITE_DEBUG
	while(1){
		ret = gs_write(buf, 20);
		printf("func: %s waitting write done\n", __func__);
		if(usb_trans_status)
			printf("func: %s line %d usb trans with error %d\n", __func__, __LINE__, usb_trans_status);
		usb_wait_trans_done(1);
		printf("func: %s readly send %d\n", __func__, ret);
    }		
#else
	while(1){
		int count = 20;
		usb_wait_trans_done(0);	
		if(usb_trans_status)
					printf("func: %s line %d usb trans with error %d\n", __func__, __LINE__, usb_trans_status);
		ret = gs_read(buf, &count);
		printf("func: %s readly read %d\n", __func__, count);		
		if(usb_trans_status)
			printf("func: %s line %d usb trans with error %d\n", __func__, __LINE__, usb_trans_status);
		for(i = 0; i<count; i++)
			printf("%c ", buf[i]);
		printf("\n");
	}	

#endif
#endif
//code for caliberate detect
	int got = 0;
	int count = CALIBERATE_STRING_LEN;
	dprintf("start to calberate\n");
	
	while(got < CALIBERATE_STRING_LEN){
		usb_wait_trans_done(0);		
		if(usb_trans_status)
			printf("func: %s line %d usb trans with error %d\n", __func__, __LINE__, usb_trans_status);
		ret = gs_read(buf + got, &count);
		if(usb_trans_status)
			printf("func: %s line %d usb trans with error %d\n", __func__, __LINE__, usb_trans_status);
        for(i=0; i<count; i++)
          dprintf("0x%x \n", buf[got+i]);
        dprintf("\n");
		got+=count;
		if(got<CALIBERATE_STRING_LEN){
			count=CALIBERATE_STRING_LEN-got;
			continue;
		}else{
			break;
		}
	}

	printf("caliberate:what got from host total %d is \n", got);
	for(i=0; i<got;i++)
		printf("0x%x ", buf[i]);
	printf("\n");
	ret = check_caliberate(buf, CALIBERATE_STRING_LEN);
	dprintf("check_caliberate return is 0x%x\n", ret);
	if(!ret){
        printf("func: %s line: %d caliberate failed\n", __func__, __LINE__);
		return;
    }
	else{
        caliberate_mode = ret;
		ret = gs_write(buf, got);
		dprintf("func: %s waitting %d write done\n", __func__, got);
		if(usb_trans_status)
			printf("func: %s line %d usb trans with error %d\n", __func__, __LINE__, usb_trans_status);
		usb_wait_trans_done(1);
		udc_power_off();
        vlx_mode();
	}	
    
    //nerver come to here
    while(1);
#endif
}
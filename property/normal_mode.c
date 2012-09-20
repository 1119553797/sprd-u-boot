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
#include <malloc.h>

unsigned char raw_header[8192];
static int flash_page_size = 0;

#define VMJALUNA_PART "vmjaluna"
#define MODEM_PART "modem"
#define KERNEL_PART "kernel"
#define FIXNV_PART "fixnv"
#define BACKUPFIXNV_PART "backupfixnv"
#define RUNTIMEVN_PART "runtimenv"
#define DSP_PART "dsp"
#ifdef CONFIG_SP8810W
#define FIRMWARE_PART "firmware"
#endif

#define DSP_SIZE		(3968 * 1024)
#define VMJALUNA_SIZE		(300 * 1024)

#ifdef CONFIG_SP8810W
#define FIXNV_SIZE			(120 * 1024)
#define FIRMWARE_SIZE 		(9933 * 1024) /* 9.7M */
#else
#define FIXNV_SIZE		(64 * 1024)
#endif

#define PRODUCTINFO_SIZE	(3 * 1024)
#define RUNTIMENV_SIZE		(256 * 1024)
#ifdef MCP_F2R1
#define MODEM_SIZE		(3500 * 1024)  	/* 3.5MB */
#else
#define MODEM_SIZE		(8 * 1024 * 1024)
#endif

#define DSP_ADR			0x00020000
#define VMJALUNA_ADR		0x00400000
#define FIXNV_ADR		0x00480000
#define RUNTIMENV_ADR		0x004a0000
#define MODEM_ADR		0x00500000
#define RAMDISK_ADR 		0x04c00000
#ifdef CONFIG_SP8810W
#define PRODUCTINFO_ADR		0x0049E000
#define FIRMWARE_ADR		0x01600000
#else
#define PRODUCTINFO_ADR		0x00490000
#endif

#if BOOT_NATIVE_LINUX
//pls make sure uboot running area
#define VLX_TAG_ADDR            (0x100)
#define KERNEL_ADR		(0x8000)

#else

#define KERNEL_ADR		0x04508000
#define VLX_TAG_ADDR            0x5100000 //after initrd

#endif

#define MAX_SN_LEN 			(24)
#define SP09_MAX_SN_LEN			MAX_SN_LEN
#define SP09_MAX_STATION_NUM		(15)
#define SP09_MAX_STATION_NAME_LEN	(10)
#define SP09_SPPH_MAGIC_NUMBER          (0X53503039)    // "SP09"
#define SP09_MAX_LAST_DESCRIPTION_LEN   (32)

typedef struct _tagSP09_PHASE_CHECK
{
	unsigned long 	Magic;                	// "SP09"
	char    	SN1[SP09_MAX_SN_LEN]; 	// SN , SN_LEN=24
	char    	SN2[SP09_MAX_SN_LEN];    // add for Mobile
	int     	StationNum;                 	// the test station number of the testing
	char    	StationName[SP09_MAX_STATION_NUM][SP09_MAX_STATION_NAME_LEN];
	unsigned char 	Reserved[13];               	//
	unsigned char 	SignFlag;
	char    	szLastFailDescription[SP09_MAX_LAST_DESCRIPTION_LEN];
	unsigned short  iTestSign;				// Bit0~Bit14 ---> station0~station 14
	                 		  			  //if tested. 0: tested, 1: not tested
	unsigned short  iItem;    // part1: Bit0~ Bit_14 indicate test Station,1 : Pass,

}SP09_PHASE_CHECK_T, *LPSP09_PHASE_CHECK_T;
const static int SP09_MAX_PHASE_BUFF_SIZE = sizeof(SP09_PHASE_CHECK_T);

int eng_getphasecheck(SP09_PHASE_CHECK_T* phase_check)
{
	int aaa;
	unsigned long tested;

	if (phase_check->Magic == SP09_SPPH_MAGIC_NUMBER) {
		//printf("Magic = 0x%08x\n",phase_check->Magic);
		printf("SN1 = %s   SN2 = %s\n",phase_check->SN1, phase_check->SN2);
		/*printf("StationNum = %d\n",phase_check->StationNum);
		printf("Reserved = %s\n",phase_check->Reserved);
		printf("SignFlag = 0x%02x\n",phase_check->SignFlag);
		printf("iTestSign = 0x%04x\n",phase_check->iTestSign);
		printf("iItem = 0x%04x\n",phase_check->iItem);*/
		if (phase_check->SignFlag == 1) {
			for (aaa = 0; aaa < phase_check->StationNum/*SP09_MAX_STATION_NUM*/; aaa ++) {
				printf("%s : ", phase_check->StationName[aaa]);
				tested = 1 << aaa;
				if ((tested & phase_check->iTestSign) == 0) {
					if ((tested & phase_check->iItem) == 0)
						printf("Pass; ");
					else
						printf("Fail; ");
				} else
					printf("UnTested; ");
			}
		} else {
			printf("station status are all invalid!\n");
			for (aaa = 0; aaa < phase_check->StationNum/*SP09_MAX_STATION_NUM*/; aaa ++)
				printf("%s  ", phase_check->StationName[aaa]);
		}
		printf("\nLast error: %s\n",phase_check->szLastFailDescription);
	} else
		printf("no production information / phase check!\n");

	return 0;
}

int eng_phasechecktest(unsigned char *array, int len)
{
	SP09_PHASE_CHECK_T phase;

	memset(&phase, 0, sizeof(SP09_PHASE_CHECK_T));
	memcpy(&phase, array, len);

	return eng_getphasecheck(&phase);
}

extern void cmd_yaffs_mount(char *mp);
extern void cmd_yaffs_umount(char *mp);
extern int cmd_yaffs_ls_chk(const char *dirfilename);
extern void cmd_yaffs_mread_file(char *fn, unsigned char *addr);
void set_vibrator(int on);
void vibrator_hw_init(void);
void MMU_InvalideICACHEALL(void);

void nand_block_info(struct mtd_info *nand, int *good, int *bad)
{
	loff_t off;
	int goodblk, badblk;

	goodblk = badblk = 0;

	for (off = 0; off < nand->size; off += nand->erasesize)
		if (nand_block_isbad(nand, off)) {
			//printf("bad block :  %08llx\n", (unsigned long long)off);
			badblk ++;
		} else {
			//printf("good block : %08llx\n", (unsigned long long)off);
			goodblk ++;
		}
	*good = goodblk;
	*bad = badblk;
}

/*
* retval : -1 is wrong  ;  1 is correct
*/
int nv_is_correct(unsigned char *array, unsigned long size)
{
	if ((array[size] == 0x5a) && (array[size + 1] == 0x5a) && (array[size + 2] == 0x5a) && (array[size + 3] == 0x5a)) {
		array[size] = 0xff; array[size + 1] = 0xff;
		array[size + 2] = 0xff; array[size + 3] = 0xff;
		return 1;
	} else
		return -1;
}

int nv_is_correct_endflag(unsigned char *array, unsigned long size)
{
	if ((array[size] == 0x5a) && (array[size + 1] == 0x5a) && (array[size + 2] == 0x5a) && (array[size + 3] == 0x5a))
		return 1;
	else
		return -1;
}

void array_value_range(unsigned char * array, int start, int end)
{
	int aaa;

	printf("\n\n");

	for (aaa = start; aaa <= end; aaa ++) {
		printf("arr[%d] = %02x\n", aaa, array[aaa]);
	}

	printf("\n\n");
}


void array_value(unsigned char * array, int len)
{
	int aaa;

	printf("\n\n");

	for (aaa = 0; aaa < len; aaa ++) {
		if ((aaa % 16) == 0)
			printf("\n");
		printf(" %02x", array[aaa]);
	}

	printf("\n\n");
}

void array_diff(unsigned char * array1, unsigned char * array2, int len)
{
	int ii;

	printf("arrar diff is starting   array1 = 0x%08x  array2 = 0x%08x  len = %d\n", (unsigned int)array1, (unsigned int)array2, len);
	for (ii = 0; ii < len; ii ++) {
		if (array1[ii] != array2[ii]) {
			printf("\narray1[%d] = 0x%02x  array2[%d] = 0x%02x\n", ii, array1[ii], ii, array2[ii]);
		}
	}
	printf("arrar diff is finished\n");
}
static int start_linux()
{
	void (*theKernel)(int zero, int arch, u32 params);
	u32 exec_at = (u32)-1;
	u32 parm_at = (u32)-1;
	u32 machine_type;

	machine_type = 0x7dd;         /* get machine type */

	theKernel = (void (*)(int, int, u32))KERNEL_ADR; /* set the kernel address */

	*(volatile u32*)0x84001000 = 'j';
	*(volatile u32*)0x84001000 = 'm';
	*(volatile u32*)0x84001000 = 'p';

	*(volatile u32*)(0x20900000 + 0x218) |= (0x1);//internal ram using 0xffff0000
	theKernel(0, machine_type, VLX_TAG_ADDR);    /* jump to kernel with register set */
	while(1);
	return 0;
}


//if not to boot native linux, cmdline=NULL, kerne_pname=boot, backlight_set=on.
void vlx_nand_boot(char * kernel_pname, char * cmdline, int backlight_set)
{
    boot_img_hdr *hdr = (void *)raw_header;
	struct mtd_info *nand;
	struct mtd_device *dev;
	struct part_info *part;
	struct nand_chip *chip;
	u8 pnum;
	int ret;
	size_t size;
	loff_t off = 0;
	char *fixnvpoint = "/fixnv";
	char *fixnvfilename = "/fixnv/fixnv.bin";
	char *fixnvfilename2 = "/fixnv/fixnvchange.bin";
	char *backupfixnvpoint = "/backupfixnv";
	char *backupfixnvfilename = "/backupfixnv/fixnv.bin";
	char *backupfixnvfilename2 = "/backupfixnv/fixnvchange.bin";
	char *runtimenvpoint = "/runtimenv";
	char *runtimenvfilename = "/runtimenv/runtimenv.bin";
	char *runtimenvfilename2 = "/runtimenv/runtimenvchange.bin";
	char *productinfopoint = "/productinfo";
	char *productinfofilename = "/productinfo/productinfo.bin";
	char *productinfofilename2 = "/productinfo/productinfochange.bin";
	int fixnv_right, backupfixnv_right;
	nand_erase_options_t opts;
    	char * mtdpart_def = NULL;
        #ifdef CONFIG_SC8810
     	MMU_Init(CONFIG_MMU_TABLE_ADDR);
	#endif
	ret = mtdparts_init();
	if (ret != 0){
		printf("mtdparts init error %d\n", ret);
		return;
	}

#ifdef CONFIG_SPLASH_SCREEN
#define SPLASH_PART "boot_logo"
	ret = find_dev_and_part(SPLASH_PART, &dev, &pnum, &part);
	if(ret){
		printf("No partition named %s\n", SPLASH_PART);
		return;
	}else if(dev->id->type != MTD_DEV_TYPE_NAND){
		printf("Partition %s not a NAND device\n", SPLASH_PART);
		return;
	}

	off=part->offset;
	nand = &nand_info[dev->id->num];
	//read boot image header
	size = 1<<19;//where the size come from????
	char * bmp_img = malloc(size);
	if(!bmp_img){
	    printf("not enough memory for splash image\n");
	    return;
	}
	ret = nand_read_offset_ret(nand, off, &size, (void *)bmp_img, &off);
	if(ret != 0){
		printf("function: %s nand read error %d\n", __FUNCTION__, ret);
		return;
	}
    extern int lcd_display_bitmap(ulong bmp_image, int x, int y);
    extern void lcd_display(void);
    extern void set_backlight(uint32_t value);
    if(backlight_set == BACKLIGHT_ON){
	    extern void *lcd_base;
	    extern void Dcache_CleanRegion(unsigned int addr, unsigned int length);

	    lcd_display_bitmap((ulong)bmp_img, 0, 0);
#ifdef CONFIG_SC8810
	    Dcache_CleanRegion((unsigned int)(lcd_base), size);//Size is to large.
#endif
	    lcd_display();
	    set_backlight(255);
       	    //printf("aftersetbacklight\n");
	    udelay(1);	//zhuwenjian need a delay to effect register
    }
#endif
    set_vibrator(0);

#if !(BOOT_NATIVE_LINUX)
	/*int good_blknum, bad_blknum;
	nand_block_info(nand, &good_blknum, &bad_blknum);
	printf("good is %d  bad is %d\n", good_blknum, bad_blknum);*/
	///////////////////////////////////////////////////////////////////////
	/* recovery damaged fixnv or backupfixnv */
	fixnv_right = 0;
	memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
	cmd_yaffs_mount(fixnvpoint);
	ret = cmd_yaffs_ls_chk(fixnvfilename);
	if (ret == (FIXNV_SIZE + 4)) {
		cmd_yaffs_mread_file(fixnvfilename, (unsigned char *)FIXNV_ADR);
		if (1 == nv_is_correct_endflag((unsigned char *)FIXNV_ADR, FIXNV_SIZE))
			fixnv_right = 1;//right
	}
	cmd_yaffs_umount(fixnvpoint);

	backupfixnv_right = 0;
	memset((unsigned char *)RUNTIMENV_ADR, 0xff, FIXNV_SIZE + 4);
	cmd_yaffs_mount(backupfixnvpoint);
	ret = cmd_yaffs_ls_chk(backupfixnvfilename);
	if (ret == (FIXNV_SIZE + 4)) {
		cmd_yaffs_mread_file(backupfixnvfilename, (unsigned char *)RUNTIMENV_ADR);
		if (1 == nv_is_correct_endflag((unsigned char *)RUNTIMENV_ADR, FIXNV_SIZE))
			backupfixnv_right = 1;//right
	}
	cmd_yaffs_umount(backupfixnvpoint);
	//printf("fixnv_right = %d  backupfixnv_right = %d\n", fixnv_right, backupfixnv_right);
	if ((fixnv_right == 1) && (backupfixnv_right == 0)) {
		printf("fixnv is right, but backupfixnv is wrong, so erase and recovery backupfixnv\n");
		////////////////////////////////
		find_dev_and_part(BACKUPFIXNV_PART, &dev, &pnum, &part);
		//printf("offset = 0x%08x  size = 0x%08x\n", part->offset, part->size);
		nand = &nand_info[dev->id->num];
		memset(&opts, 0, sizeof(opts));
		opts.offset = part->offset;
		opts.length = part->size;
		opts.quiet = 1;
		nand_erase_opts(nand, &opts);
		////////////////////////////////
		cmd_yaffs_mount(backupfixnvpoint);
    		cmd_yaffs_mwrite_file(backupfixnvfilename, (char *)FIXNV_ADR, (FIXNV_SIZE + 4));
		cmd_yaffs_ls_chk(backupfixnvfilename);
		cmd_yaffs_umount(backupfixnvpoint);
	} else if ((fixnv_right == 0) && (backupfixnv_right == 1)) {
		printf("backupfixnv is right, but fixnv is wrong, so erase and recovery fixnv\n");
		////////////////////////////////
		find_dev_and_part(FIXNV_PART, &dev, &pnum, &part);
		//printf("offset = 0x%08x  size = 0x%08x\n", part->offset, part->size);
		nand = &nand_info[dev->id->num];
		memset(&opts, 0, sizeof(opts));
		opts.offset = part->offset;
		opts.length = part->size;
		opts.quiet = 1;
		nand_erase_opts(nand, &opts);
		////////////////////////////////
		cmd_yaffs_mount(fixnvpoint);
    		cmd_yaffs_mwrite_file(fixnvfilename, (char *)RUNTIMENV_ADR, (FIXNV_SIZE + 4));
		cmd_yaffs_ls_chk(fixnvfilename);
		cmd_yaffs_umount(fixnvpoint);
	} else if ((fixnv_right == 0) && (backupfixnv_right == 0)) {
		printf("\n\nfixnv and backupfixnv are all wrong.\n\n");
	}
	///////////////////////////////////////////////////////////////////////
	/* FIXNV_PART */
	printf("Reading fixnv to 0x%08x\n", FIXNV_ADR);
	memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
	/* fixnv */
    cmd_yaffs_mount(backupfixnvpoint);
	ret = cmd_yaffs_ls_chk(backupfixnvfilename);
	if (ret == (FIXNV_SIZE + 4)) {
		cmd_yaffs_mread_file(backupfixnvfilename, (unsigned char *)FIXNV_ADR);
		if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE)) {
			memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
			ret = cmd_yaffs_ls_chk(backupfixnvfilename2);
			if (ret == (FIXNV_SIZE + 4)) {
				cmd_yaffs_mread_file(backupfixnvfilename2, (unsigned char *)FIXNV_ADR);
				if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE)) {
					/*#########################*/
					cmd_yaffs_umount(backupfixnvpoint);
					/* file is wrong */
					memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
					/* read fixnv */
   			cmd_yaffs_mount(fixnvpoint);
			ret = cmd_yaffs_ls_chk(fixnvfilename);
			if (ret == (FIXNV_SIZE + 4)) {
				cmd_yaffs_mread_file(fixnvfilename, (unsigned char *)FIXNV_ADR);
				if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE)) {
					memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
					/* read fixnv backup */
					ret = cmd_yaffs_ls_chk(fixnvfilename2);
					if (ret == (FIXNV_SIZE + 4)) {
						cmd_yaffs_mread_file(fixnvfilename2, (unsigned char *)FIXNV_ADR);
						if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE))
							memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
					}
					/* read fixnv backup */
				}
			} else {
				/* read fixnv backup */
				ret = cmd_yaffs_ls_chk(fixnvfilename2);
				if (ret == (FIXNV_SIZE + 4)) {
					cmd_yaffs_mread_file(fixnvfilename2, (unsigned char *)FIXNV_ADR);
					if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE))
						memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
				}
				/* read fixnv backup */
			}
			cmd_yaffs_umount(fixnvpoint);
					/*#########################*/
				}
			} else {
				/*#########################*/
				cmd_yaffs_umount(backupfixnvpoint);
				/* file is wrong */
				memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
				/* read fixnv */
    			cmd_yaffs_mount(fixnvpoint);
			ret = cmd_yaffs_ls_chk(fixnvfilename);
			if (ret == (FIXNV_SIZE + 4)) {
				cmd_yaffs_mread_file(fixnvfilename, (unsigned char *)FIXNV_ADR);
				if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE)) {
					memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
					/* read fixnv backup */
					ret = cmd_yaffs_ls_chk(fixnvfilename2);
					if (ret == (FIXNV_SIZE + 4)) {
						cmd_yaffs_mread_file(fixnvfilename2, (unsigned char *)FIXNV_ADR);
						if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE))
							memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
					}
					/* read fixnv backup */
				}
			} else {
				/* read fixnv backup */
				ret = cmd_yaffs_ls_chk(fixnvfilename2);
				if (ret == (FIXNV_SIZE + 4)) {
					cmd_yaffs_mread_file(fixnvfilename2, (unsigned char *)FIXNV_ADR);
					if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE))
						memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
				}
				/* read fixnv backup */
			}
			cmd_yaffs_umount(fixnvpoint);
				/*#########################*/
			}
			//////////////////////
		} else {
			/* file is right */
			cmd_yaffs_umount(backupfixnvpoint);
		}
	} else {
		memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
		ret = cmd_yaffs_ls_chk(backupfixnvfilename2);
		if (ret == (FIXNV_SIZE + 4)) {
			cmd_yaffs_mread_file(backupfixnvfilename2, (unsigned char *)FIXNV_ADR);
			if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE)) {
				/*#########################*/
				cmd_yaffs_umount(backupfixnvpoint);
				/* file is wrong */
				memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
				/* read fixnv */
    				cmd_yaffs_mount(fixnvpoint);
				ret = cmd_yaffs_ls_chk(fixnvfilename);
				if (ret == (FIXNV_SIZE + 4)) {
					cmd_yaffs_mread_file(fixnvfilename, (unsigned char *)FIXNV_ADR);
					if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE)) {
						memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
						/* read fixnv backup */
						ret = cmd_yaffs_ls_chk(fixnvfilename2);
						if (ret == (FIXNV_SIZE + 4)) {
							cmd_yaffs_mread_file(fixnvfilename2, (unsigned char *)FIXNV_ADR);
							if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE))
								memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
						}
						/* read fixnv backup */
					}
				} else {
					/* read fixnv backup */
					ret = cmd_yaffs_ls_chk(fixnvfilename2);
					if (ret == (FIXNV_SIZE + 4)) {
						cmd_yaffs_mread_file(fixnvfilename2, (unsigned char *)FIXNV_ADR);
						if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE))
							memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
					}
					/* read fixnv backup */
				}
				cmd_yaffs_umount(fixnvpoint);
				/*#########################*/
			}
		} else {
			/*#########################*/
			cmd_yaffs_umount(backupfixnvpoint);
			/* file is wrong */
			memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
			/* read fixnv */
    			cmd_yaffs_mount(fixnvpoint);
			ret = cmd_yaffs_ls_chk(fixnvfilename);
			if (ret == (FIXNV_SIZE + 4)) {
				cmd_yaffs_mread_file(fixnvfilename, (unsigned char *)FIXNV_ADR);
				if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE)) {
					memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
					/* read fixnv backup */
					ret = cmd_yaffs_ls_chk(fixnvfilename2);
					if (ret == (FIXNV_SIZE + 4)) {
						cmd_yaffs_mread_file(fixnvfilename2, (unsigned char *)FIXNV_ADR);
						if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE))
							memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
					}
					/* read fixnv backup */
				}
			} else {
				/* read fixnv backup */
				ret = cmd_yaffs_ls_chk(fixnvfilename2);
				if (ret == (FIXNV_SIZE + 4)) {
					cmd_yaffs_mread_file(fixnvfilename2, (unsigned char *)FIXNV_ADR);
					if (-1 == nv_is_correct((unsigned char *)FIXNV_ADR, FIXNV_SIZE))
						memset((unsigned char *)FIXNV_ADR, 0xff, FIXNV_SIZE + 4);
				}
				/* read fixnv backup */
			}
			cmd_yaffs_umount(fixnvpoint);
			/*#########################*/
		}
		///////////////////////////////
	}
	//array_value((unsigned char *)FIXNV_ADR, FIXNV_SIZE);

	///////////////////////////////////////////////////////////////////////
	/* PRODUCTINFO_PART */
	printf("Reading productinfo to 0x%08x\n", PRODUCTINFO_ADR);
    	cmd_yaffs_mount(productinfopoint);
	ret = cmd_yaffs_ls_chk(productinfofilename);
	if (ret == (PRODUCTINFO_SIZE + 4)) {
		cmd_yaffs_mread_file(productinfofilename, (unsigned char *)PRODUCTINFO_ADR);
		if (-1 == nv_is_correct((unsigned char *)PRODUCTINFO_ADR, PRODUCTINFO_SIZE)) {
			memset((unsigned char *)PRODUCTINFO_ADR, 0xff, PRODUCTINFO_SIZE + 4);
			ret = cmd_yaffs_ls_chk(productinfofilename2);
			if (ret == (PRODUCTINFO_SIZE + 4)) {
				cmd_yaffs_mread_file(productinfofilename2, (unsigned char *)PRODUCTINFO_ADR);
				if (-1 == nv_is_correct((unsigned char *)PRODUCTINFO_ADR, PRODUCTINFO_SIZE)) {
					memset((unsigned char *)PRODUCTINFO_ADR, 0xff, PRODUCTINFO_SIZE + 4);
				}
			}
		}
	} else {
		memset((unsigned char *)PRODUCTINFO_ADR, 0xff, PRODUCTINFO_SIZE + 4);
		ret = cmd_yaffs_ls_chk(productinfofilename2);
		if (ret == (PRODUCTINFO_SIZE + 4)) {
			cmd_yaffs_mread_file(productinfofilename2, (unsigned char *)PRODUCTINFO_ADR);
			if (-1 == nv_is_correct((unsigned char *)PRODUCTINFO_ADR, PRODUCTINFO_SIZE)) {
				memset((unsigned char *)PRODUCTINFO_ADR, 0xff, PRODUCTINFO_SIZE + 4);
			}
		}
	}
	cmd_yaffs_umount(productinfopoint);
	//array_value((unsigned char *)PRODUCTINFO_ADR, PRODUCTINFO_SIZE);
	eng_phasechecktest((unsigned char *)PRODUCTINFO_ADR, SP09_MAX_PHASE_BUFF_SIZE);
	///////////////////////////////////////////////////////////////////////

	///////////////////////////////////////////////////////////////////////
	/* RUNTIMEVN_PART */
	printf("Reading runtimenv to 0x%08x\n", RUNTIMENV_ADR);
	/* runtimenv */
    cmd_yaffs_mount(runtimenvpoint);
	ret = cmd_yaffs_ls_chk(runtimenvfilename);
	if (ret == (RUNTIMENV_SIZE + 4)) {
		/* file exist */
		cmd_yaffs_mread_file(runtimenvfilename, (unsigned char *)RUNTIMENV_ADR);
		if (-1 == nv_is_correct((unsigned char *)RUNTIMENV_ADR, RUNTIMENV_SIZE)) {
			////////////////
			/* file isn't right and read backup file */
			memset((unsigned char *)RUNTIMENV_ADR, 0xff, RUNTIMENV_SIZE + 4);
			ret = cmd_yaffs_ls_chk(runtimenvfilename2);
			if (ret == (RUNTIMENV_SIZE + 4)) {
				cmd_yaffs_mread_file(runtimenvfilename2, (unsigned char *)RUNTIMENV_ADR);
				if (-1 == nv_is_correct((unsigned char *)RUNTIMENV_ADR, RUNTIMENV_SIZE)) {
					/* file isn't right */
					memset((unsigned char *)RUNTIMENV_ADR, 0xff, RUNTIMENV_SIZE + 4);
				}
			}
			////////////////
		}
	} else {
		/* file don't exist and read backup file */
		memset((unsigned char *)RUNTIMENV_ADR, 0xff, RUNTIMENV_SIZE + 4);
		ret = cmd_yaffs_ls_chk(runtimenvfilename2);
		if (ret == (RUNTIMENV_SIZE + 4)) {
			cmd_yaffs_mread_file(runtimenvfilename2, (unsigned char *)RUNTIMENV_ADR);
			if (-1 == nv_is_correct((unsigned char *)RUNTIMENV_ADR, RUNTIMENV_SIZE)) {
				/* file isn't right */
				memset((unsigned char *)RUNTIMENV_ADR, 0xff, RUNTIMENV_SIZE + 4);
			}
		}
	}
	cmd_yaffs_umount(runtimenvpoint);
	//array_value((unsigned char *)RUNTIMENV_ADR, RUNTIMENV_SIZE);

	////////////////////////////////////////////////////////////////
	/* DSP_PART */
	printf("Reading dsp to 0x%08x\n", DSP_ADR);
	ret = find_dev_and_part(DSP_PART, &dev, &pnum, &part);
	if (ret) {
		printf("No partition named %s\n", DSP_PART);
		return;
	} else if (dev->id->type != MTD_DEV_TYPE_NAND) {
		printf("Partition %s not a NAND device\n", DSP_PART);
		return;
	}

	off = part->offset;
	nand = &nand_info[dev->id->num];
	flash_page_size = nand->writesize;
	size = (DSP_SIZE + (flash_page_size - 1)) & (~(flash_page_size - 1));
	if(size <= 0) {
		printf("dsp image should not be zero\n");
		return;
	}
	ret = nand_read_offset_ret(nand, off, &size, (void*)DSP_ADR, &off);
	if(ret != 0) {
		printf("dsp nand read error %d\n", ret);
		return;
	}
#endif
	////////////////////////////////////////////////////////////////
	/* KERNEL_PART */
	printf("Reading kernel to 0x%08x\n", KERNEL_ADR);

	ret = find_dev_and_part(kernel_pname, &dev, &pnum, &part);
	if(ret){
		printf("No partition named %s\n", kernel_pname);
        return;
	}else if(dev->id->type != MTD_DEV_TYPE_NAND){
		printf("Partition %s not a NAND device\n", kernel_pname);
        return;
	}

	off=part->offset;
	nand = &nand_info[dev->id->num];
	//read boot image header
	size = nand->writesize;
	flash_page_size = nand->writesize;
	ret = nand_read_offset_ret(nand, off, &size, (void *)hdr, &off);
	if(ret != 0){
		printf("function: %s nand read error %d\n", __FUNCTION__, ret);
        return;
	}
	if(memcmp(hdr->magic, BOOT_MAGIC, BOOT_MAGIC_SIZE)){
		printf("bad boot image header, give up read!!!!\n");
        return;
	}
	else
	{
		//read kernel image
		size = (hdr->kernel_size+(flash_page_size - 1)) & (~(flash_page_size - 1));
		if(size <=0){
			printf("kernel image should not be zero\n");
			return;
		}
		ret = nand_read_offset_ret(nand, off, &size, (void *)KERNEL_ADR, &off);
		if(ret != 0){
			printf("kernel nand read error %d\n", ret);
			return;
		}
		//read ramdisk image
		size = (hdr->ramdisk_size+(flash_page_size - 1)) & (~(flash_page_size - 1));
		if(size<0){
			printf("ramdisk size error\n");
			return;
		}
		ret = nand_read_offset_ret(nand, off, &size, (void *)RAMDISK_ADR, &off);
		if(ret != 0){
			printf("ramdisk nand read error %d\n", ret);
			return;
		}
	}

#if !(BOOT_NATIVE_LINUX)
	////////////////////////////////////////////////////////////////
	/* MODEM_PART */
	printf("Reading modem to 0x%08x\n", MODEM_ADR);
	ret = find_dev_and_part(MODEM_PART, &dev, &pnum, &part);
	if (ret) {
		printf("No partition named %s\n", MODEM_PART);
		return;
	} else if (dev->id->type != MTD_DEV_TYPE_NAND) {
		printf("Partition %s not a NAND device\n", MODEM_PART);
		return;
	}

	off = part->offset;
	nand = &nand_info[dev->id->num];
	size = (MODEM_SIZE +(flash_page_size - 1)) & (~(flash_page_size - 1));
	if(size <= 0) {
		printf("modem image should not be zero\n");
		return;
	}
	ret = nand_read_offset_ret(nand, off, &size, (void*)MODEM_ADR, &off);
	if(ret != 0) {
		printf("modem nand read error %d\n", ret);
		return;
	}

	//array_value((unsigned char *)MODEM_ADR, MODEM_SIZE);
#ifdef CONFIG_SP8810W
	/* FIRMWARE_PART */
	printf("Reading firmware to 0x%08x\n", FIRMWARE_ADR);
	ret = find_dev_and_part(FIRMWARE_PART, &dev, &pnum, &part);
	if (ret) {
		printf("No partition named %s\n", FIRMWARE_PART);
		return;
	} else if (dev->id->type != MTD_DEV_TYPE_NAND) {
		printf("Partition %s not a NAND device\n", FIRMWARE_PART);
		return;
	}

	off = part->offset;
	nand = &nand_info[dev->id->num];
	size = (FIRMWARE_SIZE +(flash_page_size - 1)) & (~(flash_page_size - 1));
	if(size <= 0) {
		printf("firmware image should not be zero\n");
		return;
	}
	ret = nand_read_offset_ret(nand, off, &size, (void*)FIRMWARE_ADR, &off);
	if(ret != 0) {
		printf("firmware nand read error %d\n", ret);
		return;
	}
#endif
	////////////////////////////////////////////////////////////////
	/* VMJALUNA_PART */
	printf("Reading vmjaluna to 0x%08x\n", VMJALUNA_ADR);
	ret = find_dev_and_part(VMJALUNA_PART, &dev, &pnum, &part);
	if (ret) {
		printf("No partition named %s\n", VMJALUNA_PART);
		return;
	} else if (dev->id->type != MTD_DEV_TYPE_NAND) {
		printf("Partition %s not a NAND device\n", VMJALUNA_PART);
		return;
	}

	off = part->offset;
	nand = &nand_info[dev->id->num];
	size = (VMJALUNA_SIZE +(flash_page_size - 1)) & (~(flash_page_size - 1));
	if(size <= 0) {
		printf("modem image should not be zero\n");
		return;
	}
	ret = nand_read_offset_ret(nand, off, &size, (void*)VMJALUNA_ADR, &off);
	if(ret != 0) {
		printf("modem nand read error %d\n", ret);
		return;
	}
#endif

	//array_value((unsigned char *)VMJALUNA_ADR, 16 * 10);
    //check caliberation mode
    int str_len;
    char * buf;
    buf = malloc(1024);
    memset (buf, 0, 1024);

    sprintf(buf, "initrd=0x%x,0x%x", RAMDISK_ADR, hdr->ramdisk_size);

    /* preset loop_per_jiffy */
    str_len = strlen(buf);
#ifdef CONFIG_LOOP_PER_JIFFY
    sprintf(&buf[str_len], " lpj=%d", CONFIG_LOOP_PER_JIFFY);
#else
    sprintf(&buf[str_len], " lpj=%d", 3350528); /* SC8810 1GHz */
#endif

    str_len = strlen(buf);
    mtdpart_def = get_mtdparts();
    sprintf(&buf[str_len], " %s", mtdpart_def);
    if(cmdline && cmdline[0]){
            str_len = strlen(buf);
            sprintf(&buf[str_len], " %s", cmdline);
    }
	{
		//add lcd id
		extern uint32_t load_lcd_id_to_kernel();
		uint32_t lcd_id = load_lcd_id_to_kernel();

		str_len = strlen(buf);
		sprintf(&buf[str_len], " video=sprdfb:fb0_id=0x%x,fb1_id=0x%x",
				 lcd_id, 0);
		str_len = strlen(buf);

	}
	{
		char *factorymodepoint = "/productinfo";
		char *factorymodefilename = "/productinfo/factorymode.file";
		char *usb_serialfilename = "/productinfo/usb_s.ini";
		char *tpcalcoefilename = "/productinfo/tpcalcoef";  
		cmd_yaffs_mount(factorymodepoint);
		ret = cmd_yaffs_ls_chk(usb_serialfilename );
		if (ret == -1) {
			/* no usb file */
		} else {
			str_len = strlen(buf);
			sprintf(&buf[str_len], " usb_s=");
			str_len = strlen(buf);
			cmd_yaffs_mread_file(usb_serialfilename, &buf[str_len]);
			while((buf[str_len] != '\n') && (buf[str_len] != '\r'))//\r is mac file; \n is unix/linux file; \n\r is windows file;
			{
				str_len++;
			}
			buf[str_len] = '\0';
			buf[str_len+1] = '\0';
		}
		ret = cmd_yaffs_ls_chk(factorymodefilename );
		if (ret == -1) {
			/* no factorymode.file found, nothing to do */
		} else {
			str_len = strlen(buf);
			sprintf(&buf[str_len], " factory");
		}
		ret = cmd_yaffs_ls_chk(tpcalcoefilename );
		if (ret == -1) {
			/* no tpcalcoef file */
		} else {
			str_len = strlen(buf);
			sprintf(&buf[str_len], " cal_coef=");
			str_len = strlen(buf);
			cmd_yaffs_mread_file(tpcalcoefilename, &buf[str_len]);
			while((buf[str_len] != '\n') && (buf[str_len] != '\r'))//\r is mac file; \n is unix/linux file; \n\r is windows file;
			{
				str_len++;
			}
			buf[str_len] = '\0';
			buf[str_len+1] = '\0';
		}
		cmd_yaffs_umount(factorymodepoint);
	}
	str_len = strlen(buf);
#ifdef RAM512M
    sprintf(&buf[str_len], " ram=512M");
#else
    sprintf(&buf[str_len], " ram=256M");
#endif

	chip = (struct nand_chip *)(nand->priv);
	str_len = strlen(buf);
	sprintf(&buf[str_len], " nandflash=nandid(0x%02x,", chip->nandid[0]);
	str_len = strlen(buf);
	sprintf(&buf[str_len], "0x%02x,", chip->nandid[1]);
	str_len = strlen(buf);
	sprintf(&buf[str_len], "0x%02x,", chip->nandid[2]);
	str_len = strlen(buf);
	sprintf(&buf[str_len], "0x%02x,", chip->nandid[3]);
	str_len = strlen(buf);
	sprintf(&buf[str_len], "0x%02x),", chip->nandid[4]);
	str_len = strlen(buf);
	sprintf(&buf[str_len], "pagesize(%d),oobsize(%d),eccsize(%d),eccbit(%d)", nand->writesize, nand->oobsize, chip->ecc.size, chip->eccbitmode);

    printf("pass cmdline: %s\n", buf);
    //lcd_printf(" pass cmdline : %s\n",buf);
    //lcd_display();
    creat_atags(VLX_TAG_ADDR, buf, NULL, 0);
    void (*entry)(void) = (void*) VMJALUNA_ADR;
#ifndef CONFIG_SC8810
    MMU_InvalideICACHEALL();
#endif
#ifdef CONFIG_SC8810
    MMU_DisableIDCM();
#endif

#if BOOT_NATIVE_LINUX
	start_linux();
#else
	entry();
#endif
}
void normal_mode(void)
{
#ifdef CONFIG_SC8810
    //MMU_Init(CONFIG_MMU_TABLE_ADDR);
	vibrator_hw_init();
#endif
    set_vibrator(1);
#if BOOT_NATIVE_LINUX
    vlx_nand_boot(BOOT_PART, CONFIG_BOOTARGS, BACKLIGHT_ON);
#else
    vlx_nand_boot(BOOT_PART, NULL, BACKLIGHT_ON);
#endif

}

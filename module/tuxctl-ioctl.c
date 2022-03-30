/* tuxctl-ioctl.c
 *
 * Driver (skeleton) for the mp2 tuxcontrollers for ECE391 at UIUC.
 *
 * Mark Murphy 2006
 * Andrew Ofisher 2007
 * Steve Lumetta 12-13 Sep 2009
 * Puskar Naha 2013
 */

#include <asm/current.h>
#include <asm/uaccess.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/kdev_t.h>
#include <linux/tty.h>
#include <linux/spinlock.h>

#include "tuxctl-ld.h"
#include "tuxctl-ioctl.h"
#include "mtcp.h"

#define debug(str, ...) \
	printk(KERN_DEBUG "%s: " str, __FUNCTION__, ## __VA_ARGS__)

/*
 * State -1: cmd not sent
 * State 0: cmd sent to device
 * State 1: cmd complete 
 */
static volatile int init_sent = -1;
static volatile int bioc_sent = -1;
static volatile int led_sent = -1;

static unsigned long buttons_pressed = 0;
spinlock_t lock = SPIN_LOCK_UNLOCKED;
unsigned long flag;

static unsigned char hex_table[16] = {
	0xE7, //0
	0x06, //1
	0xCB, //2
	0x8F, //3
	0x2E, //4
	0XAD, //5
	0XED, //6
	0X86, //7
	0XEF, //8
	0XAF, //9
	0XEE, //A
	0X6D, //b
	0XE1, //C
	0X4F, //d
	0XE9, //E
	0XE8  //F
};

/************************ Protocol Implementation *************************/

/* tuxctl_handle_packet()
 * IMPORTANT : Read the header for tuxctl_ldisc_data_callback() in 
 * tuxctl-ld.c. It calls this function, so all warnings there apply 
 * here as well.
 */
void tuxctl_handle_packet (struct tty_struct* tty, unsigned char* packet)
{
    unsigned a, b, c;

    a = packet[0]; /* Avoid printk() sign extending the 8-bit */
    b = packet[1]; /* values when printing them. */
    c = packet[2];

    //printk("packet : %x %x %x\n", a, b, c); 
    spin_lock_irqsave(&lock, flag);

    switch(a){

	    case MTCP_RESET:

		    if (init_sent == 0)
		    	init_sent = 1;
		    else {
			bioc_sent = -1;
			led_sent =  -1;
		    }
		    //printk(">>> MTCP_RESET\n");
		    break;

	    case MTCP_ACK: 

		   // printk(">>> ACK received\n");
		    break;

	    case MTCP_BIOC_EVENT:

		    if (((b & 0x0F) != 0x0F) || ((c & 0x0F) != 0x0F)){
		    buttons_pressed = 0; 
		    buttons_pressed |= (b & 0x0F);
		    buttons_pressed |= (c << 4);
		    buttons_pressed = (~buttons_pressed) & 0xFF;
		    if (buttons_pressed == 0x40)
			    buttons_pressed = 0x20;
		    else if (buttons_pressed == 0x20)
			    buttons_pressed = 0x40;
		    }

		    break;

	    case MTCP_ERROR:
		    //printk(">>> MTCP_ERROR: invalid command sent to device\n");
		    break;

	spin_unlock_irqrestore(&lock, flag);
	return;
    
    }

}

/******** IMPORTANT NOTE: READ THIS BEFORE IMPLEMENTING THE IOCTLS ************
 *                                                                            *
 * The ioctls should not spend any time waiting for responses to the commands *
 * they send to the controller. The data is sent over the serial line at      *
 * 9600 BAUD. At this rate, a byte takes approximately 1 millisecond to       *
 * transmit; this means that there will be about 9 milliseconds between       *
 * the time you request that the low-level serial driver send the             *
 * 6-byte SET_LEDS packet and the time the 3-byte ACK packet finishes         *
 * arriving. This is far too long a time for a system call to take. The       *
 * ioctls should return immediately with success if their parameters are      *
 * valid.                                                                     *
 *                                                                            *
 ******************************************************************************/
int 
tuxctl_ioctl (struct tty_struct* tty, struct file* file, 
	      unsigned cmd, unsigned long arg)
{
    char command[6];

    switch (cmd) {

	case TUX_INIT:

		//printk(">>> TUX_INIT\n");

		init_sent = -1;
		bioc_sent = -1;
		led_sent = -1;
		buttons_pressed = 0;

		command[0] = MTCP_RESET_DEV;
		init_sent = 0;
		tuxctl_ldisc_put(tty, command, 1);
		//if (tuxctl_ldisc_put(tty, command, 1) > 0)
		//	printk("MTCP_RESET_DEV error");

		return 0;
		break;

	case TUX_BUTTONS:


		if ((unsigned long*) arg == NULL)
			return -EINVAL;

		if (init_sent == 1){
			if (bioc_sent == -1){
				command[0] = MTCP_DBG_OFF;
				tuxctl_ldisc_put(tty, command, 1);
				//if (tuxctl_ldisc_put(tty, command, 1) > 0)
				//	printk("MTCP_DBG_OFF error");

				command[0] = MTCP_BIOC_ON;
				bioc_sent = 1;
				tuxctl_ldisc_put(tty, command, 1);
				//if (tuxctl_ldisc_put(tty, command, 1) > 0)
				//	printk("MTCP_BIOC_ON error");
			}
		}


    		spin_lock_irqsave(&lock, flag);
		copy_to_user ((void*) arg, (const void*) &buttons_pressed, 4);	
		buttons_pressed = 0;
		spin_unlock_irqrestore(&lock, flag);
		//printk(">>> TUX_BUTTONS : 0x%lx \n", buttons_pressed); 
		return 0;
		break;


	case TUX_SET_LED:

		//printk(">>> TUX_SET_LED : 0x%lx \n", arg);

		if (init_sent == 1){
			int i, hex_index, point;
			if (led_sent == -1){
				command[0] = MTCP_DBG_OFF;
				tuxctl_ldisc_put(tty, command, 1);
				//if (tuxctl_ldisc_put(tty, command, 1) > 0)
				//	printk("MTCP_DBG_OFF error");

				command[0] = MTCP_LED_USR;
				tuxctl_ldisc_put(tty, command, 1);
				//if (tuxctl_ldisc_put(tty, command, 1) > 0)
				//	printk("MTCP_LED_USR error");
			}

			//MTCP_SET_LED
			command[0] = MTCP_LED_SET;
			command[1] = (char) ((arg & 0x00FFFFFF) >> 16);

			// set other command bytes 
			i = 2;
			point = (int) ((arg >> 24) & 0xF);

			if (command[1] & 0x1){
				hex_index = (int) (arg & 0xF);	
				command[i] = hex_table[hex_index];
				if (((point >> 0) & 0x1) == 0x1)
					command[i] |= 0x10;
				i++;		
			}
			if (command[1] & 0x2){
				hex_index = (int) ((arg >> 4) & 0xF);	
				command[i] = hex_table[hex_index];
				if (((point >> 1) & 0x1) == 0x1)
					command[i] |= 0x10;
				i++;
			}
			if (command[1] & 0x4){
				hex_index = (int) ((arg >> 8) & 0xF);	
				command[i] = hex_table[hex_index];
				if (((point >> 2) & 0x1) == 0x1)
					command[i] |= 0x10;
				i++;
			}
			if (command[1] & 0x8){
				hex_index = (int) ((arg >> 12) & 0xF);	
				command[i] = hex_table[hex_index];
				if (((point >> 3) & 0x1) == 0x1)
					command[i] |= 0x10;
				i++;
			}

			led_sent = 1;
			tuxctl_ldisc_put(tty, command, i);
			//if (tuxctl_ldisc_put(tty, command, i) > 0)
			//	printk("MTCP_LED_SET error");
		}
		return 0;
		break;

	case TUX_LED_ACK:
	case TUX_LED_REQUEST:
	case TUX_READ_LED:
	default:
	    return -EINVAL;
    }
}


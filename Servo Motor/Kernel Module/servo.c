/*******************************************
Author: Jiangyu Wang
Email: jiangyu@bu.edu
Date: May 5 2017
*******************************************/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/fcntl.h> /* O_ACCMODE */
#include <linux/jiffies.h> /* jiffies */
#include <asm/system.h> /* cli(), *_flags */
#include <asm/uaccess.h> /* copy_from/to_user */
#include <linux/interrupt.h>
#include <asm/arch/gpio.h>
#include <asm/hardware.h>
#include <asm/arch/pxa-regs.h>
#include <linux/clocksource.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <asm/mach/time.h>

#define TOGGLE_TICK 80000
 // @TODO figure out resolution maths
#define PWMSERVO_MAJOR 62

/** Define module metadata */
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Christopher J. Woodall <chris.j.woodall@gmail.com>");

/** Define pwmservo file ops */
/* Define mytimer file operator function headers (open, release, read, write) */
static int pwmservo_open(struct inode *inode, struct file *filp);
static int pwmservo_release(struct inode *inode, struct file *filp);
static ssize_t pwmservo_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t pwmservo_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);

/* pwmservo file access structure */
struct file_operations pwmservo_fops = {
    read: pwmservo_read,
    write: pwmservo_write,
    open: pwmservo_open,
    release: pwmservo_release
};

/* Prototypes */
static int pwmservo_init(void);
static void pwmservo_exit(void);

static struct timer_list mycounter;
void counter_callback(unsigned long data);

/* Declaration of the init and exit functions */
module_init(pwmservo_init);
module_exit(pwmservo_exit);

// Setup servo value global variables.
uint32_t servo_val0 = 0;
uint32_t servo_cycles = 0;
static uint32_t res;
static int direction = 1; //Clockwise Turning
static int flag = 0;

static irqreturn_t servo_timer_handler(int irq, void *dev_id) {
        //printk(KERN_ALERT "HANDLING!");
        //printk(KERN_ALERT "%i", OSCR);
    int next_match;
    
    do {
        OSSR = OSSR_M1;  /* Clear match on timer 1 */
        next_match = (OSMR1 += TOGGLE_TICK);
        servo_cycles += 1;
    } while( (signed long)(next_match - OSCR) <= 8 );
    
    // Send servo_valN to the appropriate PWM channel when the time is right.
    PWM_PWDUTY0 = ((servo_cycles & 0x3) == 0x3)?servo_val0:0;
  
    // Clever alternative
    // servo_cycles
    return IRQ_HANDLED;
}

static int pwmservo_init(void) {
    /** Register character driver for pwmservo **/
    int ret;
    ret = register_chrdev(PWMSERVO_MAJOR, "pwmservo", &pwmservo_fops);

    if (ret < 0) {
        printk(KERN_ALERT "Woah! You failed to register the pwmservo char device...\n");
        return ret;
    }
    
    /** Setup GPIOs for PWM0 and PWM1 **/
    pxa_gpio_mode(GPIO16_PWM0_MD); // setup GPIO16 as PWM0
    pxa_set_cken(CKEN0_PWM0,1); //Enable the PWM0 Clock
    res = 110; 
    servo_val0 = res; // Start from LEFT most 
    PWM_PWDUTY0 = servo_val0;
    PWM_CTRL0 = 0x3F;
    PWM_PERVAL0 = 0x3FF;
    
    // Initialize OS TIMER
    //    OSSR = 0xf; /* clear status on all timers */
    if (request_irq(IRQ_OST1, &servo_timer_handler, IRQF_TIMER | IRQF_DISABLED, "", NULL)) {
        goto fail;
    }

    OIER |= OIER_E1; /* enable match on timer match 1 to cause interrupts */
    // Install OS Timer Match 1 interrupt
    OSMR1 = OSCR + TOGGLE_TICK; /* set initial match */

    setup_timer(&mycounter, counter_callback, 0);
    mod_timer(&mycounter, jiffies + msecs_to_jiffies(1000));

    printk(KERN_ALERT "Installed PWM Servo");
    return 0;
fail: 
	pwmservo_exit(); 
	return 0;
}


static void pwmservo_exit(void) {
    /* Free memory */
    del_timer(&mycounter);	
    unregister_chrdev(PWMSERVO_MAJOR, "pwmservo");

    pxa_set_cken(CKEN0_PWM0,0); //Disable the PWM0 Clock

    OIER = OIER & (~OIER_E1); /* disable match on timer match 1 to cause interrupts */
    free_irq(IRQ_OST1, NULL);
	printk(KERN_ALERT "Removing pwmservo module\n");
}

/** Implement file operators **/
static int pwmservo_open(struct inode *inode, struct file *filp)
{
    //printk("Servo file opened\n");
    return 0;
}

static int pwmservo_release(struct inode *inode, struct file *filp)
{
    //printk("Servo file released\n");
    return 0;
}

static ssize_t pwmservo_read( struct file *filp, char *buf, size_t count, 
                            loff_t *f_pos )
{
   	char msg[512];
	char *tbuf = msg;

	if (flag == 0){
		flag = 1;
		return 0;
	}

	switch (res)
	{
		case 110:
			sprintf(tbuf, "0");
			break;
		case 170:
			sprintf(tbuf, "30");
			break;
		case 230:
			sprintf(tbuf, "60");
			break;
		case 290:
			sprintf(tbuf, "90");
			break;
		case 350:
			sprintf(tbuf, "120");
			break;
		case 410:
			sprintf(tbuf, "150");
			break;
		case 470:
			sprintf(tbuf, "180");
			break;
	}
	count = strlen(tbuf);
	count = count + 1;

	//sprintf(tbuf, "Degree at 180");
	
	if (copy_to_user(buf, msg + *f_pos , count))
		{	
			return -EFAULT;
		}
	if (flag == 1){
		flag = 0;
	}
	return count;
}

static ssize_t pwmservo_write(struct file *filp, const char *buf, size_t count,
                            loff_t *f_pos)
{
    return 0;
}
void counter_callback(unsigned long data){
	if (direction == 1)
	{
		res = res + 60; // 60 corresponding to 30 degree
		servo_val0 = res;
		if (res == 470)
		{
			direction = 0;		
		}
	}
	else
	{
		res = res - 60;
		servo_val0 = res;
		if (res == 110)
		{
			direction = 1;
		}		
	}
	mod_timer(&mycounter, jiffies + msecs_to_jiffies(1000));
}

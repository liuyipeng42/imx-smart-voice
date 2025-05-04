#include <asm/io.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/ide.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/semaphore.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/wait.h>

#define DEV_CNT 1       /* 设备号长度 	*/
#define DEV_NAME "key0" /* 设备名字 	*/

/* 中断IO描述结构体 */
struct key_irq {
    int gpio;                           /* gpio */
    int irqnum;                         /* 中断号     */
    char name[10];                      /* 名字 */
    irqreturn_t (*handler)(int, void*); /* 中断服务函数 */
};

/* key_dev设备结构体 */
struct key_device {
    dev_t devid;               /* 设备号 	 */
    struct cdev cdev;          /* cdev 	*/
    struct class* class;       /* 类 		*/
    struct device* device;     /* 设备 	 */
    int major;                 /* 主设备号	  */
    int minor;                 /* 次设备号   */
    struct device_node* dnode; /* 设备节点 */
    atomic_t key_status;       /* 标记是否完成一次完成的按键，包括按下和释放 */
    struct timer_list timer;   /* 定义一个定时器*/
    struct key_irq key_irq;    /* 按键init述数组 */

    wait_queue_head_t r_wait; /* 读等待队列头 */
};

struct key_device key_dev; /* irq设备 */

static irqreturn_t key0_handler(int irq, void* dev_id) {
    struct key_device* dev = (struct key_device*)dev_id;

    dev->timer.data = (volatile long)dev_id;
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(10)); /* 10ms定时 */
    return IRQ_RETVAL(IRQ_HANDLED);
}

void timer_function(unsigned long arg) {
    unsigned char value;
    struct key_irq* key_irq;
    struct key_device* dev = (struct key_device*)arg;

    key_irq = &dev->key_irq;

    value = gpio_get_value(key_irq->gpio);  // 读取IO值
    if (value == 0) {                       // 按下按键
        atomic_set(&dev->key_status, 1);
    } else {                              // 按键松开
        atomic_set(&dev->key_status, 2);  // 标记松开按键，即完成一次完整的按键过程
    }

    // 唤醒进程
    if (atomic_read(&dev->key_status)) {  // 完成一次按键过程
        // wake_up(&dev->r_wait);
        wake_up_interruptible(&dev->r_wait);
    }
}

static int key_open(struct inode* inode, struct file* filp) {
    filp->private_data = &key_dev; /* 设置私有数据  */
    return 0;
}

static ssize_t key_read(struct file* filp, char __user* buf, size_t cnt, loff_t* offt) {
    int ret = 0;
    unsigned char key_status = 0;
    struct key_device* dev = (struct key_device*)filp->private_data;

    DECLARE_WAITQUEUE(wait, current);            /* 定义一个等待队列 */
    if (atomic_read(&dev->key_status) == 0) {    /* 没有按键按下 */
        add_wait_queue(&dev->r_wait, &wait);     /* 将等待队列添加到等待队列头 */
        __set_current_state(TASK_INTERRUPTIBLE); /* 设置任务状态 */
        schedule();                              /* 进行一次任务切换 */
        if (signal_pending(current)) {           /* 判断是否为信号引起的唤醒 */
            ret = -ERESTARTSYS;
            goto wait_error;
        }
        __set_current_state(TASK_RUNNING);      /* 将当前任务设置为运行状态 */
        remove_wait_queue(&dev->r_wait, &wait); /* 将对应的队列项从等待队列头删除 */
    }

    key_status = atomic_read(&dev->key_status);

    if (key_status != 0) {     /* 有按键按下 */
        if (key_status == 1) { /* 按键按下 */
            ret = copy_to_user(buf, &key_status, sizeof(key_status));
        } else if (key_status == 2) { /* 按键松开 */
            // keyvalue |= 0x80;         /* 设置高位 */
            ret = copy_to_user(buf, &key_status, sizeof(key_status));
        }
        atomic_set(&dev->key_status, 0); /* 按下标志清零 */
    } else {
        goto data_error;
    }
    return 0;

wait_error:
    set_current_state(TASK_RUNNING);        /* 设置任务为运行态 */
    remove_wait_queue(&dev->r_wait, &wait); /* 将等待队列移除 */
    return ret;

data_error:
    return -EINVAL;
}

/* 设备操作函数 */
static struct file_operations key_fops = {
    .owner = THIS_MODULE,
    .open = key_open,
    .read = key_read,
};

static int key_probe(struct platform_device* dev) {
    char name[10];
    int ret;

    printk("key driver and device was matched!\r\n");
    /* 1、设置设备号 */
    if (key_dev.major) {
        key_dev.devid = MKDEV(key_dev.major, 0);
        register_chrdev_region(key_dev.devid, DEV_CNT, DEV_NAME);
    } else {
        alloc_chrdev_region(&key_dev.devid, 0, DEV_CNT, DEV_NAME);
        key_dev.major = MAJOR(key_dev.devid);
        key_dev.minor = MINOR(key_dev.devid);
    }

    /* 2、注册设备      */
    cdev_init(&key_dev.cdev, &key_fops);
    cdev_add(&key_dev.cdev, key_dev.devid, DEV_CNT);

    /* 3、创建类      */
    // /sys/class/DEV_NAME
    key_dev.class = class_create(THIS_MODULE, DEV_NAME);
    if (IS_ERR(key_dev.class)) {
        return PTR_ERR(key_dev.class);
    }

    /* 4、创建设备 */
    // /dev/DEV_NAME
    key_dev.device = device_create(key_dev.class, NULL, key_dev.devid, NULL, DEV_NAME);
    if (IS_ERR(key_dev.device)) {
        return PTR_ERR(key_dev.device);
    }

    atomic_set(&key_dev.key_status, 0);

    /* 5、初始化IO */
    key_dev.dnode = of_find_node_by_path("/key");
    if (key_dev.dnode == NULL) {
        printk("gpiokey node nost find!\r\n");
        return -EINVAL;
    }

    key_dev.key_irq.gpio = of_get_named_gpio(key_dev.dnode, "key-gpio", 0);
    if (key_dev.key_irq.gpio < 0) {
        printk("can't get key-gpio\r\n");
        return -EINVAL;
    }

    /* 初始化key所使用的IO，并且设置成中断模式 */
    memset(key_dev.key_irq.name, 0, sizeof(name)); /* 缓冲区清零 */
    sprintf(key_dev.key_irq.name, "KEY0");         /* 组合名字 */
    gpio_request(key_dev.key_irq.gpio, name);
    gpio_direction_input(key_dev.key_irq.gpio);
    key_dev.key_irq.irqnum = irq_of_parse_and_map(key_dev.dnode, 0);

    /* 申请中断 */
    key_dev.key_irq.handler = key0_handler;

    ret = request_irq(key_dev.key_irq.irqnum, key_dev.key_irq.handler,
                      IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, key_dev.key_irq.name, &key_dev);
    if (ret < 0) {
        printk("irq %d request failed!\r\n", key_dev.key_irq.irqnum);
        return -EFAULT;
    }

    /* 创建定时器 */
    init_timer(&key_dev.timer);
    key_dev.timer.function = timer_function;

    /* 初始化等待队列头 */
    init_waitqueue_head(&key_dev.r_wait);

    printk("key init finished\r\n");
    return 0;
}

static int key_remove(struct platform_device* dev) {
    /* 删除定时器 */
    del_timer_sync(&key_dev.timer); /* 删除定时器 */

    /* 释放中断 */
    free_irq(key_dev.key_irq.irqnum, &key_dev);
    gpio_free(key_dev.key_irq.gpio);
    cdev_del(&key_dev.cdev);
    unregister_chrdev_region(key_dev.devid, DEV_CNT);
    device_destroy(key_dev.class, key_dev.devid);
    class_destroy(key_dev.class);
    return 0;
}

/* 匹配列表 */
static const struct of_device_id key_of_match[] = {{.compatible = "atkalpha-key"}, {/* Sentinel */}};

/* platform驱动结构体 */
static struct platform_driver key_driver = {
    .driver =
        {
            .name = "platform-key",         /* 驱动名字，用于和设备匹配 */
            .of_match_table = key_of_match, /* 设备树匹配表 		  */
        },
    .probe = key_probe,
    .remove = key_remove,
};

static int __init keydriver_init(void) {
    return platform_driver_register(&key_driver);
}

static void __exit keydriver_exit(void) {
    platform_driver_unregister(&key_driver);
}

module_init(keydriver_init);
module_exit(keydriver_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("LYP");

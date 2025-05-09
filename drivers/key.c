#include <asm/atomic.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define DEV_CNT 1
#define DEV_NAME "key"

struct key_device {
    dev_t devid;
    struct cdev cdev;
    struct class* class;
    struct device* device;
    struct gpio_desc* key_gpio;

    int irq;
    struct timer_list timer;

    wait_queue_head_t wait_queue;
    atomic_t key_status;
};

static struct key_device key_dev;

static irqreturn_t key_handler(int irq, void* dev_id) {
    mod_timer(&key_dev.timer, jiffies + msecs_to_jiffies(10));
    return IRQ_RETVAL(IRQ_HANDLED);
}

void timeout_handler(unsigned long arg) {
    if (gpiod_get_value(key_dev.key_gpio))
        atomic_set(&key_dev.key_status, 1);
    else
        atomic_set(&key_dev.key_status, 2);

    if (atomic_read(&key_dev.key_status))
        wake_up_interruptible(&key_dev.wait_queue);
}

static ssize_t key_read(struct file* filp, char __user* buf, size_t cnt, loff_t* offt) {
    int ret = 0;
    unsigned char key_status = 0;

    DECLARE_WAITQUEUE(wait, current);
    if (atomic_read(&key_dev.key_status) == 0) {
        add_wait_queue(&key_dev.wait_queue, &wait);
        __set_current_state(TASK_INTERRUPTIBLE);
        schedule();
        if (signal_pending(current)) {
            ret = -ERESTARTSYS;
            goto wait_error;
        }
        __set_current_state(TASK_RUNNING);
        remove_wait_queue(&key_dev.wait_queue, &wait);
    }

    key_status = atomic_read(&key_dev.key_status);

    if (key_status != 0) {
        if (copy_to_user(buf, &key_status, sizeof(key_status))) 
            ret = -EFAULT;
        atomic_set(&key_dev.key_status, 0);
    } else {
        ret = -EINVAL;
    }
    return ret;

wait_error:
    set_current_state(TASK_RUNNING);
    remove_wait_queue(&key_dev.wait_queue, &wait);
    return ret;
}

static struct file_operations key_fops = {
    .owner = THIS_MODULE,
    .read = key_read,
};

static int key_probe(struct platform_device* pdev) {
    int ret;

    alloc_chrdev_region(&key_dev.devid, 0, DEV_CNT, DEV_NAME);

    cdev_init(&key_dev.cdev, &key_fops);
    cdev_add(&key_dev.cdev, key_dev.devid, DEV_CNT);
    key_dev.class = class_create(THIS_MODULE, DEV_NAME);
    key_dev.device = device_create(key_dev.class, NULL, key_dev.devid, NULL, DEV_NAME);

    key_dev.key_gpio = devm_gpiod_get(&pdev->dev, DEV_NAME, GPIOD_IN);

    key_dev.irq = gpiod_to_irq(key_dev.key_gpio);

    ret = devm_request_irq(&pdev->dev, key_dev.irq, key_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                           "key_irq", NULL);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request IRQ\n");
        return ret;
    }

    setup_timer(&key_dev.timer, timeout_handler, 0);
    init_waitqueue_head(&key_dev.wait_queue);
    atomic_set(&key_dev.key_status, 0);

    return 0;
}

static int key_remove(struct platform_device* dev) {
    del_timer_sync(&key_dev.timer);

    device_destroy(key_dev.class, key_dev.devid);
    class_destroy(key_dev.class);
    cdev_del(&key_dev.cdev);

    unregister_chrdev_region(key_dev.devid, DEV_CNT);
    return 0;
}

static const struct of_device_id key_of_match[] = {{.compatible = "key"}, {}};

static struct platform_driver key_platform_driver = {
    .driver =
        {
            .name = "key",
            .of_match_table = key_of_match,
        },
    .probe = key_probe,
    .remove = key_remove,
};

module_platform_driver(key_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LYP");

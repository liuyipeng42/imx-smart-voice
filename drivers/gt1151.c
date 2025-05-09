#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define GT_CTRL_REG 0X8040  /* GT1151控制寄存器         */
#define GT_MODSW_REG 0X804D /* GT1151模式切换寄存器        */
#define GT_CFGS_REG 0X8047  /* GT1151配置起始地址寄存器    */
#define GT_CHECK_REG 0X80FF /* GT1151校验和寄存器       */
#define GT_PID_REG 0X8140   /* GT1151产品ID寄存器       */

#define GT_GSTID_REG 0X814E  /* GT1151当前检测到的触摸情况 */
#define GT_TP1_REG 0X814F    /* 第一个触摸点数据地址 */
#define GT_TP2_REG 0X8157    /* 第二个触摸点数据地址 */
#define GT_TP3_REG 0X815F    /* 第三个触摸点数据地址 */
#define GT_TP4_REG 0X8167    /* 第四个触摸点数据地址  */
#define GT_TP5_REG 0X816F    /* 第五个触摸点数据地址   */
#define MAX_SUPPORT_POINTS 5 /* 最多5点电容触摸 */

struct gt1151_dev {
    struct gpio_desc* reset_gpio;
    struct gpio_desc* irq_gpio;
    int irqnum;
    void* private_data;
    struct input_dev* input;
    struct i2c_client* client;
};

struct gt1151_dev gt1151;

const unsigned char GT1151_CT[] = {
    0x48, 0xe0, 0x01, 0x10, 0x01, 0x05, 0x0d, 0x00, 0x01, 0x08, 0x28, 0x05, 0x50, 0x32, 0x03, 0x05, 0x00,
    0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x89, 0x28, 0x0a, 0x17, 0x15, 0x31, 0x0d,
    0x00, 0x00, 0x02, 0x9b, 0x03, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x00,
    0x0f, 0x94, 0x94, 0xc5, 0x02, 0x07, 0x00, 0x00, 0x04, 0x8d, 0x13, 0x00, 0x5c, 0x1e, 0x00, 0x3c, 0x30,
    0x00, 0x29, 0x4c, 0x00, 0x1e, 0x78, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0a, 0x0c, 0x0e, 0x10, 0x12, 0x14,
    0x16, 0x18, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x02, 0x04, 0x05, 0x06, 0x08, 0x0a, 0x0c, 0x0e, 0x1d, 0x1e,
    0x1f, 0x20, 0x22, 0x24, 0x28, 0x29, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static int gt1151_read_regs(struct gt1151_dev* dev, u16 reg, u8* buf, int len) {
    int ret;
    u8 regdata[2];
    struct i2c_msg msg[2];
    struct i2c_client* client = (struct i2c_client*)dev->client;

    /* GT1151寄存器长度为2个字节 */
    regdata[0] = reg >> 8;
    regdata[1] = reg & 0xFF;

    /* msg[0]为发送要读取的首地址 */
    msg[0].addr = client->addr; /* ft5x06地址 */
    msg[0].flags = !I2C_M_RD;   /* 标记为发送数据 */
    msg[0].buf = &regdata[0];   /* 读取的首地址 */
    msg[0].len = 2;             /* reg长度*/

    /* msg[1]读取数据 */
    msg[1].addr = client->addr; /* ft5x06地址 */
    msg[1].flags = I2C_M_RD;    /* 标记为读取数据*/
    msg[1].buf = buf;           /* 读取数据缓冲区 */
    msg[1].len = len;           /* 要读取的数据长度*/

    ret = i2c_transfer(client->adapter, msg, 2);
    if (ret == 2) {
        ret = 0;
    } else {
        ret = -EREMOTEIO;
    }
    return ret;
}

static s32 gt1151_write_regs(struct gt1151_dev* dev, u16 reg, u8* buf, u8 len) {
    u8 b[256];
    struct i2c_msg msg;
    struct i2c_client* client = (struct i2c_client*)dev->client;

    b[0] = reg >> 8;         /* 寄存器首地址低8位 */
    b[1] = reg & 0XFF;       /* 寄存器首地址高8位 */
    memcpy(&b[2], buf, len); /* 将要写入的数据拷贝到数组b里面 */

    msg.addr = client->addr; /* gt1151地址 */
    msg.flags = 0;           /* 标记为写数据 */

    msg.buf = b;       /* 要写入的数据缓冲区 */
    msg.len = len + 2; /* 要写入的数据长度 */

    return i2c_transfer(client->adapter, &msg, 1);
}

static irqreturn_t gt1151_irq_handler(int irq, void* dev_id) {
    int touch_num = 0;
    int input_x, input_y;
    int id = 0;
    int ret = 0;
    u8 data;
    u8 touch_data[5];
    struct gt1151_dev* dev = dev_id;

    ret = gt1151_read_regs(dev, GT_GSTID_REG, &data, 1);
    if (data == 0x00) { /* 没有触摸数据，直接返回 */
        goto fail;
    } else { /* 统计触摸点数据 */
        touch_num = data & 0x0f;
    }

    /* 由于GT1151没有硬件检测每个触摸点按下和抬起，因此每个触摸点的抬起和按
     * 下不好处理，尝试过一些方法，但是效果都不好，因此这里暂时使用单点触摸
     */
    if (touch_num) { /* 单点触摸按下 */
        gt1151_read_regs(dev, GT_TP1_REG, touch_data, 5);
        id = touch_data[0] & 0x0F;
        if (id == 0) {
            input_x = touch_data[1] | (touch_data[2] << 8);
            input_y = touch_data[3] | (touch_data[4] << 8);

            input_mt_slot(dev->input, id);
            input_mt_report_slot_state(dev->input, MT_TOOL_FINGER, true);
            input_report_abs(dev->input, ABS_MT_POSITION_X, input_x);
            input_report_abs(dev->input, ABS_MT_POSITION_Y, input_y);
        }
    } else if (touch_num == 0) { /* 单点触摸释放 */
        input_mt_slot(dev->input, id);
        input_mt_report_slot_state(dev->input, MT_TOOL_FINGER, false);
    }

    input_mt_report_pointer_emulation(dev->input, true);
    input_sync(dev->input);

    data = 0x00; /* 向0X814E寄存器写0 */
    gt1151_write_regs(dev, GT_GSTID_REG, &data, 1);

fail:
    return IRQ_HANDLED;
}

int gt1151_probe(struct i2c_client* client, const struct i2c_device_id* id) {
    u8 data;
    int ret;
    gt1151.client = client;

    gt1151.reset_gpio = devm_gpiod_get_optional(&client->dev, "reset", GPIOD_OUT_HIGH);
    gt1151.irq_gpio = devm_gpiod_get_optional(&client->dev, "interrupt", GPIOD_IN);

    // 初始化GT1151时序
    gpiod_set_value(gt1151.reset_gpio, 1);  // 复位
    msleep(10);
    gpiod_set_value(gt1151.reset_gpio, 0);  // 结束复位
    msleep(10);
    // 配置中断引脚
    gpiod_set_value(gt1151.irq_gpio, 1);  // 拉低
    msleep(50);
    gpiod_direction_input(gt1151.irq_gpio);  // 设为输入

    // 软复位GT1151
    data = 0x02;
    gt1151_write_regs(&gt1151, GT_CTRL_REG, &data, 1);
    msleep(100);
    data = 0x00;
    gt1151_write_regs(&gt1151, GT_CTRL_REG, &data, 1);
    msleep(100);

    // 初始化输入设备
    gt1151.input = devm_input_allocate_device(&client->dev);
    if (!gt1151.input)
        return -ENOMEM;

    gt1151.input->name = client->name;
    gt1151.input->id.bustype = BUS_I2C;
    gt1151.input->dev.parent = &client->dev;
    input_set_capability(gt1151.input, EV_KEY, BTN_TOUCH);
    input_set_abs_params(gt1151.input, ABS_MT_POSITION_X, 0, 480, 0, 0);
    input_set_abs_params(gt1151.input, ABS_MT_POSITION_Y, 0, 272, 0, 0);

    input_mt_init_slots(gt1151.input, MAX_SUPPORT_POINTS, 0);
    ret = input_register_device(gt1151.input);
    if (ret) {
        dev_err(&client->dev, "Unable to register input device.\n");
        return ret;
    }

    ret = devm_request_threaded_irq(&client->dev, client->irq, NULL, gt1151_irq_handler,
                                    IRQF_TRIGGER_FALLING | IRQF_ONESHOT, client->name, &gt1151);
    if (ret) {
        dev_err(&client->dev, "Unable to request touchscreen IRQ.\n");
        return ret;
    }
    return 0;
}

int gt1151_remove(struct i2c_client* client) {
    input_unregister_device(gt1151.input);
    return 0;
}

const struct i2c_device_id gt1151_id_table[] = {{
                                                    "goodix,gt1151",
                                                    0,
                                                },
                                                {}};

const struct of_device_id gt1151_of_match_table[] = {{.compatible = "goodix,gt1151"}, {}};

struct i2c_driver gt1151_i2c_driver = {
    .driver =
        {
            .name = "gt1151",
            .owner = THIS_MODULE,
            .of_match_table = gt1151_of_match_table,
        },
    .id_table = gt1151_id_table,
    .probe = gt1151_probe,
    .remove = gt1151_remove,
};

module_i2c_driver(gt1151_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LYP");

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net_buf.h>
#include <zephyr/toolchain.h>
#include <zephyr/devicetree.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>

#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/assigned_numbers.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_core.h>

LOG_MODULE_REGISTER(receiver_main, LOG_LEVEL_INF);

static double decode_double_le(const uint8_t *in);
static bool ad_parse_cb(struct bt_data *data, void *user_data);
static void scan_cb (const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type, struct net_buf_simple *buf);

const struct device *const usbdevce = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

#define SENS_ADDR_STR "de:ad:be:ef:00:01"
#define MFG_ID 0xffff
// company id + 2x uint64
#define MFG_DATA_LEN (2 + 8 + 8)

int main(void)
{
    int err;
    uint32_t dtr = 0;
    bt_addr_le_t sens_addr;


    while (!dtr) {
        uart_line_ctrl_get(usbdevce, UART_LINE_CTRL_DTR, &dtr);
        k_sleep(K_MSEC(100));
    }

    LOG_INF("USB setup successfully!");

    if ((err = bt_enable(NULL)))
    {
        LOG_ERR("Bluetooth init failed! (err: %d)", err);
        return -1;
    }

    // Create accept list
    if ((err = bt_addr_le_from_str(SENS_ADDR_STR, "random", &sens_addr)))
    {
        LOG_ERR("Invalid address string! (err: %d)", err);
        return -2;
    }
    if ((err = bt_le_filter_accept_list_add(&sens_addr)))
    {
        LOG_ERR("Failed to add to accept list! (err: %d)", err);
        return -1;
    }


    // setup passive scan
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_PASSIVE,
        .options = BT_LE_SCAN_OPT_FILTER_ACCEPT_LIST,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW
    };

    if ((err = bt_le_scan_start(&scan_param, scan_cb)))
    {
        LOG_ERR("Failed to start bluetooth scanning! (err: %d)", err);
        return -1;
    }

    LOG_INF("Bluetooth scanning started!");

}


static void scan_cb (const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type, struct net_buf_simple *buf)
{
    ARG_UNUSED(addr);
    ARG_UNUSED(rssi);
    ARG_UNUSED(adv_type);

    bt_data_parse(buf, ad_parse_cb, NULL);
}

static bool ad_parse_cb(struct bt_data *data, void *user_data)
{
    uint16_t company_id;
    double temp, hum;

    if (data->type != BT_DATA_MANUFACTURER_DATA)
    {
        return true;
    }

    if (data->data_len != MFG_DATA_LEN)
    {
        LOG_WRN("Data len did not match! (l: %d))", data->data_len);
        return true;
    }

    company_id = sys_get_le16(&data->data[0]);
    if (company_id != MFG_ID)
    {
        LOG_WRN("Wrong MFR ID! (id: %04x)", company_id);
        return true;
    }

    temp = decode_double_le(&data->data[2]);
    hum = decode_double_le(&data->data[10]);

    printk("%0.2f %0.2f\n", temp, hum);
    return true;
}

static double decode_double_le(const uint8_t *in)
{
    uint64_t bits = sys_get_le64(in);
    double val;

    memcpy(&val, &bits, sizeof(val));
    return val;
}
 

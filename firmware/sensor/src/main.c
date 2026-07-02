#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/sensor.h>

#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/assigned_numbers.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_core.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static void poll_sensor();
static void get_sensor_update();
static void encode_double_le(double val, uint8_t *out);

const struct device *const sth_sensor = DEVICE_DT_GET(DT_NODELABEL(sht3x));

struct sensor_value temp, hum;

/* 0xFFFF is reserved by the Bluetooth SIG for internal/test use, not a real
 * vendor ID. Fine here since this is a private link and nothing else needs
 * to identify the packet by company ID. Swap for a real ID if that changes. */
#define MFG_ID 0xFFFF

/* company id (2 bytes) + 2x double (16 bytes) */
#define MFG_DATA_LEN (2 + 16)
static uint8_t mfg_data[MFG_DATA_LEN];

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, sizeof(mfg_data)),
};
 

int main (void)
{
    int err;
    bt_addr_le_t addr;

    if (!(err = device_is_ready(sth_sensor))) {
        LOG_ERR("Device %s is not ready! (err: %d)", sth_sensor->name, err);
        return -1;
    }


    if ((err = bt_addr_le_from_str("de:ad:be:ef:00:01", "random", &addr)))
    {
        LOG_ERR("Invalid address string! (err: %d)", err);
        return -2;
    }

    if ((err = bt_id_create(&addr, NULL)) < 0)
    {
        LOG_ERR("Creating bt identity failed! (err: %d))", err);
        return -1;
    }

    get_sensor_update();

    if ((err = bt_enable(NULL)) != 0)
    {
        LOG_ERR("Failed to start bt advertising! (err: %d)", err);
        return -1;
    }

    err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_USE_IDENTITY,
                                 BT_GAP_ADV_FAST_INT_MIN_2,
                                 BT_GAP_ADV_FAST_INT_MAX_2,
                                 NULL),
                 ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start! (err %d)", err);
        return 0;
    }

    LOG_INF("Broadcasting started!");

    while (true)
    {
        k_sleep(K_SECONDS(1));

        get_sensor_update();

        if ((err = bt_le_adv_update_data(ad, ARRAY_SIZE(ad), NULL, 0)) != 0)
            LOG_ERR("Failed to update advertising packet! (err: %d)", err);

        LOG_INF("Temp: %.2f  | HUM: %0.2f%% RH",
                sensor_value_to_double(&temp),
                sensor_value_to_double(&hum));
    }
}


static void poll_sensor()
{
    int rc;
    rc = sensor_sample_fetch(sth_sensor);
    if (rc == 0)
    {
        rc = sensor_channel_get(sth_sensor, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    }
    if (rc == 0)
    {
        rc = sensor_channel_get(sth_sensor, SENSOR_CHAN_HUMIDITY, &hum);
    }
    if (rc != 0)
    {
        LOG_ERR("Failed to read sensor: %d", rc);
    }
}


/*
 * Bluetooth advertising packets don't have fancy features
 * such as doubles or floats. Instead these functions
 * encode them as a uint64_t.
 */
static void encode_double_le(double val, uint8_t *out)
{
    uint64_t bits;

    memcpy(&bits, &val, sizeof(bits));
    sys_put_le64(bits, out);
}

static void get_sensor_update()
{
    poll_sensor();

    sys_put_le16(MFG_ID, &mfg_data[0]);
    encode_double_le(sensor_value_to_double(&temp), &mfg_data[2]);
    encode_double_le(sensor_value_to_double(&hum), &mfg_data[10]);
}

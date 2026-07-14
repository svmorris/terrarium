#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/assigned_numbers.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_core.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);


#define TEMP_HIGH 26.0
#define TEMP_LOW  22.0
#define HUM_HIGH 60.0
#define HUM_LOW 50.0
#define HUM_EXTREME 80.0
#define TEMP_TOOLOW 16.0

static void poll_sensor();
static void check_run_hum();
static void get_sensor_update();
static void check_run_modules(double temp, double hum);
static void encode_double_le(double val, uint8_t *out);

#define TOGGLE_NODE DT_ALIAS(toggle_gpio)

const struct device *const sth_sensor = DEVICE_DT_GET(DT_NODELABEL(sht3x));
static const struct gpio_dt_spec mist_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(mist_pin), gpios);
static const struct gpio_dt_spec fan_pin = GPIO_DT_SPEC_GET(DT_NODELABEL(fan_pin), gpios);

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
 

int64_t last_hum = 0;

int main (void)
{
    int err;
    bt_addr_le_t addr;
    LOG_INF("5");
    k_sleep(K_SECONDS(1));

    LOG_INF("4");
    k_sleep(K_SECONDS(1));

    LOG_INF("3");
    k_sleep(K_SECONDS(1));

    LOG_INF("2");
    k_sleep(K_SECONDS(1));

    LOG_INF("1");
    k_sleep(K_SECONDS(1));

    if (!(err = device_is_ready(sth_sensor))) {
        LOG_ERR("Device %s is not ready! (err: %d)", sth_sensor->name, err);
        return -1;
    }

    // gpt
    if (!gpio_is_ready_dt(&mist_pin)) {
        LOG_ERR("gpio0 device not ready");
        return 0;
    }
    err = gpio_pin_configure_dt(&mist_pin, GPIO_OUTPUT_INACTIVE);
    if (err < 0) {
        LOG_ERR("Failed to configure pin (err %d)", err);
        return 0;
    }

    err = gpio_pin_configure_dt(&fan_pin, GPIO_OUTPUT_INACTIVE);
    if (err < 0) {
        LOG_ERR("Failed to configure pin (err %d)", err);
        return 0;
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

        // This is all temporary code that should be redone
        double dhum = sensor_value_to_double(&hum);
        double dtemp = sensor_value_to_double(&temp)-2;

        // temp doesn't seem to be nearly accurate
        check_run_modules(dtemp, dhum);

        LOG_INF("Temp: %.2f  | HUM: %0.2f%% RH", dtemp, dhum);
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

static void check_run_modules(double temp, double hum)
{
    // temp
    if (temp > TEMP_HIGH)
    {
        LOG_INF("Starting fan (reason: too hot)");
        gpio_pin_set_dt(&fan_pin, 1);
    }
    
    // Unless the humidity is way too high
    // the fan should be turned off when the
    // temp is cold
    if (temp < TEMP_LOW && hum < HUM_EXTREME)
    {
        LOG_INF("Stopping fan (reason: temp low)");
        gpio_pin_set_dt(&fan_pin, 0);
    }


    // Humidity
    // If humidity was run within the last 10 seconds
    // then we can't let it run again. The foam needs
    // time to re-saturate
    if (hum < HUM_LOW && temp > TEMP_TOOLOW)
    {
        check_run_hum();
    }

    // never run the fan if its too cold already
    if (hum > HUM_HIGH && temp > TEMP_LOW)
    {
        LOG_INF("Starting fan (reason: humidity high)");
        gpio_pin_set_dt(&fan_pin, 1);
    }

    if (hum > HUM_EXTREME)
    {
        LOG_INF("Starting fan (reason: humidity extreme)");
        gpio_pin_set_dt(&fan_pin, 1);
    }
}



static void check_run_hum()
{
    int64_t uptime = k_uptime_get() / 1000;
    if ((uptime - last_hum) > 20)
    {
        LOG_INF("Starting humidity");
        gpio_pin_set_dt(&mist_pin, 1);
        k_sleep(K_SECONDS(15));
        gpio_pin_set_dt(&mist_pin, 0);
        LOG_INF("Stopped humidity");
        last_hum = uptime;
    }
}

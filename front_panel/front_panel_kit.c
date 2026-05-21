#include "front_panel_kit.h"

#include "board_config.h"
#include "cpu_state.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "virtual_monitor.h"

#if CONFIG_ALTAIR_BOARD_LONELY_BINARY_ALTAIR_KIT

#define KIT_PIN_SWITCH_LOAD GPIO_NUM_42
#define KIT_PIN_SWITCH_CS GPIO_NUM_9
#define KIT_PIN_MISO GPIO_NUM_5
#define KIT_PIN_MOSI GPIO_NUM_4
#define KIT_PIN_RESET GPIO_NUM_39
#define KIT_PIN_CLK GPIO_NUM_1
#define KIT_PIN_LED_STORE GPIO_NUM_40
#define KIT_PIN_LED_OE GPIO_NUM_8

#define KIT_SPI_HOST SPI3_HOST
#define KIT_SPI_CLOCK_HZ (20 * 1000 * 1000)
#define KIT_STROBE_DELAY_US 1
#define KIT_SWITCH_DEBOUNCE_US (50 * 1000)

#define KIT_CMD_RUN_BIT 0x01
#define KIT_CMD_STOP_BIT 0x02
#define KIT_CMD_SINGLE_STEP_BIT 0x08
#define KIT_CMD_EXAMINE_NEXT_BIT 0x10
#define KIT_CMD_EXAMINE_BIT 0x20
#define KIT_CMD_DEPOSIT_NEXT_BIT 0x40
#define KIT_CMD_DEPOSIT_BIT 0x80

static const char *TAG = "FrontPanelKit";
static bool s_initialized;
static spi_device_handle_t s_spi;
static uint8_t s_last_status;
static uint8_t s_last_data;
static uint16_t s_last_address;
static uint8_t s_last_cmd_sample;
static uint8_t s_debounced_cmd;
static int64_t s_cmd_changed_us;
static uint16_t s_last_switch_sample;
static uint16_t s_debounced_switches;
static int64_t s_switches_changed_us;
static volatile uint8_t s_pending_command;
static const uint8_t s_reverse_nibble[16] = {
    0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
    0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf,
};

static uint8_t reverse_byte(uint8_t value)
{
    return (uint8_t)((s_reverse_nibble[value & 0x0f] << 4) |
                     s_reverse_nibble[(value >> 4) & 0x0f]);
}

static uint16_t reverse_word(uint16_t value)
{
    return (uint16_t)(reverse_byte((uint8_t)(value & 0xff)) |
                      ((uint16_t)reverse_byte((uint8_t)(value >> 8)) << 8));
}

static void kit_set_idle_levels(void)
{
    gpio_set_level(KIT_PIN_SWITCH_LOAD, 1);
    gpio_set_level(KIT_PIN_SWITCH_CS, 1);
    gpio_set_level(KIT_PIN_RESET, 1);
    gpio_set_level(KIT_PIN_LED_STORE, 1);
    gpio_set_level(KIT_PIN_LED_OE, 0);
}

static esp_err_t configure_output(gpio_num_t pin)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}

static bool configure_pin(esp_err_t err, const char *name)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s GPIO config failed: %s", name, esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool kit_spi_init(void)
{
    spi_bus_config_t bus_config = {
        .mosi_io_num = KIT_PIN_MOSI,
        .miso_io_num = KIT_PIN_MISO,
        .sclk_io_num = KIT_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4,
    };
    esp_err_t err = spi_bus_initialize(KIT_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return false;
    }

    spi_device_interface_config_t device_config = {
        .clock_speed_hz = KIT_SPI_CLOCK_HZ,
        .mode = 2,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    err = spi_bus_add_device(KIT_SPI_HOST, &device_config, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI device init failed: %s", esp_err_to_name(err));
        spi_bus_free(KIT_SPI_HOST);
        return false;
    }

    return true;
}

static bool kit_transfer_panel(uint8_t status, uint8_t data, uint16_t address,
                               bool update_leds, uint8_t *cmd_switches,
                               uint16_t *input_switches)
{
    uint8_t tx[4] = {
        reverse_byte((uint8_t)(address & 0xff)),
        reverse_byte((uint8_t)(address >> 8)),
        reverse_byte(data),
        reverse_byte(status),
    };
    uint8_t rx[4] = {0};
    spi_transaction_t transaction = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };

    if (update_leds) {
        gpio_set_level(KIT_PIN_LED_STORE, 0);
    }
    gpio_set_level(KIT_PIN_SWITCH_CS, 0);
    gpio_set_level(KIT_PIN_SWITCH_LOAD, 0);
    esp_rom_delay_us(KIT_STROBE_DELAY_US);
    gpio_set_level(KIT_PIN_SWITCH_LOAD, 1);

    esp_err_t err = spi_device_transmit(s_spi, &transaction);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI panel transfer failed: %s", esp_err_to_name(err));
    }

    gpio_set_level(KIT_PIN_SWITCH_CS, 1);
    if (update_leds) {
        gpio_set_level(KIT_PIN_LED_STORE, 1);
    }

    if (err != ESP_OK) {
        return false;
    }

    if (cmd_switches) {
        *cmd_switches = rx[2];
    }
    if (input_switches) {
        uint16_t raw_switches = (uint16_t)rx[0] | ((uint16_t)rx[1] << 8);
        *input_switches = (uint16_t)~reverse_word(raw_switches);
    }

    return true;
}

static void kit_write_leds(uint8_t status, uint8_t data, uint16_t address)
{
    (void)kit_transfer_panel(status, data, address, true, NULL, NULL);
}

static uint8_t map_command_switch(uint8_t cmd_bits)
{
    if (cmd_bits & KIT_CMD_STOP_BIT) {
        return STOP_CMD;
    }

    if (cpu_state_get_mode() != CPU_STOPPED) {
        return NOP;
    }

    if (cmd_bits & KIT_CMD_RUN_BIT) {
        return RUN_CMD;
    }
    if (cmd_bits & KIT_CMD_SINGLE_STEP_BIT) {
        return SINGLE_STEP;
    }
    if (cmd_bits & KIT_CMD_EXAMINE_BIT) {
        return EXAMINE;
    }
    if (cmd_bits & KIT_CMD_EXAMINE_NEXT_BIT) {
        return EXAMINE_NEXT;
    }
    if (cmd_bits & KIT_CMD_DEPOSIT_BIT) {
        return DEPOSIT;
    }
    if (cmd_bits & KIT_CMD_DEPOSIT_NEXT_BIT) {
        return DEPOSIT_NEXT;
    }

    return NOP;
}

static void queue_command(uint8_t command)
{
    if (command != NOP && s_pending_command == NOP) {
        if (cpu_state_get_mode() == CPU_STOPPED &&
            (command == EXAMINE || command == DEPOSIT || command == DEPOSIT_NEXT)) {
            bus_switches = s_debounced_switches;
        }
        s_pending_command = command;
    }
}

static void update_debounced_switches(uint8_t cmd_bits, uint16_t input_switches)
{
    int64_t now_us = esp_timer_get_time();

    if (cmd_bits != s_last_cmd_sample) {
        s_last_cmd_sample = cmd_bits;
        s_cmd_changed_us = now_us;
    } else if (cmd_bits != s_debounced_cmd &&
               (now_us - s_cmd_changed_us) >= KIT_SWITCH_DEBOUNCE_US) {
        s_debounced_cmd = cmd_bits;
        queue_command(map_command_switch(s_debounced_cmd));
    }

    if (input_switches != s_last_switch_sample) {
        s_last_switch_sample = input_switches;
        s_switches_changed_us = now_us;
    } else if (input_switches != s_debounced_switches &&
               (now_us - s_switches_changed_us) >= KIT_SWITCH_DEBOUNCE_US) {
        s_debounced_switches = input_switches;
        if (cpu_state_get_mode() == CPU_STOPPED) {
            bus_switches = s_debounced_switches;
        }
    }
}

bool front_panel_kit_init(void)
{
    if (s_initialized) {
        return true;
    }

    if (!configure_pin(configure_output(KIT_PIN_SWITCH_LOAD), "switch load") ||
        !configure_pin(configure_output(KIT_PIN_SWITCH_CS), "switch CS") ||
        !configure_pin(configure_output(KIT_PIN_RESET), "reset") ||
        !configure_pin(configure_output(KIT_PIN_LED_STORE), "LED store") ||
        !configure_pin(configure_output(KIT_PIN_LED_OE), "LED OE") ||
        !kit_spi_init()) {
        return false;
    }

    kit_set_idle_levels();

    s_last_status = 0;
    s_last_data = 0;
    s_last_address = 0;
    s_last_cmd_sample = 0;
    s_debounced_cmd = 0;
    s_cmd_changed_us = esp_timer_get_time();
    s_last_switch_sample = 0;
    s_debounced_switches = 0;
    s_switches_changed_us = s_cmd_changed_us;
    s_pending_command = NOP;
    s_initialized = true;

    kit_write_leds(0xff, 0xff, 0xffff);
    vTaskDelay(pdMS_TO_TICKS(250));
    kit_write_leds(0x00, 0x00, 0x0000);

    ESP_LOGI(TAG, "Lonely Binary front panel kit ready: SPI%d %d Hz mode 2, LOAD=%d CS=%d MISO=%d MOSI=%d RESET=%d CLK=%d STORE=%d OE=%d",
             KIT_SPI_HOST + 1, KIT_SPI_CLOCK_HZ,
             KIT_PIN_SWITCH_LOAD, KIT_PIN_SWITCH_CS, KIT_PIN_MISO, KIT_PIN_MOSI,
             KIT_PIN_RESET, KIT_PIN_CLK, KIT_PIN_LED_STORE, KIT_PIN_LED_OE);
    return true;
}

void front_panel_kit_update(const intel8080_t *cpu)
{
    if (!s_initialized || cpu == NULL) {
        return;
    }

    uint8_t status = cpu->cpuStatus;
    uint8_t data = cpu->data_bus;
    uint16_t address = cpu->address_bus;
    uint8_t cmd_bits = 0;
    uint16_t input_switches = 0;
    bool update_leds = status != s_last_status || data != s_last_data || address != s_last_address;

    if (!kit_transfer_panel(status, data, address, update_leds, &cmd_bits, &input_switches)) {
        return;
    }

    update_debounced_switches(cmd_bits, input_switches);

    if (update_leds) {
        s_last_status = status;
        s_last_data = data;
        s_last_address = address;
    }
}

uint8_t front_panel_kit_take_command(void)
{
    uint8_t command = s_pending_command;
    s_pending_command = NOP;
    return command;
}

void front_panel_kit_set_brightness(int brightness)
{
    (void)brightness;
}

#else

bool front_panel_kit_init(void)
{
    return true;
}

void front_panel_kit_update(const intel8080_t *cpu)
{
    (void)cpu;
}

uint8_t front_panel_kit_take_command(void)
{
    return 0;
}

void front_panel_kit_set_brightness(int brightness)
{
    (void)brightness;
}

#endif
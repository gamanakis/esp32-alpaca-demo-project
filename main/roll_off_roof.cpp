#include "roll_off_roof.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstring> // for strncpy

static const char *TAG = "RollOffRoof";


RollOffRoof::RollOffRoof() : SafetyMonitor()
{
  _connected = false;

  // Initialize TRIG pin
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_OUTPUT;
  io_conf.pin_bit_mask = (1ULL << TRIG_PIN);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
  gpio_config(&io_conf);
  gpio_set_level(TRIG_PIN, 0);

  // Initialize ECHO pin
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << ECHO_PIN);
  gpio_config(&io_conf);
}

RollOffRoof::~RollOffRoof()
{
}

esp_err_t RollOffRoof::action(const char *action, const char *parameters, char *buf, size_t len)
{
  return ALPACA_ERR_ACTION_NOT_IMPLEMENTED;
}

esp_err_t RollOffRoof::commandblind(const char *command, bool raw)
{
  return ALPACA_ERR_ACTION_NOT_IMPLEMENTED;
}

esp_err_t RollOffRoof::commandbool(const char *command, bool raw, bool *resp)
{
  return ALPACA_ERR_ACTION_NOT_IMPLEMENTED;
}

esp_err_t RollOffRoof::commandstring(const char *action, bool raw, char *buf, size_t len)
{
  return ALPACA_ERR_ACTION_NOT_IMPLEMENTED;
}

esp_err_t RollOffRoof::get_connected(bool *connected)
{
  *connected = _connected;
  return ALPACA_OK;
}

esp_err_t RollOffRoof::set_connected(bool connected)
{
  _connected = connected;
  return ALPACA_OK;
}

esp_err_t RollOffRoof::get_description(char *buf, size_t len)
{
  strncpy(buf, "RollOffRoof Dome Controller", len);
  return ALPACA_OK;
}

esp_err_t RollOffRoof::get_driverinfo(char *buf, size_t len)
{
  strncpy(buf, "Dark Dragons Astronomy LLC", len);
  return ALPACA_OK;
}

esp_err_t RollOffRoof::get_driverversion(char *buf, size_t len)
{
  strncpy(buf, "1.0.0", len);
  return ALPACA_OK;
}

esp_err_t RollOffRoof::get_interfaceversion(uint32_t *version)
{
  *version = 2;
  return ALPACA_OK;
}

esp_err_t RollOffRoof::get_name(char *buf, size_t len)
{
  strncpy(buf, "My Roof", len);
  return ALPACA_OK;
}

esp_err_t RollOffRoof::get_supportedactions(std::vector<std::string> &actions)
{
  actions.clear();
  return ALPACA_OK;
}

float RollOffRoof::read_distance_cm()
{
    // Send a 10us pulse on TRIG_PIN
    gpio_set_direction(TRIG_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(TRIG_PIN, 0);
    esp_rom_delay_us(2);
    gpio_set_level(TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_PIN, 0);

    // Listen on ECHO_PIN
    gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);

    // Wait for ECHO to go high
    int64_t start_time = esp_timer_get_time();
    while (gpio_get_level(ECHO_PIN) == 0) {
        if ((esp_timer_get_time() - start_time) > 100000) return -1; // Timeout
    }

    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level(ECHO_PIN) == 1) {
        if ((esp_timer_get_time() - echo_start) > 100000) return -1; // Timeout
    }

    int64_t echo_end = esp_timer_get_time();
    if (echo_end < echo_start) return -1;  // Just in case

    float pulse_duration = echo_end - echo_start; // in µs
    return pulse_duration / 58.0; // cm
}

esp_err_t RollOffRoof::get_issafe(bool *issafe)
{
  float distance = read_distance_cm();
  ESP_LOGI(TAG, "Distance: %.2f cm", distance);
  *issafe = (distance > 5); // safe only if object is at safe distance
  //*issafe = false;

  return ALPACA_OK;
}
#pragma once
#ifndef ROLL_OFF_ROOF_H
#define ROLL_OFF_ROOF_H

#include <alpaca_server/api.h>
#include "driver/gpio.h"
#include "esp_timer.h"


class RollOffRoof : public AlpacaServer::SafetyMonitor
{
public:
  RollOffRoof();
  ~RollOffRoof();

public:
  virtual esp_err_t action(const char *action, const char *parameters, char *buf, size_t len) override;
  virtual esp_err_t commandblind(const char *command, bool raw) override;
  virtual esp_err_t commandbool(const char *command, bool raw, bool *resp) override;
  virtual esp_err_t commandstring(const char *action, bool raw, char *buf, size_t len) override;
  virtual esp_err_t get_connected(bool *connected) override;
  virtual esp_err_t set_connected(bool connected) override;
  virtual esp_err_t get_description(char *buf, size_t len) override;
  virtual esp_err_t get_driverinfo(char *buf, size_t len) override;
  virtual esp_err_t get_driverversion(char *buf, size_t len) override;
  virtual esp_err_t get_interfaceversion(uint32_t *version) override;
  virtual esp_err_t get_name(char *buf, size_t len) override;
  virtual esp_err_t get_supportedactions(std::vector<std::string> &actions) override;
  virtual esp_err_t get_issafe(bool *issafe) override;

private:
  bool _connected;

  float read_distance_cm();

  static constexpr gpio_num_t TRIG_PIN = GPIO_NUM_5;
  static constexpr gpio_num_t ECHO_PIN = GPIO_NUM_18;
};

#endif // ROLL_OFF_ROOF_H

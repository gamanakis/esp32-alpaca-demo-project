#include "roll_off_roof.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstring> // for strncpy
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"

extern "C" {
#include "VL53L1X_api.h"
}

#define MEASUREMENT_CYCLE_MS        (50)        // the timing budget for the VL53L1
#define TIMER_PERIODIC_MS           (60)        // periodic timer for measurements

QueueHandle_t vl53_evt_queue = NULL;
uint16_t TOF = VL53L1_I2C_ADDRESS;
int     range_mm = 0;
int32_t measurement_cycle = 0;
int64_t last_measurement = 0;
esp_timer_handle_t tof_sensor_timer;   // collects measurements from the ToF sensor
uint8_t RangeStatus = VL53L1_RANGESTATUS_NONE;

static void periodic_tof_sensor(void* arg)
{
    VL53L1X_ERROR get_status;
    int64_t update_measurement_time;
    uint16_t Distance = -1;
    bool error;

    // mark the start time for this service routine
    update_measurement_time = esp_timer_get_time();
    measurement_cycle = update_measurement_time - last_measurement;
    last_measurement = update_measurement_time;

    // get the measurement and start a new measurement cycle
    get_status = VL53L1X_GetAndRestartMeasurement(TOF, &RangeStatus, &Distance);

    // determine if a measurement error happened
    error = get_status != VL53L1_ERROR_NONE;
    error = error || ( RangeStatus != VL53L1_RANGESTATUS_RANGE_VALID && 
                       RangeStatus != VL53L1_RANGESTATUS_WRAP_TARGET_FAIL );

    if (error)
      range_mm = -1;
    else
      range_mm = Distance;
}

static const char *TAG = "RollOffRoof";


RollOffRoof::RollOffRoof() : SafetyMonitor()
{
  _connected = false;

  uint8_t model_id, module_type, sensorState = 0;
  VL53L1X_ERROR status = 0;

  // startup the I2C interface and scan for the devices
  i2c_init();
  i2c_scan();

  // check the VL53L1 device and wait for it to boot
  status = VL53L1_RdByte(TOF, 0x010F, &model_id);
  printf("VL53L1X Model_ID: %X, status = %d\n", model_id, status );
  status = VL53L1_RdByte(TOF, 0x0110, &module_type);
  printf("VL53L1X Module_Type: %X, status = %d\n", module_type, status );
  while ( sensorState == 0 ) {
      status = VL53L1X_BootState(TOF, &sensorState);
      vTaskDelay( 20 / portTICK_PERIOD_MS );
  }
  printf("VL53L1 device booted\n");

  // initialize the ToF sensor
  VL53L1X_SensorInit( TOF );

  // 1=short (up to 1 M), 2=long (up to 4 M)
  VL53L1X_SetDistanceMode(TOF, 1);

  // in ms possible values [20, 50, 100, 200, 500]
  VL53L1X_SetTimingBudgetInMs(TOF, MEASUREMENT_CYCLE_MS);       

  // in ms, IM must be > = TB
  VL53L1X_SetInterMeasurementInMs(TOF, 5 + MEASUREMENT_CYCLE_MS);   

  // need to start the VL53L1 with a first request for measurement
  printf("VL53L1X Ultra Lite Driver Example running ...\n");
  VL53L1X_StartRanging(TOF);   

  vTaskDelay( TIMER_PERIODIC_MS / portTICK_PERIOD_MS );

  // create the periodic time and service routine
  const esp_timer_create_args_t tof_sensor_timer_args = {
      .callback = &periodic_tof_sensor,
      .name = "tofsensor"
  };
  esp_timer_create( &tof_sensor_timer_args, &tof_sensor_timer );
  esp_timer_start_periodic( tof_sensor_timer, TIMER_PERIODIC_MS * 1000 );
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
    printf( "\r range = %d mm, cycle = %4.1f ms, status = %u  ",
                        range_mm, (float)measurement_cycle / 1000, RangeStatus);

    if (range_mm == -1)
      return 666;
    else
      return range_mm/10; // cm
}

esp_err_t RollOffRoof::get_issafe(bool *issafe)
{
  float distance = read_distance_cm();
  ESP_LOGI(TAG, "Distance: %.2f cm", distance);
  *issafe = (distance > 5); // safe only if object is at safe distance
  //*issafe = false;

  return ALPACA_OK;
}
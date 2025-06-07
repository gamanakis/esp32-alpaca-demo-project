#include "sdkconfig.h"

#include <alpaca_server/api.h>
#include <alpaca_server/device.h>
#include <alpaca_server/discovery.h>

#include <esp_app_desc.h>
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include "roll_off_roof.h"

#include <stdio.h>
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

static void periodic_tof_sensor(void* arg)
{
    VL53L1X_ERROR get_status;
    int64_t update_measurement_time;
    uint8_t RangeStatus = VL53L1_RANGESTATUS_NONE;
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
    error = error || ( RangeStatus != VL53L1_RANGESTATUS_RANGE_VALID && RangeStatus != VL53L1_RANGESTATUS_WRAP_TARGET_FAIL );
    range_mm = error ? -1 : Distance;
}

#define SSID "SSID"
#define PASSWORD "PASS"

static const char *TAG = "main";

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {

  ESP_LOGI(TAG, "Event dispatched from event loop base=%s, event_id=%ld",
           event_base, event_id);

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  }

  if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {

    ip_event_got_ip_t *ip_info = (ip_event_got_ip_t *)event_data;

    char buf[16];

    printf("Got IP address: '%s'\n",
           esp_ip4addr_ntoa(&ip_info->ip_info.ip, buf, sizeof(buf)));
  }
}

extern "C" void app_main(void) {
  vTaskDelay(
      pdMS_TO_TICKS(5000)); // Delay to allow the serial monitor to connect

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


  esp_log_level_set("alpaca_server_api", ESP_LOG_INFO);

  nvs_flash_init();

  esp_netif_init();

  esp_event_loop_create_default();

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, nullptr, nullptr));

  // Set WiFi configuration
  wifi_config_t wifi_config = {}; // Zero-initialize everything
  strncpy((char *)wifi_config.sta.ssid, SSID, sizeof(wifi_config.sta.ssid));
  strncpy((char *)wifi_config.sta.password, PASSWORD, sizeof(wifi_config.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  esp_app_desc_t desc;
  ESP_ERROR_CHECK(esp_ota_get_partition_description(
      esp_ota_get_running_partition(), &desc));

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  config.stack_size = 8192;
  config.max_uri_handlers =
      64; // Default is 8, adjust to handle the number of routes you have.

  httpd_handle_t esp_http_server;
  ESP_ERROR_CHECK(httpd_start(&esp_http_server, &config));

  std::vector<AlpacaServer::Device *> devices = {
      new RollOffRoof(),
  };

  AlpacaServer::Api api(devices, "MY_DEVICE_SERIAL_NUMBER",
                        "Dark Dragons Alpaca Server",
                        "Dark Dragons Astronomy LLC", desc.version, "Home");
  api.register_routes(esp_http_server);

  ESP_LOGI(TAG, "Starting Discovery server");
  alpaca_server_discovery_start(80);

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    printf( "\r range = %d mm, cycle = %4.1f ms  ", range_mm, (float)measurement_cycle / 1000);
  }
}

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

#define SSID "SSID"
#define PASSWORD "PASS"

static const char *TAG = "main";

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
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

extern "C" void app_main(void)
{
  vTaskDelay(
      pdMS_TO_TICKS(5000)); // Delay to allow the serial monitor to connect

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
  }
}

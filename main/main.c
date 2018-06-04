#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "esp_pm.h"
#include "nvs_flash.h"
#include <cJSON.h>

#include "websocket.h"

#define DEFAULT_SSID "x"
#define DEFAULT_PWD "x"

static const char *TAG = "websocket";

websocket_client client;
pthread_t t;
volatile bool connected = false;
#define BLINK_GPIO 22


void send_message(const char *name, const cJSON *payload);
void send_device_list();
void handle_set_value(cJSON* resource, cJSON* value);

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_GOT_IP");
            ESP_LOGI(TAG, "got ip:%s\n",
            ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            connected = true;
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wifi_enable(void)
{
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    wifi_config_t wifi_config = {
	.sta = {
	    .ssid = DEFAULT_SSID,
	    .password = DEFAULT_PWD,
	},
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wait_for_wifi(void *pvParameter)
{
    while(!connected) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "websocket waiting");
    }

    ESP_LOGI(TAG, "websocket connecting");


    if (websocket_open(&client, "192.168.1.146", 12345, "/connect")) {
        pthread_join(client.thread, NULL);
    } else {
        ESP_LOGI(TAG, "websocket failed to connect");
    }

    vTaskDelete(NULL);
}

const char* AUTH_TOKEN= "2f6df06e-f634-4219-b609-5e4fbbe82c6a";


void on_data_received(uint8_t* data, uint16_t len) {
    fprintf(stderr, "on_data %s\n", data);

    cJSON *message = cJSON_Parse((char*) data);
    if (message == NULL)
    {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }

        return;
    }
    const cJSON *name = cJSON_GetObjectItem(message, "name");
    const cJSON *payload = cJSON_GetObjectItem(message, "payload");

    printf("msg name:\"%s\"\n", name->valuestring);

    if (strcmp(name->valuestring, "RequestGetDevices") == 0) {
        send_device_list();
    } else if (strcmp(name->valuestring, "RequestSetValue") == 0) {
        cJSON* value = cJSON_GetObjectItem(payload, "value");
        cJSON* resource = cJSON_GetObjectItem(payload, "resource");
        handle_set_value(resource, value);
    }

    cJSON_Delete(message);
}

void authorize(){
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "token", AUTH_TOKEN);
    cJSON_AddStringToObject(payload, "name", "HUB C");
    cJSON_AddStringToObject(payload, "uuid", "2f6df06e-f634-4249-b609-5e4fbbe82c6a");

    send_message("RequestAuthorize", payload);
}

void handle_set_value(cJSON* resource, cJSON* value) {
    cJSON* v = cJSON_GetObjectItem(value, "value");

    bool resourceValue = cJSON_IsTrue(v) ? true : false;

    printf("set %s %d\n", resource->valuestring, resourceValue);

    gpio_set_level(BLINK_GPIO, resourceValue);
}

void send_device_list(){
    cJSON *payload = cJSON_CreateObject();

    char devices[] = "[{\"id\": \"0685B960-736F-46F7-BEC0-9E6CBD61HDC1\", \"name\": \"Fake C Button\", \"variables\": [  {\"href\": \"\/light\",\"if\": \"oic.if.rw\",\"n\": \"Switch\",\"rt\": \"oic.r.switch.binary\",\"values\": {\"rt\": \"oic.r.switch.binary\", \"value\": false}  } ]}]";
    cJSON_AddRawToObject(payload, "devices", devices);

    send_message("EventDeviceListUpdate", payload);
}

void send_message(const char *name, const cJSON *payload) {
    cJSON *message = cJSON_CreateObject();

    cJSON_AddStringToObject(message, "name", name);
    cJSON_AddItemToObject(message, "payload", payload);

    char* text = cJSON_PrintUnformatted(message);
    cJSON_Delete(message);
    websocket_send_text(&client, text, strlen(text));
    free(text);
}

void on_connected() {
    printf("on_connected");
    authorize();
}


void app_main()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    client = websocket_init();
    client.on_connected = on_connected;
    client.on_message_received = on_data_received;

    gpio_pad_select_gpio(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    wifi_enable();
    xTaskCreate(&wait_for_wifi, "wait 4 wifi", 18192, NULL, 3, NULL);
}

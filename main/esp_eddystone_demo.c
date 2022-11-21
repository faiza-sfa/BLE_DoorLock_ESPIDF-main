/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/


/****************************************************************************
*
* This file is used for eddystone receiver.
*
****************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "esp_bt.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gatt_defs.h"
#include "esp_gattc_api.h"
#include "esp_gap_ble_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"


#include "driver/gpio.h"
#include <inttypes.h>
#define ESP_INTR_FLAG_DEFAULT 0

static QueueHandle_t gpio_evt_queue = NULL;


#include "esp_eddystone_protocol.h"
#include "esp_eddystone_api.h"


#define DEVICE_ID                   "2022002210160000"
#define MQTT_BROKER                 "mqtt://broker.hivemq.com"
#define MQTT_PORT                   1883
#define PUB_TOPIC                   "DMA/BLE_Temperature"
#define SUB_TOPIC                   "DMA/BLE_Temperature/2022002210160000"
#define MAX_MQTT_MSG_SIZE           512


#define LOCK_PIN                    26
#define UNLOCK_PIN                  GPIO_NUM_25
#define UNLOCK                      1
#define LOCK                        0
#define STATE_SEND_PERIOD           5000

bool lock_state = 1; // 0 = unlocked, 1 = locked

void executeCommand(char* command, int length);
void unlockButtonTriggered(void* arg);

static const char* DEMO_TAG = "EDDYSTONE_DEMO";
static const char *TAG = "MQTT_EXAMPLE";

static const char *BLE_TAG = "BLE Data";

xTaskHandle ble_data_task_handle;
xTaskHandle mqtt_task_handle;


esp_mqtt_client_handle_t client;

void bleDataTask(void *pvParameters);
void mqttTask(void *pvParameters);

#define MAX_BLE_MAC_ADDR_SIZE 6
#define MAX_BLE_QUEUE_SIZE 10


static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void unlockTask(void* arg)
{
    uint32_t io_num;
    for(;;) {
        if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            printf("GPIO[%"PRIu32"] intr, val: %d\n", io_num, gpio_get_level(io_num));
            printf("Triggered");
            gpio_set_level(LOCK_PIN, UNLOCK);
            lock_state = 0;

            char mqtt_msg[200];
            strcpy(mqtt_msg, DEVICE_ID);
            strcat(mqtt_msg, ",0");
            esp_mqtt_client_publish(client, PUB_TOPIC, mqtt_msg, 0, 2, 0);
        }
    }
}
typedef struct BLE_Data_Params_t
{
    uint8_t mac_addr[MAX_BLE_MAC_ADDR_SIZE];
    int rssi;
    // uint8_t version;          /*<! TLM version,0x00 for now */
    // uint16_t battery_voltage; /*<! battery voltage in mV */
    float temperature;        /*<! beacon temperature in degrees Celsius */
    // uint32_t adv_count;       /*<! adv pdu count since power-up */
    // uint32_t time;            /*<! time since power-up, a 0.1 second resolution counter */
} BLE_Data_Params_t;


void makeBleDataStr(BLE_Data_Params_t *ble_data, char* buff);
void showAllBleData(BLE_Data_Params_t *ble_data, const char *tag);

QueueHandle_t bleDataQueue;

/* declare static functions */
static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param);
static void esp_eddystone_show_inform(const esp_eddystone_result_t* res);
// static void esp_eddystone_temp(const esp_eddystone_result_t* res);

static esp_ble_scan_params_t ble_scan_params = {
    .scan_type              = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type          = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy     = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval          = 0x50,
    .scan_window            = 0x30,
    .scan_duplicate         = BLE_SCAN_DUPLICATE_DISABLE
};

static void esp_eddystone_show_inform(const esp_eddystone_result_t* res )
{
    switch(res->common.frame_type)
    {
        case EDDYSTONE_FRAME_TYPE_UID: {
            // ESP_LOGI(DEMO_TAG, "Eddystone UID inform:");
            // ESP_LOGI(DEMO_TAG, "Measured power(RSSI at 0m distance):%d dbm", res->inform.uid.ranging_data);
            // ESP_LOGI(DEMO_TAG, "EDDYSTONE_DEMO: Namespace ID:0x");
            // esp_log_buffer_hex(DEMO_TAG, res->inform.uid.namespace_id, 10);
            // ESP_LOGI(DEMO_TAG, "EDDYSTONE_DEMO: Instance ID:0x");
            // esp_log_buffer_hex(DEMO_TAG, res->inform.uid.instance_id, 6);
            break;
        }
        case EDDYSTONE_FRAME_TYPE_URL: {
            // ESP_LOGI(DEMO_TAG, "Eddystone URL inform:");
            // ESP_LOGI(DEMO_TAG, "Measured power(RSSI at 0m distance):%d dbm", res->inform.url.tx_power);
            // ESP_LOGI(DEMO_TAG, "URL: %s", res->inform.url.url);
            break;
        }
        case EDDYSTONE_FRAME_TYPE_TLM: {
            // ESP_LOGI(DEMO_TAG, "Eddystone TLM inform:");
            // ESP_LOGI(DEMO_TAG, "version: %d", res->inform.tlm.version);
            // ESP_LOGI(DEMO_TAG, "battery voltage: %d mV", res->inform.tlm.battery_voltage);
            // ESP_LOGI(DEMO_TAG, "beacon temperature in degrees Celsius: %6.1f", res->inform.tlm.temperature);
            // ESP_LOGI(DEMO_TAG, "adv pdu count since power-up: %d", res->inform.tlm.adv_count);
            // ESP_LOGI(DEMO_TAG, "time since power-up: %d s", (res->inform.tlm.time)/10);
            
            break;
        }
        default:
            break;
    }
}



// float esp_eddystone_temp(const esp_eddystone_result_t* res, )
// {
// //     if (res->common.frame_type == 'EDDYSTONE_FRAME_TYPE_TLM')
// //         return res->inform.tlm.temperature;
// }
    


static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param)
{
    esp_err_t err;

    switch(event)
    {
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT: {
            uint32_t duration = 0;
            esp_ble_gap_start_scanning(duration);
            break;
        }
        case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT: {
            if((err = param->scan_start_cmpl.status) != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(DEMO_TAG,"Scan start failed: %s", esp_err_to_name(err));
            }
            else {
                ESP_LOGI(DEMO_TAG,"Start scanning...");
            }
            break;
        }
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            esp_ble_gap_cb_param_t* scan_result = (esp_ble_gap_cb_param_t*)param;
            switch(scan_result->scan_rst.search_evt)
            {
                case ESP_GAP_SEARCH_INQ_RES_EVT: {
                    esp_eddystone_result_t eddystone_res;
                    memset(&eddystone_res, 0, sizeof(eddystone_res));
                    esp_err_t ret = esp_eddystone_decode(scan_result->scan_rst.ble_adv, scan_result->scan_rst.adv_data_len, &eddystone_res);
                    if (ret) {
                        // error:The received data is not an eddystone frame packet or a correct eddystone frame packet.
                        // just return
                        return;
                    } else {
                        // The received adv data is a correct eddystone frame packet.
                        // Here, we get the eddystone infomation in eddystone_res, we can use the data in res to do other things.
                        // For example, just print them:
                        // ESP_LOGI(DEMO_TAG, "--------Eddystone Found----------");
                        // esp_log_buffer_hex("EDDYSTONE_DEMO: Device address:", scan_result->scan_rst.bda, ESP_BD_ADDR_LEN);
                        // ESP_LOGI(DEMO_TAG, "RSSI of packet:%d dbm", scan_result->scan_rst.rssi);
                        BLE_Data_Params_t *ble_data = (BLE_Data_Params_t *)malloc(sizeof(BLE_Data_Params_t));
                        // esp_eddystone_show_inform(&eddystone_res);

                        ble_data->rssi = scan_result->scan_rst.rssi;

                        memcpy(ble_data->mac_addr, scan_result->scan_rst.bda, MAX_BLE_MAC_ADDR_SIZE);
                        // char eddy_frame_type[20];

                        // eddy_frame_type  = eddystone_res->common.frame_type;
                        // printf("Frame Type, %s", eddystone_res);
                        // ESP_LOGI(TAG, "Frame Type, %s", eddy_frame_type);
                        // float temp_ble;
                        // temp_ble = esp_eddystone_temp(&eddystone_res);
                        if(eddystone_res.common.frame_type  == EDDYSTONE_FRAME_TYPE_TLM)
                        {
                            ble_data->temperature = eddystone_res.inform.tlm.temperature;
                            printf("temp:  %6.1f \n",ble_data->temperature);
                            if (xQueueSend(bleDataQueue, ble_data, portMAX_DELAY) != pdPASS)
                            {
                                printf("BLE Data NOT sent!");
                            }
                        }
                       
                            

                        // if (eddystone_res->common.frame_type EDDYSTONE_FRAME_TYPE_TLM)
                        free(ble_data);
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:{
            if((err = param->scan_stop_cmpl.status) != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(DEMO_TAG,"Scan stop failed: %s", esp_err_to_name(err));
            }
            else {
                ESP_LOGI(DEMO_TAG,"Stop scan successfully");
            }
            break;
        }
        default:
            break;
    }
}

void esp_eddystone_appRegister(void)
{
    esp_err_t status;

    ESP_LOGI(DEMO_TAG,"Register callback");

    /*<! register the scan callback function to the gap module */
    if((status = esp_ble_gap_register_callback(esp_gap_cb)) != ESP_OK) {
        ESP_LOGE(DEMO_TAG,"gap register error: %s", esp_err_to_name(status));
        return;
    }
}

void esp_eddystone_init(void)
{
    esp_bluedroid_init();
    esp_bluedroid_enable();
    esp_eddystone_appRegister();
}



static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        char mqtt_msg[200];
        strcpy(mqtt_msg, DEVICE_ID);
        strcat(mqtt_msg, ",started");
        msg_id = esp_mqtt_client_publish(client, PUB_TOPIC, mqtt_msg, 0, 2, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, SUB_TOPIC, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        executeCommand(event->data, event->data_len);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}


void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    gpio_reset_pin(LOCK_PIN);
    gpio_set_direction(LOCK_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LOCK_PIN, LOCK);
    lock_state = 1;

    // //zero-initialize the config structure.
    // gpio_config_t io_conf = {};
    // //interrupt of rising edge
    // io_conf.intr_type = GPIO_INTR_NEGEDGE;
    // //bit mask of the pins
    // io_conf.pin_bit_mask = ;
    // //set as input mode
    // io_conf.mode = GPIO_MODE_INPUT;
    // //enable pull-up mode
    // io_conf.pull_up_en = 1;
    // gpio_config(&io_conf);

    gpio_pad_select_gpio(UNLOCK_PIN);
	gpio_set_direction(UNLOCK_PIN, GPIO_MODE_INPUT);
	gpio_set_pull_mode(UNLOCK_PIN, GPIO_PULLUP_ONLY);
	gpio_set_intr_type(UNLOCK_PIN, GPIO_INTR_NEGEDGE);
	gpio_intr_enable(UNLOCK_PIN);

    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //start gpio task
    xTaskCreate(unlockTask, "unlockTask", 2048, NULL, 10, NULL);
    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(UNLOCK_PIN, gpio_isr_handler, (void*) UNLOCK_PIN);

    


    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());


    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    esp_mqtt_client_config_t mqtt_cfg = {
        .uri = MQTT_BROKER,
        .port = MQTT_PORT,
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    esp_eddystone_init();

    /*<! set scan parameters */
    esp_ble_gap_set_scan_params(&ble_scan_params);
    bleDataQueue = xQueueCreate(MAX_BLE_QUEUE_SIZE, sizeof(BLE_Data_Params_t));
    xTaskCreatePinnedToCore(&bleDataTask, "BLE data task", 4096, NULL, 1, ble_data_task_handle, 1);
    xTaskCreatePinnedToCore(&mqttTask, "MQTT task", 2048, NULL, 1, mqtt_task_handle, 0);
}


void bleDataTask(void *pvParameters)
{
    BLE_Data_Params_t *ble_data = (BLE_Data_Params_t *)malloc(sizeof(BLE_Data_Params_t));
    char ble_data_str[MAX_BLE_MAC_ADDR_SIZE*3 + 15]; 
    char pub_msg[MAX_MQTT_MSG_SIZE];
    char rssi_str[5];
    char tempe[10];
    int msg_id;
    while (1)
    {
        if (bleDataQueue != NULL)
        {
            if (xQueueReceive(bleDataQueue, ble_data, portMAX_DELAY) == pdPASS)
            {
                printf("Data Received\n");
                showAllBleData(ble_data, BLE_TAG);
                sprintf(ble_data_str, "%02X:%02X:%02X:%02X:%02X:%02X,%f,", ble_data->mac_addr[0], ble_data->mac_addr[1], ble_data->mac_addr[2],
                ble_data->mac_addr[3], ble_data->mac_addr[4], ble_data->mac_addr[5],ble_data->temperature);
                // strcat(ble_data_str, ",");
                
                itoa(ble_data->rssi, rssi_str, 10);
                strcat(ble_data_str, rssi_str);

                // itoa(ble_data->temperature, tempe, 10);
                // snprintf (tempe, sizeof(tempe), "%f",ble_data->temperature);
                // dtostrf(float_value, min_width, num_digits_after_decimal, where_to_store_string)

                // dtostrf(ble_data->temperature, 10, 2, tempe);
                // sprintf(tempe,"%f",ble_data->temperature);

                // strcat(ble_data_str, tempe);
                
                strcpy(pub_msg, DEVICE_ID);
                strcat(pub_msg, ",");
                strcat(pub_msg, ble_data_str);

                msg_id = esp_mqtt_client_publish(client, PUB_TOPIC, pub_msg, 0, 2, 0);
                ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            }
        }
    }
    free(ble_data);
}

void showAllBleData(BLE_Data_Params_t *ble_data, const char *tag)
{
    ESP_LOGI(tag, "Mac: %02X:%02X:%02X:%02X:%02X:%02X\n", ble_data->mac_addr[0], ble_data->mac_addr[1], ble_data->mac_addr[2],
             ble_data->mac_addr[3], ble_data->mac_addr[4], ble_data->mac_addr[5]);
    ESP_LOGI(tag, "rssi: %d", ble_data->rssi);
    ESP_LOGI(tag, "Temperature:%f", ble_data->temperature);
}

void makeBleDataStr(BLE_Data_Params_t *ble_data, char* buff)
{
    sprintf(buff, "%02X:%02X:%02X:%02X:%02X:%02X", ble_data->mac_addr[0], ble_data->mac_addr[1], ble_data->mac_addr[2],
             ble_data->mac_addr[3], ble_data->mac_addr[4], ble_data->mac_addr[5]);
    // strcat(buff, ",");
    // char rssi[5];
    // itoa(ble_data->rssi, rssi, 10);
    // strcat(buff, rssi);
    printf("%s", buff);
}

void mqttTask(void *pvParameters)
{
    // esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)malloc(sizeof(esp_mqtt_client_handle_t));
    // &client = pvParameters;
    // esp_mqtt_client_handle_t *client = (esp_mqtt_client_handle_t*)pvParameters;

    int msg_id;
    
    while(1)
    {
        if(lock_state)
        {
            char mqtt_msg[200];
            strcpy(mqtt_msg, DEVICE_ID);
            strcat(mqtt_msg, ",1");
            msg_id = esp_mqtt_client_publish(client, PUB_TOPIC, mqtt_msg, 0, 2, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        }
        else
        {
            char mqtt_msg[200];
            strcpy(mqtt_msg, DEVICE_ID);
            strcat(mqtt_msg, ",0");
            msg_id = esp_mqtt_client_publish(client, PUB_TOPIC, mqtt_msg, 0, 2, 0);
            ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        }

        vTaskDelay(STATE_SEND_PERIOD / portTICK_PERIOD_MS);
    }
}

void executeCommand(char* command, int length)
{
    char command_to_be_executed[256];
    int i;
    for(i = 0; i < length; i++)
    {
        command_to_be_executed[i] = command[i];
    }
    command_to_be_executed[i] = '\0';

    printf("Command=%s\n", command_to_be_executed);

    char mqtt_msg[200];
    strcpy(mqtt_msg, DEVICE_ID);

    if(!strcmp(command_to_be_executed, "open"))
    {
        printf("Opening");
        gpio_set_level(LOCK_PIN, UNLOCK);
        lock_state = 0;
        strcat(mqtt_msg, ",0");
    }
    else if(!strcmp(command_to_be_executed, "close"))
    {
        printf("Closing");
        gpio_set_level(LOCK_PIN, LOCK);
        lock_state = 1;
        strcat(mqtt_msg, ",1");
    }
    else
    {
        strcat(mqtt_msg, ",invalid");
    }
    esp_mqtt_client_publish(client, PUB_TOPIC, mqtt_msg, 0, 2, 0);
}


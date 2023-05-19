#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "nvs_flash.h"
#include <string.h>
#include "fml_fsm.h"
#include "driver/gpio.h"
#include "fml_wifisniffer.h"

#define BREAKINTERNET_LED 2
#define BREAKINTERNET_BUTTON   23 

enum breakinternet_state{
    CAPTURE_FAIL,
    CAPTURE_SUCCESS,
    CAPTURE_WAITING,
};

enum breakinternet_event{
    EVENT_PRESS,                //按下
    EVENT_SPRINGUP,             //弹起
    EVENT_RESET,                //重置事件
};

enum breakinternet_button_state{
    BUTTON_STATE_PRESS,
    BUTTON_STATE_SPRINGUP,
};

enum breakinternet_led_state{
    LED_STATE_OFF,
    LED_STATE_FLASH,
    LED_STATE_ON,
};
static fml_fsm_Handle_t breakinternet_fsm_handle;
static uint8_t button_state = BUTTON_STATE_SPRINGUP;
static uint8_t led_state = LED_STATE_OFF;
static char devaddr[6];
static char bassid[6];

static int breakinternet_Reset(void)
{
    gpio_reset_pin(BREAKINTERNET_LED);
    gpio_set_direction(BREAKINTERNET_LED, GPIO_MODE_OUTPUT);
    button_state = BUTTON_STATE_PRESS;
    led_state = LED_STATE_OFF;
    fml_wifisniffer_stop();
    return 0;
}

static int breakinternet_enter_CaptureWaiting(void)
{
    if(button_state == BUTTON_STATE_PRESS)
    {
        button_state = BUTTON_STATE_SPRINGUP;
        fml_wifisniffer_start();
        led_state = LED_STATE_FLASH;
        return 0;
    }

    return -1;
}

static int breakinternet_enter_CaptureSuccess(void)
{
    static uint32_t tim_count = 0;
    uint8_t devaddr_flag = 0, bssidflag = 0;
    static uint8_t nulldata[] = 
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };   

    if(tim_count == 10)
    {
        tim_count = 0;
        if(fml_wifisniffer_GetLastDevAddr(devaddr, 6) == 0)
        {
            if(memcmp(nulldata,devaddr,6) != 0)devaddr_flag = 1;
        }

        if(fml_wifisniffer_GetLastDevBssId(bassid, 6) == 0)
        {
            if(memcmp(nulldata,bassid,6) != 0)bssidflag = 1;                                   
        }
        if(devaddr_flag && bssidflag)
        {
            led_state = LED_STATE_ON;
            return 0;
        }
    }

    tim_count++;

    return -1;
}

static int breakinternet_CaptureSucess(void)
{
    fml_wifisniffer_SendDeauthFrame(devaddr, 6, bassid, 6);
    return -1;
}

fml_fsm_table_t breakinternet_fsm_table[] = 
{
    //{到来的事件，当前的状态，将要要执行的函数，下一个状态}
    {EVENT_PRESS,CAPTURE_FAIL,breakinternet_Reset,CAPTURE_FAIL},
    {EVENT_PRESS,CAPTURE_WAITING,breakinternet_Reset,CAPTURE_FAIL},
    {EVENT_PRESS,CAPTURE_SUCCESS,breakinternet_Reset,CAPTURE_FAIL},

    {EVENT_SPRINGUP,CAPTURE_FAIL,breakinternet_enter_CaptureWaiting,CAPTURE_WAITING},
    {EVENT_SPRINGUP,CAPTURE_WAITING,breakinternet_enter_CaptureSuccess,CAPTURE_SUCCESS},
    //{EVENT_SPRINGUP,CAPTURE_SUCCESS,breakinternet_CaptureSucess,CAPTURE_SUCCESS},


    {EVENT_RESET,CAPTURE_WAITING,breakinternet_Reset,CAPTURE_FAIL},
    
};

void app_main(void)
{
	printf("Hello world!\n");

    /* Print chip information */
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    printf("This is %s chip with %d CPU core(s), WiFi%s%s%s, ",
           CONFIG_IDF_TARGET,
           chip_info.cores,
           (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
           (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
           (chip_info.features & CHIP_FEATURE_IEEE802154) ? ", 802.15.4 (Zigbee/Thread)" : "");
		
	unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;
    printf("silicon revision v%d.%d, ", major_rev, minor_rev);
    if(esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        printf("Get flash size failed");
    }else{
		printf("%" PRIu32 "MB %s flash\n", flash_size / (uint32_t)(1024 * 1024),
			   (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

		printf("Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());
	}


    wifi_country_t  wifi_country;//基于国家区域限制的WIFI

    wifi_country.cc[0] = 'C';
    wifi_country.cc[1] = 'N';
    wifi_country.schan = 1;
    wifi_country.nchan = 13;
    wifi_country.policy = WIFI_COUNTRY_POLICY_AUTO;

    nvs_flash_init();
    esp_netif_init();
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t  cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_country(&wifi_country));    /* set country for channel range [1, 13] */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());

    fml_wifisniffer_init();
    fml_wifisniffer_SetRssi(-25);

    breakinternet_fsm_handle = fml_fsm_Register(breakinternet_fsm_table,CAPTURE_FAIL,sizeof(breakinternet_fsm_table)/sizeof(fml_fsm_table_t));

    gpio_reset_pin(BREAKINTERNET_BUTTON);
    gpio_set_direction(BREAKINTERNET_BUTTON, GPIO_MODE_INPUT);

    int button_io;
    uint8_t led_io = 0;
    uint32_t led_count = 0;
    int curState;

    while (1) {
        printf("app_main running:%d,%d...\n",button_state,curState);

        switch(led_state)
        {
            case LED_STATE_OFF:
            gpio_set_level(BREAKINTERNET_LED, 0);
            break;

            case LED_STATE_FLASH:
            gpio_set_level(BREAKINTERNET_LED, led_io);
            led_count++;
            if(led_count == 10)
            {
                led_io = !led_io;
                led_count = 0;
            }
            break;

            case LED_STATE_ON:
            gpio_set_level(BREAKINTERNET_LED, 1);
            break;
        }

        button_io = gpio_get_level(BREAKINTERNET_BUTTON);

        if(button_io == 0)
        {
            fml_fsm_SendEvent(breakinternet_fsm_handle, EVENT_PRESS, portMAX_DELAY);
        }
        else
        {
            fml_fsm_SendEvent(breakinternet_fsm_handle, EVENT_SPRINGUP, portMAX_DELAY);
        }
        fml_fsm_GetCurState(breakinternet_fsm_handle, &curState);
        if(curState == CAPTURE_SUCCESS)
        {
            breakinternet_CaptureSucess();
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

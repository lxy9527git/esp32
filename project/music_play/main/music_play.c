/* Play mp3 file by audio pipeline
   with possibility to start, stop, pause and resume playback
   as well as adjust volume

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_mem.h"
#include "audio_common.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "esp_peripherals.h"
#include "periph_touch.h"
#include "periph_adc_button.h"
#include "periph_button.h"
#include "board.h"
#include "hdl_wm8978.h"

#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "nvs_flash.h"
#include <string.h>
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "stddef.h"
#include "esp_netif.h"
#include "lwip/tcp.h"
#include "esp_http_client.h"

/*
 * 要连接的路由器名字和密码
 */

//路由器的名字
#define GATEWAY_SSID "Xiaomi_FBBC"
//连接的路由器密码
#define GATEWAY_PASSWORD "87654321."

#define MUSIC_PLAY_TASK_PRIO                                            (3)                                     //任务优先级
#define MUSIC_PLAY_STK_SIZE                                             (2048*2)                                //任务堆栈大小

#define WIFI_CONNECTED_BIT                                              (BIT0)
#define WIFI_ask_mp3_BIT                                                (BIT1)
#define WIFI_rpy_mp3_BIT                                                (BIT2)

#ifndef MAC2STR
#define MAC2STR(a)                                                      (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR                                                          "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

#define MUSIC_URL                                                       "http://music.163.com/song/media/outer/url?id=2048895127.mp3"

#define BUFFER_SIZE                                                     (1024)

static const char *TAG = "music_play";

static struct marker {
    int pos;
    const uint8_t *start;
    const uint8_t *end;
} file_marker;


TaskHandle_t                    music_play_task_handle;                                                        //任务句柄
StackType_t                     music_play_task_stack[MUSIC_PLAY_STK_SIZE];                                    //任务栈
StaticTask_t                    music_play_task_tcb;                                                           //任务控制块

static EventGroupHandle_t       event_group;
static char                     url_buff[1024];
static int                      bytes_to_read;
static int                      bytes_read;
static char *                   mp3_buf;


static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data);
static void wifi_init_sta_ap(void);
static esp_err_t http_event_handler(esp_http_client_event_t *evt);
static void music_play_task(void *parameter);

static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)                           //station模式开启回调
    {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)               //断开与路由器的连接
    {
        esp_wifi_connect();
        xEventGroupClearBits(event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)                         //成功连接到路由器，获取到IP地址
    {
        ip_event_got_ip_t *event;
        event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "event_handler:SYSTEM_EVENT_STA_GOT_IP!");
        ESP_LOGI(TAG, "got ip:" IPSTR "\n", IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(event_group, WIFI_CONNECTED_BIT);
    }
    else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)                 //作为AP热点模式，检测到有子设备接入
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG,"station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
    }
    else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)              //作为AP热点模式，检测到有子设备断开了连接
    {
		ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");                                         
		wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",MAC2STR(event->mac), event->aid);
    }
}

static void wifi_init_sta_ap(void)
{
    nvs_flash_init();
    ESP_ERROR_CHECK(esp_netif_init());                                  //初始化内部的lwip(Light weight internet protocol)
    event_group = xEventGroupCreate();                                  //创建事件组
    ESP_ERROR_CHECK(esp_event_loop_create_default());                   //创建系统事件任务
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();       //创建有tcp/ip堆栈的默认网络接口实例绑定STA。
    assert(sta_netif);

    sta_netif = esp_netif_create_default_wifi_ap();                     //创建有tcp/ip堆栈的默认网络接口实例绑定AP。
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );

    //配置STA模式
    wifi_config_t sta_config = { 
    .sta = { .ssid = GATEWAY_SSID, 
     .password = GATEWAY_PASSWORD } };
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_APSTA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_config));
    ESP_LOGI(TAG, "udp_wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s \n",
    GATEWAY_SSID, GATEWAY_PASSWORD);

    // 配置AP模式
    wifi_config_t ap_config = {
        .ap = {
            .ssid = "esp32_ap",
            .password = "12345678",
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));


    ESP_ERROR_CHECK( esp_wifi_start() );

}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            if(strcmp(evt->header_key,"Location") == 0)
            {
                memcpy(url_buff,evt->header_value,strlen(evt->header_value));
                ESP_LOGI(TAG, "copy:%s",url_buff);
            }
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;

        default:
            break;
    }
    return ESP_OK;
}

int mp3_music_read_cb(audio_element_handle_t el, char *buf, int len, TickType_t wait_time, void *ctx)
{

    //获取要分段的大小
    bytes_to_read = len;
    mp3_buf = buf;

    //通知对方
    xEventGroupSetBits(event_group, WIFI_ask_mp3_BIT);

    //等待对方返回数据
    if(xEventGroupWaitBits(event_group,WIFI_rpy_mp3_BIT,true,true,wait_time) == 0)
    {
        //重启
        esp_restart();
        return AEL_IO_TIMEOUT;
    }
    //返回的数据长度
    if(bytes_read == 0)
    {
        printf("AEL_IO_DONE!!!!!!\n");
        return AEL_IO_DONE;
    }
    printf("mp3_music_read_cb:%d\n",uxTaskPriorityGet(NULL));

    return bytes_read;
}

static void music_play_task(void *parameter)
{

    //等待是否已经成功连接到路由器的标志位
    xEventGroupWaitBits(event_group,WIFI_CONNECTED_BIT,false,true,portMAX_DELAY);

    //5秒之后开始创建socket
    ESP_LOGI(TAG, "esp32 is ready !!! create music_play after 5s...\n");
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    //下载的url
    memcpy(url_buff,MUSIC_URL,strlen(MUSIC_URL));

    while(1)
    {
        esp_http_client_config_t config = {
            .url = url_buff,
            .event_handler = http_event_handler,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);

        esp_err_t err = esp_http_client_open(client, 0);
        if(err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            vTaskDelete(NULL);
            return;
        }

        int total_len = esp_http_client_fetch_headers(client);
        if(total_len < 0)
        {
            ESP_LOGE(TAG, "Failed to get content length");
            esp_http_client_cleanup(client);
            vTaskDelete(NULL);
            return;
        }

        int bytes_written = 0;
        while(bytes_written < total_len)
        {
            //等待数据请求位值1
            xEventGroupWaitBits(event_group,WIFI_ask_mp3_BIT,true,true,portMAX_DELAY);

            //读取分段
            ESP_LOGI(TAG, "mp3 bytes_written:%d / %d",bytes_written,total_len);
            bytes_read = 0;
            bytes_read = esp_http_client_read(client,mp3_buf,bytes_to_read);
            if(bytes_read <= 0)
            {
                ESP_LOGE(TAG, "Failed to read data");
                esp_http_client_cleanup(client);
                vTaskDelete(NULL);
                //重启摆烂
                esp_restart();
                return;
            }

            //通知对方
            xEventGroupSetBits(event_group, WIFI_rpy_mp3_BIT);

            //预备读取下一段
            bytes_written += bytes_read;
        }

        //等待数据请求位值1
        xEventGroupWaitBits(event_group,WIFI_ask_mp3_BIT,true,true,portMAX_DELAY);
        //表示读完了
        bytes_read = 0;
        //通知对方
        xEventGroupSetBits(event_group, WIFI_rpy_mp3_BIT);

        esp_http_client_cleanup(client);
        ESP_LOGI(TAG, "Download complete");

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    int ret;

    WM8978_Init();				//初始化WM8978
	WM8978_HPvol_Set(40,40);	//耳机音量设置
	WM8978_SPKvol_Set(60);		//喇叭音量设置
    WM8978_ADDA_Cfg(1,0);	    //开启DAC
	WM8978_Input_Cfg(0,0,0);    //关闭输入通道
	WM8978_Output_Cfg(1,0);	    //开启DAC输出   
    WM8978_I2S_Cfg(2,0);
    WM8978_MIC_Gain(45);
    WM8978_AUX_Gain(7);

    wifi_init_sta_ap();


    audio_pipeline_handle_t pipeline;
    audio_element_handle_t i2s_stream_writer, mp3_decoder;

    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    ESP_LOGI(TAG, "[ 2 ] Create audio pipeline, add all elements to pipeline, and subscribe pipeline event");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG, "[2.1] Create mp3 decoder to decode mp3 file and set custom read callback");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
    audio_element_set_read_cb(mp3_decoder, mp3_music_read_cb, NULL);

    ESP_LOGI(TAG, "[2.2] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = {                                              \
        .type = AUDIO_STREAM_WRITER,                                                \
        .i2s_config = {                                                             \
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),      \
            .sample_rate = 44100,                                                   \
            .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,                           \
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,                           \
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,                      \
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2 | ESP_INTR_FLAG_IRAM,          \
            .dma_buf_count = 3,                                                     \
            .dma_buf_len = 300,                                                     \
            .use_apll = true,                                                       \
            .tx_desc_auto_clear = true,                                             \
            .fixed_mclk = 0                                                         \
        },                                                                          \
        .i2s_port = I2S_NUM_0,                                                      \
        .use_alc = false,                                                           \
        .volume = 0,                                                                \
        .out_rb_size = I2S_STREAM_RINGBUFFER_SIZE,                                  \
        .task_stack = I2S_STREAM_TASK_STACK,                                        \
        .task_core = I2S_STREAM_TASK_CORE,                                          \
        .task_prio = I2S_STREAM_TASK_PRIO,                                          \
        .stack_in_ext = false,                                                      \
        .multi_out_num = 0,                                                         \
        .uninstall_drv = true,                                                      \
        .need_expand = false,                                                       \
        .expand_src_bits = I2S_BITS_PER_SAMPLE_16BIT,                               \
        .buffer_len = I2S_STREAM_BUF_SIZE,                                          \
    };
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[2.3] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    ESP_LOGI(TAG, "[2.4] Link it together [mp3_music_read_cb]-->mp3_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[2] = {"mp3", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 2);

    ESP_LOGI(TAG, "[ 4 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.1] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[ 5.1 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);

    //初始化任务music_play
    music_play_task_handle = xTaskCreateStatic((TaskFunction_t)music_play_task,                 //任务函数
                    (const char*)"music_play",                                                  //任务名称
                    (uint32_t)MUSIC_PLAY_STK_SIZE,                                              //任务堆栈大小
                    (void*)NULL,                                                                //传递给任务函数的参数
                    (UBaseType_t)MUSIC_PLAY_TASK_PRIO,                                          //任务优先级
                    (StackType_t*)&music_play_task_stack,                                       //任务堆栈
                    (StaticTask_t*)&music_play_task_tcb);                                       //任务控制块

    while (1) 
    {
        audio_event_iface_msg_t msg;
        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            continue;
        }

        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) mp3_decoder
            && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t music_info = {0};
            audio_element_getinfo(mp3_decoder, &music_info);
            ESP_LOGI(TAG, "[ * ] Receive music info from mp3 decoder, sample_rates=%d, bits=%d, ch=%d",
                     music_info.sample_rates, music_info.bits, music_info.channels);
            int ret = i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
            ESP_LOGI(TAG, "i2s_stream_set_clk:%d---------------\n",ret);
        }

        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
            && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
            && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG, "[ * ] Stop event received");


            ESP_LOGI(TAG, "[ * ] [mode] tap event");
            audio_pipeline_stop(pipeline);
            audio_pipeline_wait_for_stop(pipeline);
            audio_pipeline_terminate(pipeline);
            audio_pipeline_reset_ringbuffer(pipeline);
            audio_pipeline_reset_elements(pipeline);
            audio_pipeline_run(pipeline);
        }


        ESP_LOGI(TAG,"msg:%d,%d,%d,%d\n",(int)msg.source_type,(int)msg.source,(int)msg.cmd,(int)msg.data);

       vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

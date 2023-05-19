#ifndef __FML_WIFISNIFFER_H__
#define __FML_WIFISNIFFER_H__

#include "stdio.h"

/*
            Promiscusous Packet Structure


                                                    WIFI PACKET - RxControl
                                                    (wifi_pkt_rx_ctrl_t)

            WIFI PROMISCUOUS PACKET         ------>                                                                                             WIFI IEEE80211 PACKET - MAC HDR 
            (wifi_promiscuous_pkt_t)                                                                                                            (wifi_ieee80211_mac_hdr_t)                              WIFI IEEE80211 PACKET - NETWORK DATA
                                                     WIFI PACKET - PAYLOAD          ------->      WIFI IEEE80211 PACKET             -------->                                                           (uint8_t*)    
                                                    (uint8_t *)                                   (wifi_ieee80211_packet_t)                     WIFI IEEE80211 PACKET - PAYLOAD             ------->                
                                                                                                                                                (uint8_t *)                                             WIFI IEEE80211 PACKET - CSUM
                                                                                                                                                                                                        (uint32_t)    

*/

/**< DEFINE---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
#define FML_WIFISNIFFER_WIFI_CHANNEL_MAX                                    (13)                            //通道最大数
#define FML_WIFISNIFFER_WIFI_CHANNEL_SWITCH_INTERVAL                        (200)                           //切换不同通道的间隔时间
#define FML_WIFISNIFFER_TASK_PRIO                                           (2)                             //任务优先级
#define FML_WIFISNIFFER_STK_SIZE                                            (2048*3)                        //任务堆栈大小
#define FML_WIFISNIFFER_TASK_NAME                                           "FML_WIFISNIFFER_TASK"          //任务名

/**< STRUCT---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
typedef struct {
    unsigned frame_ctrl:16;
    unsigned duration_id:16;
    uint8_t addr1[6]; /* receiver address */
    uint8_t addr2[6]; /* sender address */
    uint8_t addr3[6]; /* filtering address */
    unsigned sequence_ctrl:16;
    uint8_t addr4[6]; /* optional */
} wifi_ieee80211_mac_hdr_t;

typedef struct {
    wifi_ieee80211_mac_hdr_t hdr;
    uint8_t payload[0]; /* network data ended with 4 bytes csum (CRC32) */
} wifi_ieee80211_packet_t;

/**< API---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/*
*@fn        fml_wifisniffer_init
*@brief     WIFI嗅探初始化
*@param     none
*@return    错误码
*/
int fml_wifisniffer_init(void);

/*
*@fn        fml_wifisniffer_stop
*@brief     停止嗅探
*@param     none
*@return    错误码
*/
int fml_wifisniffer_stop(void);

/*
*@fn        fml_wifisniffer_start
*@brief     开始嗅探
*@param     none
*@return    错误码
*/
int fml_wifisniffer_start(void);

/*
*@fn        fml_wifisniffer_SetChannelInterval
*@brief     设置通道间隔时间
*@param     interval                -[in]           间隔时间
*@return    错误码
*/
int fml_wifisniffer_SetChannelInterval(uint32_t interval);

/*
*@fn        fml_wifisniffer_SetRssi
*@brief     设置RSSI的范围: 0 ~ max_rssi
*@param     max_rssi                -[in]           最大rssi值
*@return    错误码
*/
int fml_wifisniffer_SetRssi(int max_rssi);

/*
*@fn        fml_wifisniffer_GetLastDevAddr
*@brief     获取最近一次捕捉的设备地址
*@param     buf                     -[out]          返回的地址缓冲区
*@param     len                     -[in]           缓冲区大小
*@return    错误码
*/
int fml_wifisniffer_GetLastDevAddr(char *buf, uint8_t len);

/*
*@fn        fml_wifisniffer_GetLastDevBssId
*@brief     获取最近一次捕捉的设备bssid
*@param     buf                     -[out]          返回的地址缓冲区
*@param     len                     -[in]           缓冲区大小
*@return    错误码
*/
int fml_wifisniffer_GetLastDevBssId(char *buf, uint8_t len);

/*
*@fn        fml_wifisniffer_GetLastDevChannel
*@brief     获取最近一次捕捉的设备所在通道
*@param     buf                     -[out]          返回的通道
*@return    错误码
*/
int fml_wifisniffer_GetLastDevChannel(uint8_t *buf);

/*
*@fn        fml_wifisniffer_SendDeauthFrame
*@brief     发送解除认证帧
*@param     dst                     -[in]           目标地址
*@param     dlen                    -[in]           目标地址大小
*@param     bssid                   -[in]           目标网络地址
*@param     blen                    -[in]           目标网络地址大小
*@return    错误码
*/
int fml_wifisniffer_SendDeauthFrame(char *dst, uint8_t dlen, char *bssid, uint8_t blen);


#endif




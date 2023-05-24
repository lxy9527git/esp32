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


/*
 * 要连接的路由器名字和密码
 */

//路由器的名字
#define GATEWAY_SSID "Xiaomi_FBBC"
//连接的路由器密码
#define GATEWAY_PASSWORD "87654321."

//数据包大小
#define EXAMPLE_DEFAULT_PKTSIZE 1024

#define UDP_CLIENT_TASK_PRIO                                           (3)                                     //任务优先级
#define UDP_CLIENT_STK_SIZE                                            (2048*3)                                //任务堆栈大小
#define UDP_SERVICE_TASK_PRIO                                          (3)                                     //任务优先级
#define UDP_SERVICE_STK_SIZE                                           (2048*3)                                //任务堆栈大小
#define TCP_CLIENT_TASK_PRIO                                           (3)                                     //任务优先级
#define TCP_CLIENT_STK_SIZE                                            (2048*3)                                //任务堆栈大小
#define TCP_SERVICE_TASK_PRIO                                          (3)                                     //任务优先级
#define TCP_SERVICE_STK_SIZE                                           (2048*3)                                //任务堆栈大小

/*
 * station模式时候，服务器地址配置
 */
//服务器的地址：这里的 255.255.255.255是在局域网发送，不指定某个设备
#define SERVER_IP "192.168.4.255"
//端口号
#define SERVICE_PORT 3721

#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

#define WIFI_CONNECTED_BIT              BIT0
#define WIFI_MASTER_CONNECTED_BIT       BIT1

#define TCP_SERVICE_IP                  "192.168.4.2"       //服务器IP地址
#define TCP_SERVICE_PORT                (8080)              //服务器端口号           




TaskHandle_t    udp_client_task_handle;                                                        //任务句柄
StackType_t     udp_client_task_stack[UDP_CLIENT_STK_SIZE];                                    //任务栈
StaticTask_t    udp_client_task_tcb;                                                           //任务控制块
TaskHandle_t    udp_service_task_handle;                                                       //任务句柄
StackType_t     udp_service_task_stack[UDP_SERVICE_STK_SIZE];                                  //任务栈
StaticTask_t    udp_service_task_tcb;                                                          //任务控制块

TaskHandle_t    tcp_client_task_handle;                                                        //任务句柄
StackType_t     tcp_client_task_stack[TCP_CLIENT_STK_SIZE];                                    //任务栈
StaticTask_t    tcp_client_task_tcb;                                                           //任务控制块
TaskHandle_t    tcp_service_task_handle;                                                       //任务句柄
StackType_t     tcp_service_task_stack[TCP_SERVICE_STK_SIZE];                                  //任务栈
StaticTask_t    tcp_service_task_tcb;                                                          //任务控制块

static EventGroupHandle_t udp_event_group;
static const char* TAG = "udp";
static const char* TAG2 = "tcp";
struct sockaddr_in remote_addr;
static int udp_client_socket_fd;
static char mac_addr_str[128];


static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data);
static void udp_wifi_init_sta(void);
static esp_err_t create_udp_client();
static void udp_client_task(void *parameter);
static void udp_service_task(void *parameter);
static void tcp_client_task(void *parameter);
static void tcp_service_task(void *parameter);
static char* get_sock_state(int s);

static void event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)                           //station模式开启回调
    {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)               //断开与路由器的连接
    {
        esp_wifi_connect();
        xEventGroupClearBits(udp_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)                         //成功连接到路由器，获取到IP地址
    {
        ip_event_got_ip_t *event;
        event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "event_handler:SYSTEM_EVENT_STA_GOT_IP!");
        ESP_LOGI(TAG, "got ip:" IPSTR "\n", IP2STR(&event->ip_info.ip));
        sprintf(mac_addr_str, "%d.%d.%d.%d",IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(udp_event_group, WIFI_CONNECTED_BIT);
    }
    else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)                 //作为AP热点模式，检测到有子设备接入
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG,"station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
        xEventGroupSetBits(udp_event_group, WIFI_MASTER_CONNECTED_BIT);
    }
    else if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)              //作为AP热点模式，检测到有子设备断开了连接
    {
		ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED");                                         
		wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
		ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",MAC2STR(event->mac), event->aid);
        xEventGroupClearBits(udp_event_group, WIFI_MASTER_CONNECTED_BIT);
    }
}

// 获取当前子网掩码
static void get_subnet_mask() {
    // 获取默认网络接口
    esp_netif_t *netif = esp_netif_get_default_netif();

     // 获取网络接口的 IP 信息
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif,&ip_info);

    // 打印子网掩码
    printf("Subnet mask: %s\n", ip4addr_ntoa(&ip_info.netmask));
}

static void udp_wifi_init_sta_ap(void)
{
    nvs_flash_init();
    ESP_ERROR_CHECK(esp_netif_init());                                  //初始化内部的lwip(Light weight internet protocol)
    udp_event_group = xEventGroupCreate();                              //创建事件组
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

static int create_udp_client()
{
    ESP_LOGI(TAG, "create_udp_client()");
    //打印下要连接的服务器地址
    ESP_LOGI(TAG, "connecting to %s:%d",SERVER_IP, SERVICE_PORT);

    udp_client_socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if(udp_client_socket_fd < 0)
    {
        return -1;
    }

    remote_addr.sin_family      =   AF_INET;
    remote_addr.sin_port        =   htons(SERVICE_PORT);
    remote_addr.sin_addr.s_addr =   inet_addr(SERVER_IP);

    return 0;
}

static void udp_client_task(void *parameter)
{
    ESP_LOGI(TAG, "task udp_client start.....\n");

    //等待是否已经成功连接到路由器的标志位
    xEventGroupWaitBits(udp_event_group,WIFI_CONNECTED_BIT,false,true,portMAX_DELAY);

    //5秒之后开始创建socket
    ESP_LOGI(TAG, "esp32 is ready !!! create udp client or connect servece after 5s...\n");
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    //创建客户端并且检查是否创建成功
    ESP_LOGI(TAG, "Now Let us create udp client ...\n");
    if(create_udp_client() != 0)
    {
        ESP_LOGI(TAG, "client create socket error, stop!!!\n");
        vTaskDelete(NULL);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "client create socket succeed!!!\n");
    }

    //创建一个发送和接收数据的任务
    while(1)
    {
        char tx_buffer[128];
        sprintf(tx_buffer, "Hello from client!");
        int err = sendto(udp_client_socket_fd, tx_buffer, strlen(tx_buffer), MSG_DONTWAIT, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
        if (err < 0) 
        {
            ESP_LOGI(TAG, "client send Error occurred during sending: errno %d", errno);
        }
        else
        {
            ESP_LOGI(TAG, "client send success !!!\n");
        }

         char rx_buffer[128];
        int len = recvfrom(udp_client_socket_fd, rx_buffer, sizeof(rx_buffer) - 1, MSG_DONTWAIT, (struct sockaddr *)&remote_addr, (socklen_t *)&remote_addr);
        if (len < 0) {
            ESP_LOGI(TAG, "client recvfrom failed: errno %d", errno);
        } else {
            rx_buffer[len] = 0;
            ESP_LOGI(TAG, "client Received %d bytes from %s:%d ", len, SERVER_IP, ntohs(remote_addr.sin_port));
            ESP_LOGI(TAG, "%s\n", rx_buffer);
        }
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

static void udp_service_task(void *parameter)
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    ESP_LOGI(TAG, "task udp_service start.....\n");
    //等待是否已经成功连接到路由器的标志位
    xEventGroupWaitBits(udp_event_group,WIFI_CONNECTED_BIT,false,true,portMAX_DELAY);


    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERVICE_PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
    // memset(addr_str,0,sizeof(addr_str));
    // memcpy(addr_str,mac_addr_str,sizeof(mac_addr_str) - 1);
    // dest_addr.sin_addr.s_addr =   inet_addr(addr_str);
    // ESP_LOGI(TAG, "Service IP:%s,%lu\n",addr_str,dest_addr.sin_addr.s_addr);

    int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
    if (sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d\n", errno);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Service Socket created\n");
    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d\n", errno);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Service Socket bound, port %d\n", SERVICE_PORT);

    while (1) {
        ESP_LOGI(TAG, "Service Waiting for data...\n");get_subnet_mask();
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

        if (len < 0) {
            ESP_LOGE(TAG, "Service recvfrom failed: errno %d\n", errno);
        }
        else {
            rx_buffer[len] = 0;
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
            ESP_LOGI(TAG, "Service Received %d bytes from %s:", len, addr_str);
            ESP_LOGI(TAG, "%s\n", rx_buffer);
        }
        char tx_buffer[128];
        sprintf(tx_buffer, "Hello from Service!");
        int err = sendto(sock, tx_buffer, strlen(tx_buffer), 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Service Send Error occurred during sending: errno %d", errno);
        }
        else{
            ESP_LOGI(TAG, "Service send success !!!\n");
        }

        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

static void tcp_client_task(void *parameter)
{
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    //等待是否已经成功连接到路由器的标志位
    xEventGroupWaitBits(udp_event_group,WIFI_MASTER_CONNECTED_BIT,false,true,portMAX_DELAY);

    //5秒之后开始创建socket
    ESP_LOGI(TAG2, "esp32 is ready !!! create tcp client or connect servece after 5s...\n");
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(TCP_SERVICE_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TCP_SERVICE_PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
    inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

    int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (sock < 0) {
        ESP_LOGE(TAG2, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG2, "Socket created, connecting to %s:%d", addr_str, TCP_SERVICE_PORT);

    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG2, "Socket unable to connect: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG2, "Successfully connected");

    while(1)
    {
        char data[] = "Hello, world!";
        int err = send(sock, data, strlen(data), 0);
        if (err < 0) {
            ESP_LOGE(TAG2, "Error occurred during sending: errno %d", errno);
        }

        char recv_buf[64];
        int len = recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG2, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG2, "Connection closed");
        } else {
            recv_buf[len] = '\0';
            ESP_LOGI(TAG2, "Received %d bytes: %s", len, recv_buf);
        }


        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

static void tcp_service_task(void *parameter)
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    //等待是否已经成功连接到路由器的标志位
    xEventGroupWaitBits(udp_event_group,WIFI_MASTER_CONNECTED_BIT,false,true,portMAX_DELAY);

    //5秒之后开始创建socket
    ESP_LOGI(TAG2, "esp32 is ready !!! create tcp client or connect servece after 5s...\n");
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(TCP_SERVICE_PORT);
    addr_family = AF_INET;
    ip_protocol = IPPROTO_IP;
    inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);

    int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (sock < 0) {
        ESP_LOGE(TAG2, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG2, "Socket created, connecting to %s:%d", addr_str, TCP_SERVICE_PORT);
    ESP_LOGI(TAG2, "after socket Socket state:%s",get_sock_state(sock));

    int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG2, "Socket unable to bind: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG2, "Socket bound, port %d", TCP_SERVICE_PORT);
    ESP_LOGI(TAG2, "after bind Socket state:%s",get_sock_state(sock));

    err = listen(sock, 1);//backlog：指定服务器套接字的最大连接数，即等待连接队列的长度。
    if (err != 0) {
        ESP_LOGE(TAG2, "Error occurred during listen: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG2, "after listen Socket state:%s",get_sock_state(sock));

    struct sockaddr_in  source_addr;
    uint32_t            source_addr_len = sizeof(source_addr);
    int send_sock = accept(sock, (struct sockaddr *)&source_addr, &source_addr_len);
    if (send_sock < 0) {
        ESP_LOGE(TAG2, "Unable to accept connection: errno %d", errno);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    inet_ntoa_r(source_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
    ESP_LOGI(TAG2, "Socket accepted ip address: %s", addr_str);
    ESP_LOGI(TAG2, "after accept Socket state:%s",get_sock_state(sock));

    // int keepAlive = 1; // 开启keepalive属性
    // int keepIdle = 60; // 如该连接在60秒内没有任何数据往来,则进行探测 
    // int keepInterval = 5; // 探测时发包的时间间隔为5 秒
    // int keepCount = 3; // 探测尝试的次数.如果第1次探测包就收到响应了,则后2次的不再发.

    // setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepAlive, sizeof(keepAlive));
    // setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, (void*)&keepIdle, sizeof(keepIdle));
    // setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, (void *)&keepInterval, sizeof(keepInterval));
    // setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, (void *)&keepCount, sizeof(keepCount));

    while(1)
    {
        // //判断连接状态
        // struct sockaddr_storage     peer_addr;
        // uint32_t                    peer_addr_len = sizeof(peer_addr);
        // if (getpeername(sock, &peer_addr, &peer_addr_len) == 0) {
        //     char ip_str[INET6_ADDRSTRLEN];
        //     int port;
        //     if (peer_addr.ss_family == AF_INET) {
        //         struct sockaddr_in *s = (struct sockaddr_in *)&peer_addr;
        //         port = ntohs(s->sin_port);
        //         inet_ntop(AF_INET, &s->sin_addr, ip_str, sizeof(ip_str));
        //     } else { // AF_INET6
        //         struct sockaddr_in6 *s = (struct sockaddr_in6 *)&peer_addr;
        //         port = ntohs(s->sin6_port);
        //         inet_ntop(AF_INET6, &s->sin6_addr, ip_str, sizeof(ip_str));
        //     }
        //     ESP_LOGI(TAG2, "getpeername: %s,%d", ip_str,port);
        // } else {
        //     ESP_LOGI(TAG2, "getpeername error");
        // }


        //ESP_LOGI(TAG2, "fcntl:%d",fcntl(sock,F_GETFL,O_RDWR));

        int len = recv(send_sock, rx_buffer, sizeof(rx_buffer) - 1, 0);ESP_LOGI(TAG2, "after recv Socket state:%s",get_sock_state(sock));
        // Error occurred during receiving
        if (len < 0) {
            ESP_LOGE(TAG2, "recv failed: errno %d,%d", errno,len);
        }
        else {
            rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
            ESP_LOGI(TAG2, "Received %d bytes: %s", len, rx_buffer);
            int err = send(send_sock, rx_buffer, len, 0);ESP_LOGI(TAG2, "after send Socket state:%s",get_sock_state(sock));
            if (err < 0) {
                ESP_LOGE(TAG2, "Error occurred during sending: errno %d", errno);
            }
            ESP_LOGI(TAG2, "Sent %d bytes: %s", err, rx_buffer);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

static char* get_sock_state(int s)
{
    static char ret[256];
    int error = -1;
    socklen_t len = sizeof(error);

    memset(ret,0,sizeof(ret));
    if(getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &len) != 0)
    {
        sprintf(ret,"getsockopt fail\n");
        return ret;
    }

    if(error == 0)
    {
        sprintf(ret,"socket conneted\n");
    }
    else
    {
        sprintf(ret,"socket error:%s\n",strerror(error));
    }

    return ret;
}

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

    udp_wifi_init_sta_ap();

    // //初始化任务udp客户端
    // udp_client_task_handle = xTaskCreateStatic((TaskFunction_t)udp_client_task,                 //任务函数
    //                 (const char*)"UDP_CLIENT",                                                  //任务名称
    //                 (uint32_t)UDP_CLIENT_STK_SIZE,                                              //任务堆栈大小
    //                 (void*)NULL,                                                                //传递给任务函数的参数
    //                 (UBaseType_t)UDP_CLIENT_TASK_PRIO,                                          //任务优先级
    //                 (StackType_t*)&udp_client_task_stack,                                       //任务堆栈
    //                 (StaticTask_t*)&udp_client_task_tcb);                                       //任务控制块

    // //初始化任务udp服务端
    // udp_service_task_handle = xTaskCreateStatic((TaskFunction_t)udp_service_task,                 //任务函数
    //                 (const char*)"UDP_SERVICE",                                                  //任务名称
    //                 (uint32_t)UDP_SERVICE_STK_SIZE,                                              //任务堆栈大小
    //                 (void*)NULL,                                                                //传递给任务函数的参数
    //                 (UBaseType_t)UDP_SERVICE_TASK_PRIO,                                          //任务优先级
    //                 (StackType_t*)&udp_service_task_stack,                                       //任务堆栈
    //                 (StaticTask_t*)&udp_service_task_tcb);                                       //任务控制块

    // //初始化任务tcp客户端
    // tcp_client_task_handle = xTaskCreateStatic((TaskFunction_t)tcp_client_task,                 //任务函数
    //                 (const char*)"TCP_CLIENT",                                                  //任务名称
    //                 (uint32_t)TCP_CLIENT_STK_SIZE,                                              //任务堆栈大小
    //                 (void*)NULL,                                                                //传递给任务函数的参数
    //                 (UBaseType_t)TCP_CLIENT_TASK_PRIO,                                          //任务优先级
    //                 (StackType_t*)&tcp_client_task_stack,                                       //任务堆栈
    //                 (StaticTask_t*)&tcp_client_task_tcb);                                       //任务控制块

    //初始化任务tcp服务端
    tcp_service_task_handle = xTaskCreateStatic((TaskFunction_t)tcp_service_task,                 //任务函数
                    (const char*)"TCP_SERVICE",                                                  //任务名称
                    (uint32_t)TCP_SERVICE_STK_SIZE,                                              //任务堆栈大小
                    (void*)NULL,                                                                //传递给任务函数的参数
                    (UBaseType_t)TCP_SERVICE_TASK_PRIO,                                          //任务优先级
                    (StackType_t*)&tcp_service_task_stack,                                       //任务堆栈
                    (StaticTask_t*)&tcp_service_task_tcb);                                       //任务控制块

    while (1) {
        //printf("app_main running...\n");

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

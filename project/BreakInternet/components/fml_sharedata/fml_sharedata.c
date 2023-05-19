#include "fml_sharedata.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include <string.h>

/**< DEFINE---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
#define FML_SHAREDATA_QUEUE_SIZE                              (20)                                                      //队列大小

/**< STRUCT---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
typedef struct
{
    fml_sharedata_EventType_e                       event;                                                              //回调事件
    fml_sharedata_CallBack                          cb;                                                                 //回调函数
}   fml_sharedata_CallBackType_t;

struct fml_sharedata_ControlBlock
{
    TaskHandle_t                                    task_handle;                                                        //任务句柄
    StackType_t                                     task_stack[FML_SHAREDATA_STK_SIZE];                                 //任务栈
    StaticTask_t                                    task_tcb;                                                           //任务控制块
    QueueHandle_t                                   mQueue;
    SemaphoreHandle_t                               mMutex;
    fml_sharedata_GlobalData_t                      sharedata;
    fml_sharedata_CallBackType_t                    notify_info;
};

/**< VARIABLE---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/**< STATICFUN---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
static void fml_sharedata_task(void *parameter);


static void fml_sharedata_task(void *parameter)
{
    fml_sharedata_CallBackType_t notify_info;
    fml_sharedata_Handle_t handle = parameter;

    while(1)
    {
        if(xQueueReceive(handle->mQueue, &notify_info, 0) == pdTRUE)
        {
            if(notify_info.cb != NULL)
            {
                notify_info.cb(&notify_info.event);
            }
        }
        vTaskDelay(FML_SHAREDATA_TASK_DELAY / portTICK_PERIOD_MS);  
    }
}
/**< API---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/*
*@fn        fml_sharedata_Register
*@brief     共享数据框架注册
*@param     sharedata                               -[in]           共享数据信息
*@return    无
*/
fml_sharedata_Handle_t fml_sharedata_Register(fml_sharedata_GlobalData_t sharedata)
{
    struct fml_sharedata_ControlBlock* ret = NULL;

    ret = pvPortMalloc(sizeof(struct fml_sharedata_ControlBlock));
    if(ret == NULL)return NULL;

    ret->mMutex = xSemaphoreCreateRecursiveMutex();
    if(ret->mMutex == NULL)
    {
        vPortFree(ret);
        return NULL;
    }

    ret->mQueue = xQueueCreate(FML_SHAREDATA_QUEUE_SIZE,sizeof(fml_sharedata_CallBackType_t));
    if(ret->mQueue == NULL)
    {
        vSemaphoreDelete(ret->mMutex);
        vPortFree(ret);
        return NULL;
    }

    xQueueReset(ret->mQueue);
    ret->sharedata = sharedata;
    ret->notify_info.cb = NULL;
    ret->notify_info.event = FML_SHAREDATA_EV_NONE;

    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    
    taskENTER_CRITICAL(&lock);           //进入临界区

    //初始化任务
    ret->task_handle = xTaskCreateStatic((TaskFunction_t)fml_sharedata_task,              //任务函数
                    (const char*)FML_SHAREDATA_TASK_NAME,                                 //任务名称
                    (uint32_t)FML_SHAREDATA_STK_SIZE,                                     //任务堆栈大小
                    (void*)ret,                                                           //传递给任务函数的参数
                    (UBaseType_t)FML_SHAREDATA_TASK_PRIO,                                 //任务优先级
                    (StackType_t*)&ret->task_stack,                                       //任务堆栈
                    (StaticTask_t*)&ret->task_tcb);                                       //任务控制块

    if(ret->task_handle == NULL)
    {
        vSemaphoreDelete(ret->mMutex);
        vQueueDelete(ret->mQueue);
        vPortFree(ret);
        ret = NULL;
    }

    taskEXIT_CRITICAL(&lock);            //退出临界区

    return ret;
}

/*
*@fn        fml_sharedata_RegisterCallBack
*@brief     共享数据框架通知回调函数注册
*@param     handle                                  -[in]           句柄
*@param     cb                                      -[in]           通知回调函数
*@return    错误码
*/
int fml_sharedata_RegisterCallBack(fml_sharedata_Handle_t handle, fml_sharedata_CallBack cb)
{
    if(handle != NULL || cb != NULL)
    {
        handle->notify_info.cb = cb;
        return 0;
    }
    return -1;
}

/*
*@fn        fml_sharedata_Setting
*@brief     共享数据框架更新共享数据
*@param     handle                                  -[in]           句柄
*@param     offset                                  -[in]           偏移地址
*@param     data                                    -[in]           数据地址
*@param     data_len                                -[in]           数据长度
*@param     wait_time                               -[in]           等待事件，tick单位
*@return    错误码
*/
int fml_sharedata_Setting(fml_sharedata_Handle_t handle, uint32_t offset, uint8_t* data, uint32_t data_len, uint32_t wait_time)
{
    int ret = 0;

    if((offset + data_len > handle->sharedata.global_data_len) || data == NULL || data_len == 0)return -1;

    //lock
    if(xSemaphoreTakeRecursive(handle->mMutex, wait_time) != pdTRUE)return -2;
    //更新数据
    memcpy(handle->sharedata.global_data + offset, data, data_len);
    handle->notify_info.event = FML_SHAREDATA_EV_UPDATED;
    //触发回调函数
    fml_sharedata_CallBackType_t info;
    memcpy(&info, &handle->notify_info, sizeof(info));
    if(xQueueSend(handle->mQueue, &info, 0) != pdTRUE)ret = -3;
    //unlock
    xSemaphoreGiveRecursive(handle->mMutex);

    return ret;
}

/*
*@fn        fml_sharedata_Getting
*@brief     共享数据框架获取共享数据
*@param     handle                                  -[in]           句柄
*@param     offset                                  -[in]           偏移地址
*@param     data                                    -[out]          数据地址
*@param     data_len                                -[in]           数据长度
*@param     wait_time                               -[in]           等待事件，tick单位
*@return    错误码
*/
int fml_sharedata_Getting(fml_sharedata_Handle_t handle, uint32_t offset, uint8_t* data, uint32_t data_len, uint32_t wait_time)
{
    if((offset + data_len > handle->sharedata.global_data_len) || data == NULL || data_len == 0)return -1;

    //lock
    if(xSemaphoreTakeRecursive(handle->mMutex, wait_time) != pdTRUE)return -2;
    //更新数据
     memcpy(data, handle->sharedata.global_data + offset, data_len);
    //unlock
    xSemaphoreGiveRecursive(handle->mMutex);

    return 0;
}














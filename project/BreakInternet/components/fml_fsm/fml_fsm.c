#include "fml_fsm.h"
#include "fml_sharedata.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include <string.h>

/**< DEFINE---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
#define FML_FSM_QUEUE_SIZE                              (20)                                                            //队列大小
/**< STRUCT---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
//状态机类型
struct fml_fsm_ControlBlock
{
    TaskHandle_t                                    task_handle;                                                        //任务句柄
    StackType_t                                     task_stack[FML_FSM_STK_SIZE];                                       //任务栈
    StaticTask_t                                    task_tcb;                                                           //任务控制块
    int                                             curState;                                                           //当前状态
    fml_fsm_table_t                                 *pFsmTable;                                                         //状态表
    int                                             size;                                                               //表的项数
    QueueHandle_t                                   mQueue;                                                             //队列
    fml_sharedata_Handle_t                          sharedata_handle;                                                   //自身结构体句柄
};

/**< VARIABLE---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/


/**< STATICFUN---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

static void fml_fsm_task(void *parameter);


static void fml_fsm_task(void *parameter)
{
    int event;
    fml_fsm_Handle_t handle = parameter;
    fml_fsm_table_t *pActTable;
    int (*eventActFun)();   //函数指针初始化为空
    int NextState;
    int flag;               //标识是否满足条件

    while(1)
    {
        if(handle != NULL)
        {
            flag = 0;
            eventActFun = NULL;
            pActTable = handle->pFsmTable;
            if(xQueueReceive(handle->mQueue, &event, 0) == pdTRUE)
            {
                //获取当前动作函数
                for(int i=0; i<handle->size; i++)
                {
                    if(event == pActTable[i].event && handle->curState == pActTable[i].CurState)
                    {
                        flag = 1;
                        eventActFun = pActTable[i].eventActFun;
                        NextState = pActTable[i].NextState;
                        break;
                    }
                }

                //如果满足条件了
                if(flag)
                {
                    //动作执行
                    if(eventActFun)
                    {
                        if(eventActFun() == 0)
                        {
                            //跳转到下一个状态
                            fml_sharedata_Setting(handle->sharedata_handle, OFFSET(struct fml_fsm_ControlBlock,curState), &NextState, sizeof(NextState), 0);
                        }
                    }
                }
            }
        }
        vTaskDelay(FML_FSM_TASK_DELAY / portTICK_PERIOD_MS);  
    }
}
/**< API---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*
*@fn        fml_fsm_Register
*@brief     状态机注册
*@param     pTable                                  -[in]           状态表
*@param     CurState                                -[in]           当前状态
*@param     size                                    -[in]           表的项数
*@return    句柄
*/
fml_fsm_Handle_t fml_fsm_Register(fml_fsm_table_t *pTable, int CurState, int size)
{
    struct fml_fsm_ControlBlock *ret = NULL;

    ret = pvPortMalloc(sizeof(struct fml_fsm_ControlBlock));
    if(ret == NULL)return NULL;

    fml_sharedata_GlobalData_t sharedata;
    sharedata.global_data = (uint8_t*)ret;
    sharedata.global_data_len = sizeof(struct fml_fsm_ControlBlock);
    ret->sharedata_handle = fml_sharedata_Register(sharedata);
    if(ret->sharedata_handle == NULL)
    {
        vPortFree(ret);
        return NULL;
    }

    ret->mQueue = xQueueCreate(FML_FSM_QUEUE_SIZE,sizeof(ret->pFsmTable->event));
    if(ret->mQueue == NULL)
    {
        vPortFree(ret);
        return NULL;
    }

    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    
    taskENTER_CRITICAL(&lock);           //进入临界区

    //初始化结构体
    ret->curState = CurState;
    ret->pFsmTable = pTable;
    ret->size = size;


    //初始化任务
    ret->task_handle = xTaskCreateStatic((TaskFunction_t)fml_fsm_task,                  //任务函数
                    (const char*)FML_FSM_TASK_NAME,                                     //任务名称
                    (uint32_t)FML_FSM_STK_SIZE,                                         //任务堆栈大小
                    (void*)ret,                                                         //传递给任务函数的参数
                    (UBaseType_t)FML_FSM_TASK_PRIO,                                     //任务优先级
                    (StackType_t*)&ret->task_stack,                                     //任务堆栈
                    (StaticTask_t*)&ret->task_tcb);                                     //任务控制块

    if(ret->task_handle == NULL)
    {
        vQueueDelete(ret->mQueue);
        vPortFree(ret);
        ret = NULL;
    }

    taskEXIT_CRITICAL(&lock);            //退出临界区

    return ret;
}

/*
*@fn        fml_fsm_SendEvent
*@brief     发送事件
*@param     handle                                  -[in]           句柄
*@param     event                                   -[in]           事件
*@param     wait_time                               -[in]           等待事件，tick单位
*@return    错误码
*/
int fml_fsm_SendEvent(fml_fsm_Handle_t handle, int event, uint32_t wait_time)
{
    if(xQueueSend(handle->mQueue, &event, wait_time) != pdTRUE)return -1;

    return 0;
}

/*
*@fn        fml_fsm_GetCurState
*@brief     获取当前状态
*@param     handle                                  -[in]           句柄
*@param     state                                   -[out]          返回的状态
*@return    错误码
*/
int fml_fsm_GetCurState(fml_fsm_Handle_t handle, int *state)
{
    int curState;

    if(handle == NULL || state == NULL)return -1;

    if(fml_sharedata_Getting(handle->sharedata_handle, OFFSET(struct fml_fsm_ControlBlock,curState), &curState, sizeof(curState), 0) == 0)
    {
        *state = curState;
        return 0;
    }

    return -1;
}

















#ifndef __FML_FSM_H__
#define __FML_FSM_H__

#include "stdio.h"

/**< DEFINE---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
#define FML_FSM_TASK_PRIO                                           (1)                                     //任务优先级
#define FML_FSM_STK_SIZE                                            (2048*3)                                //任务堆栈大小
#define FML_FSM_TASK_NAME                                           "FML_FSM_TASK"                          //任务名
#define FML_FSM_TASK_DELAY                                          (10)                                    //任务延时
/**< STRUCT---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
struct fml_fsm_ControlBlock;
typedef struct fml_fsm_ControlBlock* fml_fsm_Handle_t;

//定义状态表的数据类型
typedef struct
{
    int event;                  //事件
    int CurState;               //当前状态
    int (*eventActFun)();      //函数指针
    int NextState;              //下一个状态
}   fml_fsm_table_t;

/**< API---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*
*@fn        fml_fsm_Register
*@brief     状态机注册
*@param     pTable                                  -[in]           状态表
*@param     CurState                                -[in]           当前状态
*@param     size                                    -[in]           表的项数
*@return    句柄
*/
fml_fsm_Handle_t fml_fsm_Register(fml_fsm_table_t *pTable, int CurState, int size);

/*
*@fn        fml_fsm_SendEvent
*@brief     发送事件
*@param     handle                                  -[in]           句柄
*@param     event                                   -[in]           事件
*@param     wait_time                               -[in]           等待事件，tick单位
*@return    错误码
*/
int fml_fsm_SendEvent(fml_fsm_Handle_t handle, int event, uint32_t wait_time);

/*
*@fn        fml_fsm_GetCurState
*@brief     获取当前状态
*@param     handle                                  -[in]           句柄
*@param     state                                   -[out]          返回的状态
*@return    错误码
*/
int fml_fsm_GetCurState(fml_fsm_Handle_t handle, int *state);

#endif




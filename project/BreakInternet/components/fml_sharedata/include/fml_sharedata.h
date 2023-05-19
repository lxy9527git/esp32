#ifndef __FML_SHAREDATA_H__
#define __FML_SHAREDATA_H__

#include "stdio.h"

/**< DEFINE---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
#define FML_SHAREDATA_TASK_PRIO                                           (3)                                     //任务优先级
#define FML_SHAREDATA_STK_SIZE                                            (2048*3)                                //任务堆栈大小
#define FML_SHAREDATA_TASK_NAME                                           "FML_SHAREDATA_TASK"                    //任务名
#define FML_SHAREDATA_TASK_DELAY                                          (200)                                   //任务延时
#define OFFSET(Type, member)                                              ((size_t)&( ((Type*)0)->member))        //偏移地址
/**< STRUCT---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
struct fml_sharedata_ControlBlock;
typedef struct fml_sharedata_ControlBlock* fml_sharedata_Handle_t;

//事件类型枚举
typedef enum
{
    FML_SHAREDATA_EV_NONE     =   0x00,
    FML_SHAREDATA_EV_UPDATED  =   0x01,       //数据更新
}   fml_sharedata_EventType_e;

//回调函数指针
typedef void (*fml_sharedata_CallBack)(fml_sharedata_EventType_e *ev); 

//共享数据结构体
typedef struct
{
    uint8_t*    global_data;
    uint32_t    global_data_len;
}   fml_sharedata_GlobalData_t;
/**< API---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/

/*
*@fn        fml_sharedata_Register
*@brief     共享数据框架注册
*@param     sharedata                               -[in]           共享数据信息
*@return    句柄
*/
fml_sharedata_Handle_t fml_sharedata_Register(fml_sharedata_GlobalData_t sharedata);

/*
*@fn        fml_sharedata_RegisterCallBack
*@brief     共享数据框架通知回调函数注册
*@param     handle                                  -[in]           句柄
*@param     cb                                      -[in]           通知回调函数
*@return    错误码
*/
int fml_sharedata_RegisterCallBack(fml_sharedata_Handle_t handle, fml_sharedata_CallBack cb);

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
int fml_sharedata_Setting(fml_sharedata_Handle_t handle, uint32_t offset, uint8_t* data, uint32_t data_len, uint32_t wait_time);

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
int fml_sharedata_Getting(fml_sharedata_Handle_t handle, uint32_t offset, uint8_t* data, uint32_t data_len, uint32_t wait_time);


#endif




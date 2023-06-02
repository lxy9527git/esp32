#ifndef __HAL_IIC_H__
#define __HAL_IIC_H__

#include "stdio.h"
#include "driver/gpio.h"

#define WM8978_SDA                                  (GPIO_NUM_13)
#define WM8978_SCL                                  (GPIO_NUM_12)


#define		IIC_BACK_SCL_SET		                gpio_set_level(WM8978_SCL, 1)
#define		IIC_BACK_SCL_CLR		                gpio_set_level(WM8978_SCL, 0)
#define		IIC_BACK_SDA_SET		                gpio_set_level(WM8978_SDA, 1)
#define		IIC_BACK_SDA_CLR		                gpio_set_level(WM8978_SDA, 0)
#define		IIC_BACK_SDA_READ	                    gpio_get_level(WM8978_SDA)


void IIC_BACK_Init(void);
void IIC_BACK_Start(void);
void IIC_BACK_Stop(void);
void IIC_BACK_ACK(void);
void IIC_BACK_NoACK(void);
bool IIC_BACK_WaitACK(void);
void IIC_BACK_SendByte(uint8_t _byte);
uint8_t IIC_BACK_RecByte(uint8_t ack);


#endif






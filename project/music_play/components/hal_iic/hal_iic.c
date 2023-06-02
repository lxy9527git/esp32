#include "D:\esp32\esp-adf\esp-idf\components\esp_rom\include\esp32h2\rom\ets_sys.h"
#include <stdio.h>
#include "hal_iic.h"

void IIC_BACK_Start(void)
{
	IIC_BACK_SDA_SET;
	IIC_BACK_SCL_SET;
	ets_delay_us(5);
	IIC_BACK_SDA_CLR;
	ets_delay_us(6);
	IIC_BACK_SCL_CLR;
}

void IIC_BACK_Stop(void)
{
	IIC_BACK_SCL_CLR;
	IIC_BACK_SDA_CLR;
	IIC_BACK_SCL_SET;
    ets_delay_us(6);
	IIC_BACK_SDA_SET;
	ets_delay_us(6);
}

void IIC_BACK_ACK(void)
{
	IIC_BACK_SCL_CLR;
	IIC_BACK_SDA_CLR;
	ets_delay_us(2);
	IIC_BACK_SCL_SET;
	ets_delay_us(5);
	IIC_BACK_SCL_CLR;
}

void IIC_BACK_NoACK(void)
{
	IIC_BACK_SCL_CLR;
	IIC_BACK_SDA_SET;
	ets_delay_us(2);
	IIC_BACK_SCL_SET;
	ets_delay_us(5);
	IIC_BACK_SCL_CLR;
}

bool IIC_BACK_WaitACK(void)
{
	uint8_t tim = 0;
//	IIC_BACK_SCL_CLR;
	IIC_BACK_SDA_SET;
	ets_delay_us(1);
	IIC_BACK_SCL_SET;
    ets_delay_us(1);
//	delay(2);
	while(IIC_BACK_SDA_READ){
		if(++tim>=250){
			IIC_BACK_Stop();
			return false;
		}
	}
	IIC_BACK_SCL_CLR;
	return true;
}

void IIC_BACK_SendByte(uint8_t _byte)
{
	uint8_t i = 0;
    IIC_BACK_SCL_CLR;
	for (i=0; i<8; i++){
		if (_byte&0x80)	IIC_BACK_SDA_SET;
		else IIC_BACK_SDA_CLR;
		_byte <<= 1;
        ets_delay_us(2);
		IIC_BACK_SCL_SET;
		ets_delay_us(2);
        IIC_BACK_SCL_CLR;
        ets_delay_us(2);
	}
}

uint8_t IIC_BACK_RecByte(uint8_t ack)
{
	uint8_t i = 0;
	uint8_t rec_byte = 0;

	for (i=0; i<8; i++)
    {
        IIC_BACK_SCL_CLR;
        ets_delay_us(2);
        IIC_BACK_SCL_SET;
		rec_byte <<= 1;
        if (IIC_BACK_SDA_READ) rec_byte++;
        ets_delay_us(1);
	}

    if(!ack)IIC_BACK_NoACK();
    else IIC_BACK_ACK();

	return rec_byte;
}

/********************************************/
void IIC_BACK_Init(void)
{
    //gpio_config_t io_conf;
    
    // // 配置为输出模式，带上拉电阻
    // io_conf.mode = GPIO_MODE_OUTPUT;
    // io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    // // 配置GPIO的引脚号
    // io_conf.pin_bit_mask = WM8978_SCL;
    // // 应用配置
    // gpio_config(&io_conf);

    gpio_reset_pin(WM8978_SCL);
    gpio_pullup_en(WM8978_SCL);
    gpio_set_direction(WM8978_SCL, GPIO_MODE_OUTPUT);
    IIC_BACK_SCL_SET;

    // // 配置为输出模式，带上拉电阻
    // io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    // io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    // // 配置GPIO的引脚号
    // io_conf.pin_bit_mask = WM8978_SDA;
    // // 应用配置
    // gpio_config(&io_conf);
    gpio_reset_pin(WM8978_SDA);
    gpio_pullup_en(WM8978_SDA);
    gpio_set_direction(WM8978_SDA, GPIO_MODE_INPUT_OUTPUT_OD);
    IIC_BACK_SDA_SET;
}


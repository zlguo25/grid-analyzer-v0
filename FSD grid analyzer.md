项目名：grid analyzer



描述：采集电网电压和电流，让后通过fft计算到至少50次谐波分量。

采集ADC使用两个ADS8685，分别对应电压和电流。两颗ADC的SPI口使用daisy chain形式相连。

使用MCU为STM32G474RET6。

MCU侧为低压LV侧，电压电流采样侧为高压HV侧。LV和HV之间通过数字隔离芯片ISO7741实现电气隔离。



功能：

1. 和上位机通过UART通信，传输采样数据，接收指令等
2. 通过SPI向daisy chain连接的两个ADS8685发送配置参数，接收转化好的数据
3. 通过LED灯的闪烁，显示出工作状态。其中LED1每100ms翻转。LED2在AD转化和UART数据传输时每100ms翻转，其他时刻常亮。



硬件架构

grid analyzer architecture ADS8685.svg



软件架构

UART传输模块

ADS8685交互模块



状态机：

​	状态1：idle

​		等待上位机指令

​	状态2：ADC_CONV

​		ADC转换中

​	状态3：DATA_TRANS

​		采样数据向上位机传输中


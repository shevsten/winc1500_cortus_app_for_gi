/*
 * atmel_drv.h
 *
 *  Created on: Feb 1, 2016
 *      Author: water.zhou
 */

#ifndef ATMEL_DRV_H_
#define ATMEL_DRV_H_

//#define AT_UART_PORT			UART1
//#define AT_UART_PORT_BASE		WIFI_UART1_BASE
//#define AT_UART_TX_PIN		UART1_TX_GPIO7
//#define AT_UART_RX_PIN		UART1_RX_GPIO8
#define AT_UART_PORT			UART2
#define AT_UART_PORT_BASE		WIFI_UART2_BASE
#define AT_UART_TX_PIN			UART2_TX_SPI_RXD
#define AT_UART_RX_PIN			UART2_RX_SPI_TXD

#define SERIAL_BUF_SIZE				256
#define SERIAL_BUFERROR_SIZE		10
#define SERIAL_TIMER_PERIOD			10 //the minimum interval is 10ms
#define SERIAL_TIMEOUT				1 //the maximum timeout is 20ms approximately


void atmel_Serial_Init(void);

#endif /* ATMEL_DRV_H_ */

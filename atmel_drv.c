/*
 * atmel_drv.c
 *
 *  Created on: Feb 1, 2016
 *      Author: water.zhou
 */
#include "bsp/include/nm_bsp.h"
#include "nmi_uart.h"
#include "atmel_drv.h"
#include "crt_iface.h"
#include "spi_flash_map.h"
#include "app_main.h"
#include "string.h"

static tstrOsTimer gstrAtmelTimer;
static int g_iCoreTimerInterruptTime = 0;
static uint8 serial_buf1[SERIAL_BUF_SIZE];
static uint8 serial_buf2[SERIAL_BUF_SIZE];
static uint8 serial_pkt[SERIAL_BUF_SIZE];
static uint16 recv_idx = 0;
static uint8 *serial_recving;
static uint8 *serial_recved;
static uint8 serial_timeout = 0;
uint8 buferror[SERIAL_BUFERROR_SIZE];
static tstrOsTimer gstrTimerSerial;
//Here use magic num, later fix me.
uint8 bufwifiinfo[74];

uint16 piecenum = 1;
uint16 piececount;

extern tstrOsSemaphore gstrAppSem;
extern void create_event(tenuActReq evt);

void parse_serial_packet(uint16 buflen)
{
	uint8 *p = serial_recved;
	M2M_DBG("serial recv:%s!\r\n",p);

	if(strstr(p,"RESET") || strstr(p,"reset")){
		M2M_DBG("Reset configuration!\r\n");
		create_event(ACT_REQ_FACTORY_RESET);
	}

	return;
}


static void serial_timer_callback(void *p)
{
	uint8 *buf = NULL;
	uint16 buflen = 0;
	serial_timeout++;
	//M2M_DBG("serial_timer_callback\r\n");
	if(serial_timeout > SERIAL_TIMEOUT) {
		serial_timeout = 0;
		app_os_timer_stop(&gstrTimerSerial);
		buf = serial_recved;
		serial_recved = serial_recving;
		serial_recving = buf;
		buflen = recv_idx;
		recv_idx = 0;

		parse_serial_packet(buflen);
		//add_event_to_tbl(ACT_REQ_SERIAL_RECV);
		//app_os_sem_up(&gstrAppSem);
	}
}

static void start_serial_timer(void)
{
	m2m_memset((uint8*) &gstrTimerSerial, 0, sizeof(gstrTimerSerial));
	app_os_timer_start(&gstrTimerSerial, "App_Serial", serial_timer_callback,
						SERIAL_TIMER_PERIOD, 1, NULL, 0);
}

void uart_rx_cb(void)
{
	uint32 sts;
	Uart *uart = (Uart *) AT_UART_PORT_BASE;
	sts = uart->rx_status;

	//M2M_DBG("uart_rx_cb\r\n");

	if((sts & NBIT7) == 0) {
		if(recv_idx < SERIAL_BUF_SIZE) {
			serial_recving[recv_idx] = uart->rx_data;
			recv_idx++;
			serial_timeout = 0;
		}
		if(recv_idx == 1) {
			start_serial_timer();
		}
	}

	//nm_uart_recv(AT_UART_PORT, serial_recving, 1);
	//nm_uart_send(AT_UART_PORT, &serial_recving[0], 1);
}

void atmel_Serial_Init(void)
{
	tstrUartConfig uartConfig =
	{
		.u8EnFlowctrl = 0,
		.u8TXGpioPin = AT_UART_TX_PIN,
		.u8RxGpioPin = AT_UART_RX_PIN,
#ifndef WIFI_DEBUG_MODE
		.u32BaudRate = 9600
#else
		.u32BaudRate = 115200
#endif
	};

	serial_recving = &serial_buf1[0];
	serial_recved = &serial_buf2[0];

	nm_uart_init(AT_UART_PORT, &uartConfig);
	nm_uart_register_rx_isr(AT_UART_PORT, uart_rx_cb);
}






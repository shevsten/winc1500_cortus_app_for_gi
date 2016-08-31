/*!
@file		app_main.c

@brief		This module contains user Application related functions

@author		Matt.Qian
@date		14/03 2016
@version	1.0
*/

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*
INCLUDES
*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "driver/source/m2m_hif.h"
#include "driver/include/m2m_wifi.h"
#include "driver/include/m2m_ota.h"
#include "driver/source/nmasic.h"
#include "socket/include/socket.h"
#include "nmi_uart.h"
#include "nmi_gpio.h"
#include "nmi_spi.h"
#include "nmi_btn.h"
#include "spi_flash_map.h"
#include "atmel_drv.h"
#include "app_main.h"

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*
GLOBAL VARIABLES
*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
//main task resources
uint8 	gau8StackApp[1 * 1024];
tstrOsTask gstrTaskApp;
tstrOsSemaphore gstrAppSem;

//heart beat timer
static tstrOsTimer gstrTimerHB;
//heart beat response timer
static tstrOsTimer gstrTimerHBResp;

//flags
static uint8  gbServerConnected = false;
static uint8  commProcess = INPROGRESS_HOLD;
static uint8  gbWifiConnected = 0;
static uint8  gu8App = 0;
static uint32 gu32ServerIP = 0;
static uint8  gbHeartBeatReady = 1;

//sockets
static SOCKET SoftTcpServerListenSocket	= -1;
static SOCKET httpsClientSocket	= -1;
static SOCKET APClientSocket[MAX_TCP_CLIENT_SOCKETS]	= {-1,-1,-1,-1};
static uint8  client_idx = 0;

//Tx & Rx Buffer
static char  TxBuffer[TX_BUFFER_SIZE];
static char  gau8RxBuffer[RX_BUFFER_SIZE];
static char configCallbackBuf[CONFIG_CALLBACK_LEN]={0};
static char cookie[COOKIE_LEN]={0};
//static uint8  selfDHCPIp[4];
static uint8 disconn_cnt = 0;
//event table
static tenuActReq g_event_tbl[EVENT_TABLE_SIZE] = {ACT_REQ_NONE, ACT_REQ_NONE, ACT_REQ_NONE, ACT_REQ_NONE};

/* configuration parameters */
__attribute__((__aligned__(4)))   static wifi_param_t g_wifi_param;

//AP configuration
static tstrM2MAPConfig gstrM2MAPConfig = {
		NMI_M2M_AP, NMI_M2M_AP_CHANNEL, NMI_M2M_AP_WEP_KEY_INDEX, strlen(NMI_M2M_AP_WEP_KEY),
		NMI_M2M_AP_WEP_KEY, (uint8) NMI_M2M_AP_SEC,NMI_M2M_AP_SSID_MODE,HTTP_PROV_SERVER_IP_ADDRESS};

//tcp recv packet info
static volatile data_pkt_t tcp_data_pkt;
//command control info
static cmd_content_t control_cmd[MAX_CONTROL_MSG];
//command count
static uint8 cmd_cnt;

extern char *nm_itoa(int n);
extern int nm_atoi(const char* str);
//store data into flash
//Flash total size is 4K.
void store_parameter()
{
	sint8  ret = 0;
	uint32 offset = M2M_OTA_IMAGE2_OFFSET;//M2M_TLS_FLASH_SESSION_CACHE_SIZE

	g_wifi_param.config = 1;
	g_wifi_param.pwd_type = DEFAULT_AUTH;

	//after reset, login is needed
	g_wifi_param.device_inst.register_status = 0;

	//strcpy((char*)g_wifi_param.ssid,DEFAULT_SSID);
	//strcpy((char*)g_wifi_param.password,DEFAULT_KEY);


	ret = app_spi_flash_erase(offset,FLASH_SECTOR_SZ);
	M2M_DBG("spi_flash_erase %d %d\n",offset,ret);
	ret = app_spi_flash_write(&g_wifi_param,offset,sizeof(g_wifi_param));
	M2M_DBG("spi_flash_write %d\n",ret);

}

//read data from flash
void read_parameter()
{
	sint8  ret = 0;
	uint32 offset = M2M_OTA_IMAGE2_OFFSET;//M2M_TLS_FLASH_SESSION_CACHE_SIZE
	memset(&g_wifi_param, 0, sizeof(g_wifi_param));
	ret = app_spi_flash_read(&g_wifi_param,offset,sizeof(g_wifi_param));
	M2M_DBG("spi_flash_read %d\n ssid = %s\n password = %s\n",ret, g_wifi_param.ssid, g_wifi_param.password);
	M2M_DBG("spi_flash_read %d\n device_id=%s \n",ret,
			g_wifi_param.device_inst.device_id);

}

void clear_parameter()
{
	sint8  ret = 0;
	uint32 offset = M2M_OTA_IMAGE2_OFFSET;//M2M_TLS_FLASH_SESSION_CACHE_SIZE

	ret = app_spi_flash_erase(offset,FLASH_SECTOR_SZ);
	M2M_DBG("spi_flash_erase %d %d\n",offset,ret);
}
//add event to table
void add_event_to_tbl(tenuActReq evt)
{
	static uint8 evt_idx = 0;
	g_event_tbl[evt_idx] = evt;
	evt_idx++;
	evt_idx = evt_idx % EVENT_TABLE_SIZE;
}

//get event from table
tenuActReq get_event_from_tbl(void)
{
	static uint8 head_idx = 0;
	uint8 i;
	tenuActReq event;
	for(i = 0; i < EVENT_TABLE_SIZE; i++) {
		if(g_event_tbl[head_idx] != ACT_REQ_NONE) {
			event = g_event_tbl[head_idx];
			g_event_tbl[head_idx] = ACT_REQ_NONE;
			return event;
		}
		head_idx++;
		head_idx = head_idx % EVENT_TABLE_SIZE;
	}
	return ACT_REQ_NONE;
}

//create event
void create_event(tenuActReq evt)
{
	add_event_to_tbl(evt);
	app_os_sem_up(&gstrAppSem);
}

//start tcp server
static void TCP_StartServer(void)
{

	struct sockaddr_in	addr;
	M2M_DBG("TCP_StartServer\r\n");
	/* TCP Server. */
	if(SoftTcpServerListenSocket == -1)
	{
		SoftTcpServerListenSocket = socket(AF_INET,SOCK_STREAM,0);
		if(SoftTcpServerListenSocket >= 0)
		{
			addr.sin_family			= AF_INET;
			addr.sin_port			= _htons(SOFT_TCP_SERVER_PORT);
			addr.sin_addr.s_addr	= 0;
			M2M_DBG("Begin to bind\n");
			bind(SoftTcpServerListenSocket,(struct sockaddr*)&addr,sizeof(addr));
		}
		else
		{
			M2M_ERR("TCP Server Socket Creation Failed\n");
			return;
		}
	}
	else
	{
		accept(SoftTcpServerListenSocket,NULL,0);
	}
}


void SmartConfig_AppStart()
{
	if(SoftTcpServerListenSocket != -1)
	{
		close(SoftTcpServerListenSocket);
		SoftTcpServerListenSocket = -1;
	}
	TCP_StartServer();

}

//convert mac to string
static void get_dev_mac(uint8 * macstring, uint8 * mac_addr)
{

	macstring[0] = HEX2ASCII((mac_addr[0] >> 4) & 0x0f);
	macstring[1] = HEX2ASCII((mac_addr[0] >> 0) & 0x0f);
	macstring[2] = ':';
	macstring[3] = HEX2ASCII((mac_addr[1] >> 4) & 0x0f);
	macstring[4] = HEX2ASCII((mac_addr[1] >> 0) & 0x0f);
	macstring[5] = ':';
	macstring[6] = HEX2ASCII((mac_addr[2] >> 4) & 0x0f);
	macstring[7] = HEX2ASCII((mac_addr[2] >> 0) & 0x0f);
	macstring[8] = ':';
	macstring[9] = HEX2ASCII((mac_addr[3] >> 4) & 0x0f);
	macstring[10] = HEX2ASCII((mac_addr[3] >> 0) & 0x0f);
	macstring[11] = ':';
	macstring[12] = HEX2ASCII((mac_addr[4] >> 4) & 0x0f);
	macstring[13] = HEX2ASCII((mac_addr[4] >> 0) & 0x0f);
	macstring[14] = ':';
	macstring[15] = HEX2ASCII((mac_addr[5] >> 4) & 0x0f);
	macstring[16] = HEX2ASCII((mac_addr[5] >> 0) & 0x0f);
	macstring[17] = '\0';

}

//wifi state callback
static void wifi_cb(uint8 u8MsgType, void * pvMsg)
{
	if (u8MsgType == M2M_WIFI_RESP_CON_STATE_CHANGED)
	{
		tstrM2mWifiStateChanged *pstrWifiState =(tstrM2mWifiStateChanged*) pvMsg;
		M2M_DBG("Wifi State \"%s\"\n", pstrWifiState->u8CurrState? "CONNECTED":"DISCONNECTED");
		if (pstrWifiState->u8CurrState == M2M_WIFI_CONNECTED)
		{
			if(gu8App == APP_WIFI){
				gbWifiConnected = M2M_WIFI_CONNECTED;
				disconn_cnt = 0;
			}
			else if(gu8App == APP_AP)
			{

				SmartConfig_AppStart();
			}

		}
		else if (pstrWifiState->u8CurrState == M2M_WIFI_DISCONNECTED)
		{
			gbWifiConnected = M2M_WIFI_DISCONNECTED;
			disconn_cnt++;
			if(disconn_cnt == MAX_DISCONNECT_CNT){
				//clear_parameter();
				//reset to reenter AP mode
				//app_os_sch_task_sleep(3);//delay 3 OS_TICKs
				//chip_reset();
			}
			if(gu8App == APP_WIFI)
			{
				M2M_DBG("connecting to ssid:%s, key:%s\r\n",g_wifi_param.ssid,g_wifi_param.password);
				m2m_wifi_connect((char*)g_wifi_param.ssid,
						(uint8)m2m_strlen((uint8*)g_wifi_param.ssid),
						g_wifi_param.pwd_type,
						(void*)g_wifi_param.password,
						M2M_WIFI_CH_ALL);
//				m2m_wifi_connect(DEFAULT_SSID,
//							(uint8)m2m_strlen((uint8*)DEFAULT_SSID),
//							DEFAULT_AUTH,
//							DEFAULT_KEY,
//							M2M_WIFI_CH_ALL);

			} else if (gu8App == APP_AP)
			{
//				M2M_DBG("SOFTAP DISABLE\n");

				if(SoftTcpServerListenSocket != -1)
				{
					close(SoftTcpServerListenSocket);
					SoftTcpServerListenSocket = -1;
				}
			}
		}
	}
	else if (u8MsgType == M2M_WIFI_RESP_GET_SYS_TIME)
	{
		tstrSystemTime *pstrTime = (tstrSystemTime*)pvMsg;
		M2M_DBG("TimeOf Day\n\t%d/%02d/%d %02d:%02d:%02d GMT\n",
		pstrTime->u8Month, pstrTime->u8Day, pstrTime->u16Year,
		pstrTime->u8Hour, pstrTime->u8Minute, pstrTime->u8Second);

	}
	else if (u8MsgType == M2M_WIFI_REQ_DHCP_CONF)
	{

		tstrM2MIPConfig* pstrM2MIpConfig = (tstrM2MIPConfig*) pvMsg;
		uint8 *pu8IPAddress = (uint8*) &pstrM2MIpConfig->u32StaticIP;
		M2M_DBG("DHCP  IP Address\"%u.%u.%u.%u\"\n",pu8IPAddress[0],pu8IPAddress[1],pu8IPAddress[2],pu8IPAddress[3]);
		M2M_DBG("AP  IP Address\"%u.%u.%u.%u\"\n",pu8IPAddress[4],pu8IPAddress[5],pu8IPAddress[6],pu8IPAddress[7]);

		if (gu8App == APP_WIFI)
		{
			gbWifiConnected = M2M_WIFI_CONNECTED;
//			selfDHCPIp[0] = pu8IPAddress[0];
//			selfDHCPIp[1] = pu8IPAddress[1];
//			selfDHCPIp[2] = pu8IPAddress[2];
//			selfDHCPIp[3] = pu8IPAddress[3];

			//store_parameter();
			//read_parameter();
			gethostbyname((uint8*)JY_DOMAIN_NAME);

		}

	}

}


//close http client socket
void close_httpclient_socket()
{
	//app_os_sem_up(&gstrAppSem);
	app_os_timer_stop(&gstrTimerHB);
	//g_wifi_param.device_inst.login_status = 0;
	if(httpsClientSocket>=0){
		close(httpsClientSocket);

	}
	gbServerConnected = 0;
	httpsClientSocket = -1;
	gbHeartBeatReady = 1;

	if(gu8App == APP_WIFI)
		create_event(ACT_REQ_SERVER_CONNECT);

}

//decode http result
uint8 Https_Recv_result_200( uint8 *Https_recevieBuf )
{
	uint8 *p_start = NULL;
    p_start = strstr( Https_recevieBuf,"\"result\":\"200\"");
    if( p_start == NULL ){
    	 return 1;
    }
    return 0;
}

//heart beat response timer callback
void check_heartbeat_packet_resp(void* p)
{
	//if(!isHBRespRecv){
		M2M_DBG("HB resp timer out\r\n");
		close_httpclient_socket();

		//isHBRespRecv = false;
		//app_os_timer_stop(&gstrTimerHB);
	//}
}

//generate http post header
void generate_http_post_header(char* buf1, char* buf2, uint8 cookie_enbale)
{
	strcat(buf1, "POST http://");
	strcat(buf1, JY_DOMAIN_NAME);
	strcat(buf1, "/ HTTP/1.1\r\n");
	strcat(buf1, "User-Agent: atmel/1.0.2\r\n");
	strcat(buf1, "Host: ");
	strcat(buf1, JY_DOMAIN_NAME);
	strcat(buf1, "\r\n");
	strcat(buf1, "Content-type: application/x-www-form-urlencoded\r\n");
	strcat(buf1, "Connection: Keep-Alive\r\n");
	if(cookie_enbale){
		strcat(buf1, "Cookie: PHPSESSID=");
		strcat(buf1, cookie);
		strcat(buf1, "; theme=default; lang=en\r\n");
	}
	strcat(buf1, "Content-length: ");
	strcat(buf1, nm_itoa(strlen(buf2)-2));
	strcat(buf1, "\r\n\r\n");
	strcat(buf1, buf2);
}

//send register packet
void send_register_packet()
{
	char tempbuf[100];
	char mac[18];

	memset(tempbuf, 0, sizeof(tempbuf));
	memset(mac, 0, sizeof(mac));
	memset(TxBuffer, 0, sizeof(TxBuffer));

	strcat(tempbuf, "func=device.register&id=");
	strcat(tempbuf, g_wifi_param.device_inst.device_id);
	//strcat(tempbuf, nm_itoa(g_wifi_param.device_inst.user_id));
	//strcat(tempbuf, g_wifi_param.device_inst.user_id);
	//strcat(tempbuf, "&target=");
	//strcat(tempbuf, nm_itoa(g_wifi_param.device_inst.room_id));
	//strcat(tempbuf, g_wifi_param.device_inst.room_id);
	//strcat(tempbuf, "&model=");
	//strcat(tempbuf, nm_itoa(g_wifi_param.device_inst.model_id));
	//strcat(tempbuf, g_wifi_param.device_inst.model_id);
	strcat(tempbuf, "&description=");

	get_dev_mac(mac, g_wifi_param.device_inst.mac);
	//M2M_DBG("mac:%s\r\n", mac);
	strcat(tempbuf, mac);
	strcat(tempbuf, "\r\n");
	generate_http_post_header(TxBuffer,tempbuf, false);

	if(httpsClientSocket>=0){
		M2M_DBG("send register:\r\n%s\r\n", TxBuffer);
		send(httpsClientSocket, (uint8*)TxBuffer, strlen(TxBuffer), 0);
		//while(1);
	}

}

void send_ap_config_status(uint8 status)
{
	char tempbuf[50];
	sint16 ret;
	memset(TxBuffer, 0, sizeof(TxBuffer));
	memset(tempbuf, 0, sizeof(tempbuf));

	strcat(TxBuffer, configCallbackBuf);
	strcat(TxBuffer, "(");
	strcat(TxBuffer, "{status:");
	strcat(TxBuffer, status == true?"true":"false");
	strcat(TxBuffer, ",data:'',error:''})");
	strcat(TxBuffer, "\r\n");

	if(APClientSocket[0]>=0){
		M2M_DBG("send ap config status: %d bytes,sock=%d\r\n%s\n", strlen(TxBuffer), APClientSocket[0], TxBuffer);
		ret = send(APClientSocket[0], (uint8*)TxBuffer, strlen(TxBuffer), 0);
		//M2M_DBG("ret=%d\r\n",ret);
	}
}

//send login packet
void send_login_packet(char* device_id)
{
	char tempbuf[50];
	memset(TxBuffer, 0, sizeof(TxBuffer));
	memset(tempbuf, 0, sizeof(tempbuf));

	strcat(tempbuf, "func=device.login&id=");
	strcat(tempbuf, device_id);
	strcat(tempbuf, "\r\n");
	generate_http_post_header(TxBuffer,tempbuf, false);

	if(httpsClientSocket>=0){
		M2M_DBG("send login\r\n%s\n", TxBuffer);
		send(httpsClientSocket, (uint8*)TxBuffer, strlen(TxBuffer), 0);
	}

}

//send heartbeat packet
void send_heartbeat_packet(void* p)
{
	char tempbuf[30];
	if(commProcess != INPROGRESS_HB)
		return;

	if(!gbHeartBeatReady)
		return;

	memset(TxBuffer, 0, sizeof(TxBuffer));
	memset(tempbuf, 0, sizeof(tempbuf));

	strcat(tempbuf, "func=device.beat\r\n");
	generate_http_post_header(TxBuffer,tempbuf, true);

	if(httpsClientSocket>=0){
		M2M_DBG("send heartbeat\r\n%s\n", TxBuffer);
		send(httpsClientSocket, (uint8*)TxBuffer, strlen(TxBuffer), 0);

//		if (app_os_timer_start(&gstrTimerHBResp, "heart beat check timer",
//				check_heartbeat_packet_resp,HB_RESP_INTERVAL, 0,NULL, 1)) {
//			M2M_ERR("Can't start timer\n");
//		}
	}

}

//send control status
void send_control_status(cmd_content_t* cmd,uint8 status)
{
	if(commProcess != INPROGRESS_HB)
		return;

	memset(TxBuffer, 0, sizeof(TxBuffer));

	strcat(TxBuffer, "GET http://");
	strcat(TxBuffer, JY_DOMAIN_NAME);
	strcat(TxBuffer, "/");
	strcat(TxBuffer, "?func=task.done&id=");
	strcat(TxBuffer, (char*)cmd->id);
	strcat(TxBuffer, "&status=");
	strcat(TxBuffer, nm_itoa((int)status));
	strcat(TxBuffer, " HTTP/1.1\r\n");
	strcat(TxBuffer, "Accept:text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n");
	strcat(TxBuffer, "Host: ");
	strcat(TxBuffer, JY_DOMAIN_NAME);
	strcat(TxBuffer, "\r\n");
	strcat(TxBuffer, "Connection: Keep-Alive\r\n");
	strcat(TxBuffer, "Cookie: PHPSESSID=");
	strcat(TxBuffer, cookie);
	strcat(TxBuffer, "; theme=default; lang=en\r\n");
	//strcat(TxBuffer, "Cookie: PHPSESSID=irtfkkj4qlda40hgtov9jablg6; theme=default; lang=en\r\n");
	strcat(TxBuffer, "User-Agent: atmel/1.0.2\r\n");
	strcat(TxBuffer, "\r\n");

	if(httpsClientSocket>=0){
		M2M_DBG("send control status\r\n%s\n", TxBuffer);
		send(httpsClientSocket, (uint8*)TxBuffer, strlen(TxBuffer), 0);
	}
}


//check register response
uint8 http_recv_check_register(uint8 *Https_recevieBuf)
{
	uint8 *p_start = NULL;

	p_start = strstr( Https_recevieBuf,"\"func\":\"device.register\"");
	if(p_start != NULL){
		p_start = strstr( Https_recevieBuf,"\"status\":true");
		if(p_start != NULL){

			M2M_DBG("register success\r\n");
			return 1;

		}else{
			M2M_DBG("register failed\r\n");
		}
	}else{
		//M2M_DBG("not register func\r\n");
	}

	return 0;

}

//check login response
uint8 http_recv_check_login(uint8 *Https_recevieBuf )
{
	uint8 *p_start = NULL;
	uint8 *p_cookie = NULL;

	p_start = strstr( Https_recevieBuf,"\"func\":\"device.login\"");
	if(p_start != NULL){
		p_start = strstr( Https_recevieBuf,"\"status\":true");
		if(p_start != NULL){
			if (app_os_timer_start(&gstrTimerHB, "heart beat 1s timer ",
					send_heartbeat_packet,(HB_INTERVAL), 1,NULL, 1)) {
				M2M_ERR("Can't start timer ret_timeout \r\n");
			}

			M2M_DBG("Login success\r\n");
			//get cookie
			p_start = strstr( Https_recevieBuf,"PHPSESSID=");
			p_cookie = (uint8 *) strtok((char *) p_start, "=");
			p_cookie = (uint8 *) strtok(NULL, ";");
			if(p_cookie){
				strcpy(cookie, p_cookie);
				//M2M_DBG("Cookie:%s\r\n",cookie);
			}

			return 1;
		}else{
			M2M_DBG("Login failed\r\n");
		}
	}else{
		//M2M_DBG("not Login func\r\n");
	}


	return 0;
}

//check taskdone response
uint8 http_recv_check_taskdone(uint8 *Https_recevieBuf )
{
	uint8 *p_start = NULL;
	uint8 ret = 0;

	p_start = strstr( Https_recevieBuf,"\"func\":\"task.done\"");
	if(p_start != NULL){
		p_start = strstr( Https_recevieBuf,"\"status\":true");
		if(p_start != NULL){

			M2M_DBG("Task.done success\r\n");

			ret = 1;
		}else{
			M2M_DBG("Task.done failed\r\n");
		}
		gbHeartBeatReady = 1;
	}else{
		//M2M_DBG("not Task.done func\r\n");
	}


	return ret;
}

//parse heart beat response to get commands
uint8 parse_hb_commands(uint8 *cmd_buf)
{
	uint8 *p_start = NULL;

	uint8 i=0;
	static uint8 cmd_index = 0;

	M2M_DBG("parse_hb_commands\r\n");

	//if(cmd_index >= MAX_CONTROL_MSG){
	//	M2M_DBG("parse done, max msg(5) received\r\n");
		//cmd_cnt = MAX_CONTROL_MSG;
		//cmd_index = 0;
		//create_event(ACT_REQ_FORWARD_CONTROL);
	//	return 1;
	//}

	p_start = strstr( cmd_buf,"\"id\":\"");
	gbHeartBeatReady = 0;
	if(p_start && cmd_index < MAX_CONTROL_MSG){
		do{
			control_cmd[cmd_index].id[i] = *(p_start+i+6);
		}while(*(p_start+(++i)+6) != '\"');
		control_cmd[cmd_index].id[i] = '\0';
		M2M_DBG("control_cmd.id=%s\r\n",control_cmd[cmd_index].id);

		p_start = strstr( cmd_buf,"\"name\":\"");
		i = 0;
		if(p_start){
			do{
				control_cmd[cmd_index].name[i] = *(p_start+i+8);
			}while(*(p_start+(++i)+8) != '\"');
			control_cmd[cmd_index].name[i] = '\0';
			M2M_DBG("control_cmd.name=%s\r\n",control_cmd[cmd_index].name);
			M2M_DBG("control_cmd index=%d\r\n",cmd_index);

			p_start = strstr( p_start,"\"description\":\"");
			i=0;
			if(p_start){
				while(*(p_start+i+15) != '\"'){
					control_cmd[cmd_index].description[i] = *(p_start+i+15);
					i++;
				}
				control_cmd[cmd_index].description[i] = '\0';
				M2M_DBG("control_cmd.description=%s\r\n",control_cmd[cmd_index].description);
				cmd_index++;
				//p_start = strstr( p_start,"\"type\":\"");
				parse_hb_commands(p_start);
			}
		}
		return 1;

	}else{
		if(cmd_index >= MAX_CONTROL_MSG){
				cmd_index = MAX_CONTROL_MSG - 1;
				M2M_DBG("max msg(5) received\r\n");
			}
		M2M_DBG("parse done\r\n");
		cmd_cnt = cmd_index;
		cmd_index = 0;
		create_event(ACT_REQ_FORWARD_CONTROL);
	}


	return 0;

}

//check heart beat response
uint8 http_recv_check_hb(uint8 *Https_recevieBuf )
{
	uint8 *p_start = NULL;

	p_start = strstr( Https_recevieBuf,"\"func\":\"device.beat\"");
	if(p_start != NULL){
		p_start = strstr( Https_recevieBuf,"\"status\":true");
		if(p_start != NULL){
			p_start = strstr( Https_recevieBuf,"\"content\":");
			//decode and perform actions
			if(*(p_start+10) == '\"' && *(p_start+11) == '\"'){
				M2M_DBG("no new commands\r\n");
			}else{
				//parse command arrays
				parse_hb_commands(p_start+11);
			}
			return 1;
		}else{
			M2M_DBG("HB failed\r\n");
		}
	}else{
		//M2M_DBG("not HB func\r\n");
	}

	return 0;
}

//check ap config packet
uint8 parse_config_packet(uint8* pBuf)
{
	uint8 *p_start = NULL;
	uint8* pData;
	//M2M_DBG("recv config data:%s\r\n", pBuf);
	memset(g_wifi_param.ssid, 0, sizeof(g_wifi_param.ssid));
	memset(g_wifi_param.password, 0, sizeof(g_wifi_param.password));
	memset(configCallbackBuf, 0, sizeof(configCallbackBuf));

	//copy buffer?
	p_start = (uint8 *)strstr((char*)pBuf,"callback");
		if(p_start){
			pData = (uint8 *) strtok((char *) p_start, "=");
			pData = (uint8 *) strtok(NULL, "&");
			if(pData){
				if(strlen((char*)pData) > 50){
					M2M_ERR("incorrect device_id\r\n");
					return 0;
				}else{
					strcpy(configCallbackBuf, (char*)pData);
					M2M_DBG("configCallbackBuf:%s\r\n", configCallbackBuf);
				}
			}
		}else{
			M2M_ERR("no device_id\r\n");
			return 0;
	}

//	p_start = (uint8 *)strstr((char*)pBuf,"id");
//	if(p_start){
		pData = (uint8 *) strtok(NULL, "=");
		pData = (uint8 *) strtok(NULL, "&");
		if(pData){
			if(strlen((char*)pData) > 8){
				M2M_ERR("incorrect device_id\r\n");
				return 0;
			}else{
				strcpy((char*)g_wifi_param.device_inst.device_id, (char*)pData);
				M2M_DBG("device_id:%s\r\n", g_wifi_param.device_inst.device_id);
			}
		}else{
				M2M_ERR("no device_id\r\n");
				return 0;
		}
//	}else{
//		M2M_ERR("no device_id\r\n");
//		return 0;
//	}


	pData = (uint8 *) strtok(NULL, "=");
	pData = (uint8 *) strtok(NULL, "&");
	if(pData){
		if(strlen((char*)pData) > 32){
			M2M_ERR("incorrect ssid\r\n");
			return 0;
		}else{
			strcpy((char*)g_wifi_param.ssid, (char*)pData);
			//strcpy((char*)g_wifi_param.ssid, (char*)DEFAULT_SSID);
			M2M_DBG("ssid:%s,len:%d\r\n", g_wifi_param.ssid, m2m_strlen(g_wifi_param.ssid));
		}

	}else{
		M2M_ERR("no ssid\r\n");
		return 0;
	}

	pData = (uint8 *) strtok(NULL, "=");
	pData = (uint8 *) strtok(NULL, "&");
	if(pData){
		if(strlen((char*)pData) > 64){
			M2M_ERR("incorrect key\r\n");
			return 0;
		}else{
			strcpy((char*)g_wifi_param.password, (char*)pData);
			M2M_DBG("password:%s,len:%d\r\n", g_wifi_param.password,  m2m_strlen(g_wifi_param.password));
		}

	}else{
		M2M_ERR("no password\r\n");
		return 0;
	}

	store_parameter();

	return 1;
}

//tcp socket callback
static void TCP_SocketEventHandler(SOCKET sock, uint8 u8Msg, void * pvMsg)
{
	//struct sockaddr_in uaddr;
	switch(u8Msg)
	{
	case SOCKET_MSG_BIND:
		{
			tstrSocketBindMsg	*pstrBind = (tstrSocketBindMsg*)pvMsg;
			if(pstrBind != NULL)
			{
				if(pstrBind->status == 0)
				{
					M2M_DBG("bind success,listen...\r\n");
					listen(sock, 2);
				}
				else
				{
					M2M_ERR("bind\n");
				}
			}
		}
		break;

	case SOCKET_MSG_LISTEN:
		{

			tstrSocketListenMsg	*pstrListen = (tstrSocketListenMsg*)pvMsg;
			if(pstrListen != NULL)
			{
				if(pstrListen->status == 0)
				{
					M2M_DBG("listen success,accept...\r\n");
					accept(sock,NULL,0);
				}
				else
				{
					M2M_ERR("listen\n");
				}
			}
		}
		break;

	case SOCKET_MSG_ACCEPT:
		{
			tstrSocketAcceptMsg	*pstrAccept = (tstrSocketAcceptMsg*)pvMsg;
			if(SoftTcpServerListenSocket!=-1)
			{
				accept(SoftTcpServerListenSocket,NULL,0);
			}
			if(pstrAccept->sock >= 0)
			{
				M2M_DBG("accept success, recv...,client_idx=%d,sock=%d\r\n",client_idx,pstrAccept->sock);
				//TcpNotificationSocket = pstrAccept->sock;
				//if(APClientSocket < 0){
				APClientSocket[client_idx] = pstrAccept->sock;
				recv(APClientSocket[client_idx],gau8RxBuffer,sizeof(gau8RxBuffer), 0);
				client_idx++;//}

			}
			else
			{
				M2M_ERR("accept\n");
			}
		}
		break;

	case SOCKET_MSG_CONNECT:
		{

			tstrSocketConnectMsg *pstrConnect = (tstrSocketConnectMsg *)pvMsg;

			if(pstrConnect && pstrConnect->s8Error == SOCK_ERR_NO_ERROR){
				// Here maybe triggered by http server also, need to double check.
				M2M_DBG("TCP Socket Connected...\r\n");
				gbServerConnected = true;
				recv(httpsClientSocket,gau8RxBuffer,sizeof(gau8RxBuffer), 0);
				//memset(TxBuffer, 0, sizeof(TxBuffer));
				if(!g_wifi_param.device_inst.register_status){
					commProcess = INPROGRESS_REGISTER;
				}else if(g_wifi_param.device_inst.login_status == 1){
					if (app_os_timer_start(&gstrTimerHB, "heart beat 1s timer ", send_heartbeat_packet,HB_INTERVAL, 1,NULL, 1)) {
						M2M_ERR("Can't start timer ret_timeout\n");
					}
					commProcess = INPROGRESS_HB;
				}else{
					commProcess = INPROGRESS_LOGIN;
				}


			}
			else {
				M2M_ERR("SSL Connect error:%d\r\n",pstrConnect->s8Error);
				close(httpsClientSocket);
				httpsClientSocket = -1;
				create_event(ACT_REQ_SERVER_CONNECT);
			}
		}
		break;

	case SOCKET_MSG_SEND:
		{

			M2M_DBG("send ===== ok\n");

			if(gu8App == APP_AP)
			{
			//	m2m_wifi_disable_ap();

			} else if (gu8App == APP_WIFI)
			{
//				if (httpsClientSocket >= 0)
//				{
//					recv(httpsClientSocket,gau8RxBuffer,sizeof(gau8RxBuffer), 0);
//				}

			}

		}
		break;

	case SOCKET_MSG_RECV:
		{
			tstrSocketRecvMsg	*pstrRx = (tstrSocketRecvMsg*)pvMsg;

			if(pstrRx->pu8Buffer != NULL)
			{
				pstrRx->pu8Buffer[pstrRx->s16BufferSize] = '\0';

				tcp_data_pkt.buf = pstrRx->pu8Buffer;
				tcp_data_pkt.len = pstrRx->s16BufferSize;
				tcp_data_pkt.type = ACT_REQ_TCP_RECV;
				create_event(ACT_REQ_TCP_RECV);

			}
			else
			{
				if(pstrRx->s16BufferSize == SOCK_ERR_TIMEOUT)
				{
					recv(sock,gau8RxBuffer,sizeof(gau8RxBuffer), 0);
				}
				else
				{
					close_httpclient_socket();
				}
			}
		}
		break;

	default:
		break;
	}
}

void AppServerCb(uint8* pu8HostName, uint32 u32ServerIP)
{
	if(u32ServerIP != 0)
	{
		gu32ServerIP = u32ServerIP;
		create_event(ACT_REQ_SERVER_CONNECT);
		M2M_DBG("Host name:%s\r\n",pu8HostName);
		M2M_DBG("Host IP is %d.%d.%d.%d\r\n",
					IPV4_BYTE(u32ServerIP, 0), IPV4_BYTE(u32ServerIP, 1),
					IPV4_BYTE(u32ServerIP, 2), IPV4_BYTE(u32ServerIP, 3));
	}
	else
	{
		static uint8	u8Retry = 2;
		if(u8Retry--)
		{
			M2M_DBG("Retry Resolving DNS\n");
			gethostbyname((uint8*)JY_DOMAIN_NAME);
		}
		else
		{
			M2M_DBG("Failed to Resolve DNS\n");
		}
	}
}

//connect server
static int ServerConnect(void)
{
	struct sockaddr_in addr_in;

	addr_in.sin_family = AF_INET;
	addr_in.sin_port = _htons(JY_DOMAIN_PORT);
	addr_in.sin_addr.s_addr = gu32ServerIP;

	M2M_DBG("Connectting to Server...\n");
	if (httpsClientSocket < 0)
	{
		if ((httpsClientSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			M2M_DBG("main:failed to create https client socket error!\n");
			return -1;
		}

	}

	/* If success, connect to socket */
	if (connect(httpsClientSocket, (struct sockaddr *)&addr_in, sizeof(struct sockaddr_in)) != SOCK_ERR_NO_ERROR) {
		M2M_DBG("ServerConnect: Connect Error!\n");
		return SOCK_ERR_INVALID;
	}

	/* Success */
	return SOCK_ERR_NO_ERROR;
}

//general event handler
void App_ProcessActRequest(tenuActReq enuActReq)
{
	M2M_DBG("Handle events (%d)\n", enuActReq);
	if(enuActReq == ACT_REQ_TCP_RECV)
	{
		if(tcp_data_pkt.type == ACT_REQ_TCP_RECV) {
			if (gu8App == APP_WIFI){
				if (httpsClientSocket >= 0){
					M2M_DBG("recv %d bytes from server:\r\n%s\n", tcp_data_pkt.len, tcp_data_pkt.buf);
					if(http_recv_check_register(tcp_data_pkt.buf)){
						M2M_DBG("register done\r\n");
						commProcess = INPROGRESS_LOGIN;
						g_wifi_param.device_inst.register_status = 1;
					}else if(http_recv_check_hb(tcp_data_pkt.buf)){
						M2M_DBG("HB response\r\n");
						//isHBRespRecv = true;
						app_os_timer_stop(&gstrTimerHBResp);
					}else if(http_recv_check_login(tcp_data_pkt.buf)){
						commProcess = INPROGRESS_HB;
						M2M_DBG("Login response\r\n");
					}else if(http_recv_check_taskdone(tcp_data_pkt.buf)){
							M2M_DBG("Taskdone response\r\n");
					}
				}
				recv(httpsClientSocket,gau8RxBuffer,sizeof(gau8RxBuffer), 0);

			} else if (gu8App == APP_AP)
			{
				uint8 status;
				uint8 i;
				M2M_DBG("[Configure][AP]:\r\n%s\n", tcp_data_pkt.buf);
				status = parse_config_packet(tcp_data_pkt.buf);

				send_ap_config_status(status);
				close(SoftTcpServerListenSocket);
				SoftTcpServerListenSocket = -1;
				for(i=0;i<MAX_TCP_CLIENT_SOCKETS;i++){
					close(APClientSocket[i]);
					APClientSocket[i] = -1;
				}
				client_idx = 0;
				app_os_sch_task_sleep(100);
				m2m_wifi_disable_ap();
				if(status){

					if(g_wifi_param.config){
						gu8App = APP_WIFI;
						app_os_sch_task_sleep(100);
						socketDeinit();
						m2m_wifi_deinit(NULL);

						config_wifi();

						m2m_wifi_connect(g_wifi_param.ssid,
							(uint8)m2m_strlen((uint8*)g_wifi_param.ssid),
							DEFAULT_AUTH,
							g_wifi_param.password,
							M2M_WIFI_CH_ALL);
					}
				}else{
					//config fail,reset to AP mode
					//app_os_sch_task_sleep(100);//delay 5 OS_TICKs
					chip_reset();
				}
			}


		}

	}else if(enuActReq == ACT_REQ_FORWARD_CONTROL){
		uint8 i;
		M2M_DBG("%d commands recved\r\n",cmd_cnt);
		for(i=cmd_cnt;i>0;i--){
			//forward cmd to host controller
			//M2M_DBG("cmd %d:id:%s,name:%s\r\n",i,control_cmd[i].id,control_cmd[i].name);
			//if(!strcmp(g_wifi_param.device_inst.device_id, control_cmd[i-1])){
				M2M_PRINT("%s_____%s",control_cmd[i-1].name,control_cmd[i-1].description);
				//send control status
				send_control_status(&control_cmd[i-1], TASK_DONE_SUCCESS);
				//app_os_sch_task_sleep(1);
			//}


		}
	}else if(enuActReq == ACT_REQ_SERVER_CONNECT){
		if(gbWifiConnected){
			if (httpsClientSocket < 0 && gu8App == APP_WIFI){
				if (ServerConnect() != SOCK_ERR_NO_ERROR) {
					M2M_DBG("TCP IP SSL Connect Error!\n");
					if(httpsClientSocket >= 0) {
						close(httpsClientSocket);
					}
					httpsClientSocket = -1;
				}
//				else{
//					M2M_DBG("TCP IP Connect OK!\n");
//				}
			}
		}

	}else if(enuActReq == ACT_REQ_FACTORY_RESET) {

		uint8 ret;
		ret = app_spi_flash_erase(M2M_OTA_IMAGE2_OFFSET,FLASH_SECTOR_SZ);
		M2M_DBG("spi_flash_erase %d %d\n",M2M_OTA_IMAGE2_OFFSET,ret);
		app_os_sch_task_sleep(5);//delay 5 OS_TICKs
		chip_reset();
	}
}

void device_config()
{
	//read config from flash
	//...

	//skip ap config
	g_wifi_param.config = 1;
	//skip register
	//g_wifi_param.device_inst.device_id = 33;//
	g_wifi_param.device_inst.register_status = 1;
	//register info
	//g_wifi_param.device_inst.user_id = USER_ID;
	//g_wifi_param.device_inst.room_id = ROOM_ID;
	//g_wifi_param.device_inst.model_id = MODEL_ID;

	strcpy(g_wifi_param.ssid, DEFAULT_SSID);
	strcpy(g_wifi_param.password, DEFAULT_KEY);
	g_wifi_param.pwd_type = DEFAULT_AUTH;
}

void config_wifi()
{
	tstrWifiInitParam param;
	sint8 ret = NM_SUCCESS;

	param.pfAppWifiCb = wifi_cb;

	ret = m2m_wifi_init(&param);
	if(ret != M2M_SUCCESS)
	{
		M2M_ERR("Driver init  fail\n");

	}

	socketInit();
	registerSocketCallback(TCP_SocketEventHandler, AppServerCb);

}
//main task
void app_main_task(void* pv)
{
	tenuActReq genuActReq = ACT_REQ_NONE;
	M2M_DBG("app_main_task\r\n");

	//g_wifi_param.config = 0;
	//clear_parameter();
	//device_config();

	socketInit();
	registerSocketCallback(TCP_SocketEventHandler, AppServerCb);


	if(g_wifi_param.config == 1){
		M2M_DBG("Start STA mode\r\n");
		gu8App = APP_WIFI;

//		m2m_wifi_connect(DEFAULT_SSID,
//					(uint8)m2m_strlen((uint8*)DEFAULT_SSID),
//					DEFAULT_AUTH,
//					DEFAULT_KEY,
//					M2M_WIFI_CH_ALL);
		m2m_wifi_connect(g_wifi_param.ssid,
				(uint8)m2m_strlen((uint8*)g_wifi_param.ssid),
				DEFAULT_AUTH,
				g_wifi_param.password,
				M2M_WIFI_CH_ALL);
	}else{
		M2M_DBG("Start AP mode\r\n");
		m2m_wifi_enable_ap(&gstrM2MAPConfig);
		gu8App = APP_AP;
	}


	for (;;)
	{
		app_os_sem_down(&gstrAppSem);
		genuActReq = get_event_from_tbl();
		if (genuActReq != ACT_REQ_NONE){
			M2M_DBG("genuActReq %d\n",genuActReq);
			App_ProcessActRequest(genuActReq);
		}else{
				m2m_wifi_handle_events(NULL);
		}

		if (gu32ServerIP == 0)
			continue;

//		if(gbWifiConnected){
//			if (httpsClientSocket < 0 && gu8App == APP_WIFI){
//				if (ServerConnect() != SOCK_ERR_NO_ERROR) {
//					M2M_DBG("TCP IP SSL Connect Error!\n");
//					if(httpsClientSocket >= 0) {
//						close(httpsClientSocket);
//					}
//					httpsClientSocket = -1;
//				}
//				else{
//					M2M_DBG("TCP IP Connect OK!\n");
//				}
//			}
//
//			if (SoftTcpServerListenSocket < 0 && gu8App == APP_AP) {
//				M2M_DBG("Create a tcp server\n");
//				TCP_StartServer();
//
//			} //httpServer
//		}


		if(gbWifiConnected && gbServerConnected){
			if(commProcess == INPROGRESS_REGISTER){
				send_register_packet();
				commProcess = INPROGRESS_HOLD;
			}else if(commProcess == INPROGRESS_LOGIN){
				send_login_packet(g_wifi_param.device_inst.device_id);
				commProcess = INPROGRESS_HOLD;

			}

		}
		//app_os_sch_task_sleep(1);
	}
}

//app entry
sint8 app_start(void)
{
	atmel_Serial_Init();
	M2M_DBG("APP Start\n");

	tstrWifiInitParam param;
	sint8 ret = NM_SUCCESS;
	m2m_memset((uint8*) &gstrAppSem, 0, sizeof(gstrAppSem));
	m2m_memset((uint8*) &gstrTimerHB, 0, sizeof(gstrTimerHB));
	m2m_memset((uint8*) &gstrTimerHBResp, 0, sizeof(gstrTimerHBResp));
	m2m_memset((uint8*)&param, 0, sizeof(param));
	memset((void*)&g_wifi_param.device_inst,0,sizeof(g_wifi_param.device_inst));
	param.pfAppWifiCb = wifi_cb;

	ret = m2m_wifi_init(&param);
	if(ret != M2M_SUCCESS)
	{
		M2M_ERR("Driver init  fail\n");
		goto ERR;
	}

	read_parameter();


	//Get device mac addr
	m2m_wifi_get_mac_address(g_wifi_param.device_inst.mac);
	M2M_DBG("MAC Address:%02X:%02X:%02X:%02X:%02X:%02X\r\n",
			g_wifi_param.device_inst.mac[0], g_wifi_param.device_inst.mac[1], g_wifi_param.device_inst.mac[2],
			g_wifi_param.device_inst.mac[3], g_wifi_param.device_inst.mac[4], g_wifi_param.device_inst.mac[5]);

	app_os_sem_init(&gstrAppSem, "APP", 0);
	app_os_sch_task_create(&gstrTaskApp, app_main_task, "APP", gau8StackApp,sizeof(gau8StackApp), 100);
	ERR:
		return ret;

}



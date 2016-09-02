/*!
@file		app_main.h

@brief		header file

@author		Matt Qian
@date		29/07 2016
@version	1.0
*/

#ifndef APP_MAIN_H_
#define APP_MAIN_H_

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*
MACROS
*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/

#define DEFAULT_SSID				"Z9mini"//"DATANG MIFI958-Jack"//"waterwifi"
#define DEFAULT_AUTH				M2M_WIFI_SEC_WPA_PSK
#define	DEFAULT_KEY					"atmel123"//"88888888"//"qwerty123"

#define true  						1
#define false 						0

#define APP_P2P		1		/*p2p app*/
#define APP_AP      2		/*AP app*/
#define APP_WIFI    3		/*WI-FI app*/
#define APP_INIT	4		/*allow the user to choose from three apps using btn1,btn2 and timeout*/

//AP configuration
//#define NMI_M2M_AP					"AtmelAP_"
#define NMI_M2M_AP					"gudidea"
#define NMI_M2M_AP_SEC				M2M_WIFI_SEC_WEP
#define NMI_M2M_AP_WEP_KEY_INDEX	M2M_WIFI_WEP_KEY_INDEX_1
#define NMI_M2M_AP_WEP_KEY			"1234567890"
#define NMI_M2M_AP_SSID_MODE		SSID_MODE_VISIBLE
#define NMI_M2M_AP_CHANNEL			M2M_WIFI_CH_11
#define HTTP_PROV_SERVER_IP_ADDRESS	{192, 168, 18, 1}
#define SOFT_TCP_SERVER_PORT		80

#define HB_INTERVAL 				1000
#define HB_RESP_INTERVAL			1000

#define JY_DOMAIN_NAME				"control.iot.gudidea.com"
#define JY_DOMAIN_PORT          	80

#define RX_BUFFER_SIZE			1400
#define TX_BUFFER_SIZE			1400

#define EVENT_TABLE_SIZE  		4
#define MAX_CONTROL_MSG			5
#define MAX_TCP_CLIENT_SOCKETS  4
#define USER_ID					3
#define ROOM_ID					26
#define MODEL_ID				18

#define MAX_DISCONNECT_CNT		5

#define CONFIG_CALLBACK_LEN		50
#define COOKIE_LEN				50

#define IPV4_BYTE(val, index)            (int)((val >> (index * 8)) & 0xFF)
#define HEX2ASCII(x) (((x)>=10)? (((x)-10)+'A') : ((x)+'0'))

/*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*
PRIVATE DATA TYPES
*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*=*/
enum{
	TASK_DONE_SUCCESS = 3,
	TASK_DONE_FAILURE = -3
};

enum{
	INPROGRESS_HOLD,
	INPROGRESS_REGISTER,
	INPROGRESS_LOGIN,
	INPROGRESS_HB,
	INPROGRESS_DTATA
};

typedef enum
{
	ACT_REQ_CONNECT					= 1,
	ACT_REQ_DISCONNECT				= 3,
	ACT_REQ_V2_INIT					= 29,
	ACT_REQ_TCP_RECV				= 30,
	ACT_REQ_TCP_SENT				= 31,
	ACT_REQ_FORWARD_CONTROL			= 32,
	ACT_REQ_SERVER_CONNECT			= 33,
	ACT_REQ_REGISTER				= 34,
	ACT_REQ_LOGIN					= 35,
	ACT_REQ_HEART_BEAT				= 36,
	ACT_REQ_TASK_DONE				= 37,
	ACT_REQ_START_HB_TIMER			= 38,
	ACT_REQ_SERIAL_RECV				= 39,
	ACT_REQ_SNIFFER_HANDLER			= 40,
	ACT_REQ_FACTORY_RESET			= 41,
	ACT_REQ_CHIP_RESET				= 42,
	ACT_REQ_APP_OTAU				= 43,
	ACT_REQ_WIFI_FW_OTAU			= 44,
	ACT_REQ_AP_SCAN_TEST			= 45,
	ACT_REQ_WIFI_RESET              = 46,
	ACT_REQ_EXIT					= 0,
	ACT_REQ_NONE					= 127
} tenuActReq;


/* Read and write size should be times of 4 byte*/
typedef struct _device_param_t{
	uint8 device_id[8];
	uint8 user_id[8];//creator id
	uint8 room_id[8];//room id
	//uint16 brand_id;//brand id
	uint8 model_id[8];
	uint8 mac[6];
	uint8 register_status;//if registerd
	uint8 login_status; //if logged in
	//uint8 reserved[2];
}device_param_t;

typedef struct _wifi_param_t {
	uint8 config;
	uint8 ssid[32];
	uint8 password[64];
	uint8 pwd_type;
	uint8 url[50];
	device_param_t device_inst;
}wifi_param_t;

typedef struct _cmd_content_t {
	uint8 id[4];
	uint8 name[50];
	uint8 description[40];
}cmd_content_t;

typedef struct data_pkt {
	uint8 *buf;
	uint16 len;
	uint8 type;
}data_pkt_t;



#endif

// Include the most common headers from the C standard library
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/time.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
// Sockets
# include <sys/socket.h>
# include <netinet/in.h>
# include <netdb.h>
# include <arpa/inet.h>
#include <sys/errno.h>
#include "ini.h"

// Include the main libnx system header, for Switch development
#include <switch.h>

#define GAMEPAD_PORT 5000
#define SCREEN_WIDTH 1920
#define SCREEN_HEIGHT 1088

extern "C"
{
    extern u32 __start__;
// Adjust size as needed.
u32 __nx_applet_type = AppletType_None;

// Adjust size as needed.
#define INNER_HEAP_SIZE 0x40'000
size_t nx_inner_heap_size = INNER_HEAP_SIZE;
char   nx_inner_heap[INNER_HEAP_SIZE];

void  __libnx_initheap(void);
void  appInit(void);
void  appExit(void);
}

void __libnx_initheap(void)
{
	void*  addr = nx_inner_heap;
	size_t size = nx_inner_heap_size;

	// Newlib
	extern char* fake_heap_start;
	extern char* fake_heap_end;

	fake_heap_start = (char*)addr;
	fake_heap_end   = (char*)addr + size;
}

static const SocketInitConfig sockInitConf = {
    .bsdsockets_version = 1,

    .tcp_tx_buf_size        = 0x200,
    .tcp_rx_buf_size        = 0x400,
    .tcp_tx_buf_max_size    = 0x400,
    .tcp_rx_buf_max_size    = 0x800,
    // We're not using tcp anyways

    .udp_tx_buf_size = 0x2400,
    .udp_rx_buf_size = 0xA500,

    .sb_efficiency = 2,

    .num_bsd_sessions = 3,
	.bsd_service_type = BsdServiceType_Auto,
	
};

void appInit(void)
{	
    svcSleepThread(2e+8L);
    Result rc;
    rc = hiddbgInitialize();
    if (R_FAILED(rc))
        fatalThrow(rc);

    rc = hiddbgAttachHdlsWorkBuffer();
    if (R_FAILED(rc))
        fatalThrow(rc);
	
    rc = socketInitialize(&sockInitConf);
    if (R_FAILED(rc))
        printf("Socket Failed to Initialize");
	
	rc = fsInitialize();
	if (R_FAILED(rc))
		printf("fs failed to initialize");
	
	fsdevMountSdmc();
}

void appExit(void)
{
	fsdevUnmountAll();
	socketExit();
	fsExit();
    hiddbgReleaseHdlsWorkBuffer();
    hiddbgExit();
}


typedef struct{
	u32 sock;
	struct sockaddr_in addrTo;
} Socket;

enum {
	SCE_CTRL_SELECT     = 0x000001,	//!< Select button.
	SCE_CTRL_START      = 0x000008,	//!< Start button.
	SCE_CTRL_UP         = 0x000010,	//!< Up D-Pad button.
	SCE_CTRL_RIGHT      = 0x000020,	//!< Right D-Pad button.
	SCE_CTRL_DOWN       = 0x000040,	//!< Down D-Pad button.
	SCE_CTRL_LEFT       = 0x000080,	//!< Left D-Pad button.
	SCE_CTRL_LTRIGGER   = 0x000100,	//!< Left trigger.
	SCE_CTRL_RTRIGGER   = 0x000200,	//!< Right trigger.
	SCE_CTRL_TRIANGLE   = 0x001000,	//!< Triangle button.
	SCE_CTRL_CIRCLE     = 0x002000,	//!< Circle button.
	SCE_CTRL_CROSS      = 0x004000,	//!< Cross button.
	SCE_CTRL_SQUARE     = 0x008000	//!< Square button.
};
#define NO_INPUT 0
#define MOUSE_MOV 0x01
#define LEFT_CLICK 0x08
#define RIGHT_CLICK 0x10

typedef struct{
	uint32_t buttons;
	uint8_t lx;
	uint8_t ly;
	uint8_t rx;
	uint8_t ry;
	uint16_t tx;
	uint16_t ty;
	uint8_t click;
} PadPacket;

char host[32];

static int _ParseConfigLine(void *dummy, const char *section, const char *name, const char *value)
{
	if (strcmp(name, "Vita_IP") == 0)
	{
		sprintf(host,"%s",value);
	}
	return 0;
}

void ReCreateSocket()
{
	Socket* my_socket = (Socket*) malloc(sizeof(Socket));
	memset(&my_socket->addrTo, '0', sizeof(my_socket->addrTo));
	my_socket->addrTo.sin_family = AF_INET;
	my_socket->addrTo.sin_port = htons(5000);
	my_socket->addrTo.sin_addr.s_addr = inet_addr(host);
	my_socket->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (my_socket->sock < 0){
		printf("\nFailed creating socket.");
	}else printf("\nClient socket created on port 5000");
	int err = connect(my_socket->sock, (struct sockaddr*)&my_socket->addrTo, sizeof(my_socket->addrTo));
	if (err < 0 ){
	   printf("\nConnection failed!");
    }else{
	   printf("\nConnection established!");
	}
}

// Main program entrypoint
int main(int argc, char* argv[])
{
	ini_parse("/config/vita_pad/config.ini", _ParseConfigLine, NULL);
	
	printf("IP: %s\nPort: %d\n\n",host, GAMEPAD_PORT);
	
	Socket* my_socket = (Socket*) malloc(sizeof(Socket));
	memset(&my_socket->addrTo, '0', sizeof(my_socket->addrTo));
	my_socket->addrTo.sin_family = AF_INET;
	my_socket->addrTo.sin_port = htons(5000);
	my_socket->addrTo.sin_addr.s_addr = inet_addr(host);
	my_socket->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (my_socket->sock < 0){
		printf("\nFailed creating socket.");
		return 0;
	}else printf("\nClient socket created on port 5000");
	
	// Connecting to VitaPad
	uint8_t firstScan = 1;
	PadPacket data;
	PadPacket olddata;
	
    Result rc2=0;
	/*HiddbgAbstractedPadState m_state = {0};
    s8 AbstractedVirtualPadId = 0;
    m_state.type = BIT(0);
    m_state.npadInterfaceType = NpadInterfaceType_Bluetooth;
	
    m_state.flags = 0xff;
    m_state.state.batteryCharge = 4;
	
    m_state.singleColorBody = RGBA8_MAXALPHA(255,255,255);
    m_state.singleColorButtons = RGBA8_MAXALPHA(0,0,0);
    rc = hiddbgSetAutoPilotVirtualPadState(AbstractedVirtualPadId, &m_state);
    printf("hiddbgSetAutoPilotVirtualPadState(): 0x%x\n", rc);*/
    u64 HdlsHandle=0;
    HiddbgHdlsDeviceInfo device = {0};
    HiddbgHdlsState state={0};
    //HidControllerID conID = hidGetHandheldMode() ? CONTROLLER_HANDHELD : CONTROLLER_PLAYER_1;
    device.deviceType = HidDeviceType_FullKey15;
    device.npadInterfaceType = NpadInterfaceType_Bluetooth;
    // Set the controller colors. The grip colors are for Pro-Controller on [9.0.0+].
    device.singleColorBody = RGBA8_MAXALPHA(255,255,255);
    device.singleColorButtons = RGBA8_MAXALPHA(0,0,0);
    device.colorLeftGrip = RGBA8_MAXALPHA(230,255,0);
    device.colorRightGrip = RGBA8_MAXALPHA(0,40,20);

    // Setup example controller state.
    state.batteryCharge = 4; // Set battery charge to full.
    state.joysticks[JOYSTICK_LEFT].dx = 0x1234;
    state.joysticks[JOYSTICK_LEFT].dy = -0x1234;
    state.joysticks[JOYSTICK_RIGHT].dx = 0x5678;
    state.joysticks[JOYSTICK_RIGHT].dy = -0x5678;
	
    bool isAttached;
	static int time = 0;
    // Your code / main loop goes here.
    while (appletMainLoop())
	{	
		send(my_socket->sock, "request", 8, 0);
		int count = recv(my_socket->sock, (char*)&data, sizeof(data), 0);
		if (firstScan){
			firstScan = 0;
			memcpy(&olddata,&data,sizeof(PadPacket));
		}
		
		/*if (count != 0){
			m_state.state.buttons = 0;
			if ((data.buttons & SCE_CTRL_TRIANGLE) && (!(olddata.buttons & SCE_CTRL_TRIANGLE))) m_state.state.buttons |= KEY_X;
			if ((data.buttons & SCE_CTRL_SQUARE) && (!(olddata.buttons & SCE_CTRL_SQUARE))) m_state.state.buttons |= KEY_Y;
			if ((data.buttons & SCE_CTRL_CROSS) && (!(olddata.buttons & SCE_CTRL_CROSS))) m_state.state.buttons |= KEY_B;
			if ((data.buttons & SCE_CTRL_CIRCLE) && (!(olddata.buttons & SCE_CTRL_CIRCLE))) m_state.state.buttons |= KEY_A;
			if ((data.buttons & SCE_CTRL_LTRIGGER) && (!(olddata.buttons & SCE_CTRL_LTRIGGER))) m_state.state.buttons |= KEY_L;
			if ((data.buttons & SCE_CTRL_RTRIGGER) && (!(olddata.buttons & SCE_CTRL_RTRIGGER))) m_state.state.buttons |= KEY_R;
			if ((data.buttons & SCE_CTRL_START) && (!(olddata.buttons & SCE_CTRL_START))) m_state.state.buttons |= KEY_PLUS;
			if ((data.buttons & SCE_CTRL_SELECT) && (!(olddata.buttons & SCE_CTRL_SELECT))) m_state.state.buttons |= KEY_MINUS;
			if ((data.ly < 50) && (!(olddata.ly < 50))) m_state.state.joysticks[JOYSTICK_LEFT].dy = -0x1234;
			if ((data.lx < 50) && (!(olddata.lx < 50))) m_state.state.joysticks[JOYSTICK_LEFT].dx = 0x1234;
			if ((data.buttons & SCE_CTRL_UP) && (!(olddata.buttons & SCE_CTRL_UP))) m_state.state.buttons |= KEY_DUP;
			hiddbgSetAutoPilotVirtualPadState(AbstractedVirtualPadId, &m_state);
			memcpy(&olddata,&data,sizeof(PadPacket));
		}*/
		if (count != 0 && count != -1){
            rc2 = hiddbgSetHdlsState(HdlsHandle, &state);
            if (R_FAILED(rc2)) printf("hiddbgSetHdlsState(): 0x%x\n", rc2);
			    state.buttons = 0;
			    if (data.buttons & SCE_CTRL_TRIANGLE) state.buttons |= KEY_X;
			    if (data.buttons & SCE_CTRL_SQUARE) state.buttons |= KEY_Y;
			    if (data.buttons & SCE_CTRL_CROSS) state.buttons |= KEY_B;
			    if (data.buttons & SCE_CTRL_CIRCLE) state.buttons |= KEY_A;
			    if (data.buttons & SCE_CTRL_LTRIGGER) state.buttons |= KEY_L;
			    if (data.buttons & SCE_CTRL_RTRIGGER) state.buttons |= KEY_R;
			    if (data.buttons & SCE_CTRL_START) state.buttons |= KEY_PLUS;
			    if (data.buttons & SCE_CTRL_SELECT) state.buttons |= KEY_MINUS;
			    if (data.click & MOUSE_MOV && data.tx < SCREEN_WIDTH/2 && data.ty < SCREEN_HEIGHT/2) state.buttons |= KEY_LSTICK;
			    if (data.click & MOUSE_MOV && data.tx > SCREEN_WIDTH/2 && data.ty < SCREEN_HEIGHT/2) state.buttons |= KEY_RSTICK;
			    if (data.click & MOUSE_MOV && data.tx < SCREEN_WIDTH/2 && data.ty > SCREEN_HEIGHT/2) state.buttons |= KEY_CAPTURE;
			    if (data.click & MOUSE_MOV && data.tx > SCREEN_WIDTH/2 && data.ty > SCREEN_HEIGHT/2) state.buttons |= KEY_HOME;
			    if (data.click & LEFT_CLICK) state.buttons |= KEY_ZL;
			    if (data.click & RIGHT_CLICK) state.buttons |= KEY_ZR;
			    if (data.buttons & SCE_CTRL_UP) state.buttons |= KEY_DUP;
			    if (data.buttons & SCE_CTRL_DOWN) state.buttons |= KEY_DDOWN;
			    if (data.buttons & SCE_CTRL_LEFT) state.buttons |= KEY_DLEFT;
			    if (data.buttons & SCE_CTRL_RIGHT) state.buttons |= KEY_DRIGHT;
			    state.joysticks[JOYSTICK_LEFT].dx = (data.lx - 128) * 255;
			    state.joysticks[JOYSTICK_LEFT].dy = (128 - data.ly) * 255;
			    state.joysticks[JOYSTICK_RIGHT].dx = (data.rx - 128) * 255;
			    state.joysticks[JOYSTICK_RIGHT].dy = (128 - data.ry) * 255;
			    memcpy(&olddata,&data,sizeof(PadPacket));
            if (R_SUCCEEDED(hiddbgIsHdlsVirtualDeviceAttached(HdlsHandle, &isAttached)))
            {
               if (!isAttached)
	        		hiddbgAttachHdlsVirtualDevice(&HdlsHandle, &device);
            }
	    }else{
            //svcSleepThread(5e+8L);
			if (time == 60) {
				time = 0;
			    hiddbgDetachHdlsVirtualDevice(HdlsHandle);
		        close(my_socket->sock);
			    ReCreateSocket();
			}else{
				time++;
			}
		}
        svcSleepThread(1e+7L);
	}
    // If you need threads, you can use threadCreate etc.
	return 0;
}

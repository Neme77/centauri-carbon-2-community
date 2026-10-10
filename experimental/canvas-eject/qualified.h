#pragma once
// Qualified stock 02.01.00.00 symbols; see ABI.md.
#define MOTOR_SYMBOL "_ZN6elegoo6extras6Canvas24CMD_canvas_motor_controlESt10shared_ptrI12GCodeCommandE"
static const unsigned char MOTOR_ENTRY[8]={0x10,0x48,0x2d,0xe9,0x08,0xb0,0x8d,0xe2};
#define PARSE_SYMBOL "_ZN6elegoo6extras14CanvasProtocol23PARSE_CANVAS_ALL_STATUSERKSt6vectorIhSaIhEE"
static const unsigned char PARSE_ENTRY[8]={0x10,0x48,0x2d,0xe9,0x08,0xb0,0x8d,0xe2};
#define PREFEED_SYMBOL "_ZN6elegoo6extras6Canvas21auto_prefeed_filamentEv"
static const unsigned char PREFEED_ENTRY[8]={0x30,0x48,0x2d,0xe9,0x02,0x8b,0x2d,0xed};
#define READY_SYMBOL "_ZN6elegoo6extras6Canvas12handle_readyEv"
static const unsigned char READY_ENTRY[8]={0x70,0x48,0x2d,0xe9,0x10,0xb0,0x8d,0xe2};
#define SHUTDOWN_SYMBOL "_ZN6elegoo6extras6Canvas15handle_shutdownEv"
static const unsigned char SHUTDOWN_ENTRY[8]={0x04,0xb0,0x2d,0xe5,0x00,0xb0,0x8d,0xe2};
#define MOVE_SYMBOL "_ZN6elegoo6extras14CanvasProtocol23feeder_filament_controlEhRKNS0_11FeederMotorEb"
#define STOP_SYMBOL "_ZN6elegoo6extras14CanvasProtocol21feeders_filament_stopEhbb"
#define ROCKER_SYMBOL "_ZN6elegoo6extras14CanvasProtocol14rocker_controlEhab"
#define GUI_CREATE 0x407e0
static const unsigned char GUI_CREATE_ENTRY[8]={0xf0,0x4f,0x2d,0xe9,0x00,0x60,0x50,0xe2};
#define GUI_FILAMENT_VIEW 0x24e4b8
#define GUI_LABEL_TEXT 0x12224c
#define GUI_CLEAR_STATE 0xf4634
#define GUI_ADD_STATE 0xf4620
#define GUI_MACHINE_STATUS 0xaecb0
#define GUI_SEND_GCODE 0xbb234
#define GUI_TIMER_DELETE 0x115358
#define GUI_HEIGHT 0xf5bac
#define GUI_WIDTH 0xf5b94
#define GUI_UPDATE_LAYOUT 0xf72b4
#define GUI_ADD_FLAG 0xf44b8
#define GUI_SCROLL_DIR 0xf7a20
#define GUI_BUTTON 0x11a3ac
#define GUI_SIZE 0xf57a4
#define GUI_POS 0xf56d4
#define GUI_LABEL 0x12127c
#define GUI_ALIGN 0xf5a1c
#define GUI_EVENT 0xef4a8
#define GUI_TIMER 0x115248

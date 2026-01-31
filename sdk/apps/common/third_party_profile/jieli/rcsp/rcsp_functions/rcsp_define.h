#ifndef __RCSP_DEFINE_H__
#define __RCSP_DEFINE_H__

#define RCSP_MODE_OFF	                				(0)			// RCSP功能关闭
#define RCSP_MODE_COMMON                				(1)			// 适用于通用设备，如hid
#define RCSP_MODE_SOUNDBOX              				(2)			// 适用于音箱SDK
#define RCSP_MODE_WATCH		            				(3)			// 适用于手表SDK
#define RCSP_MODE_EARPHONE              				(4)			// 适用于耳机SDK

#define RCSP_MSG_DISTRIBUTION_VER_DEFAULT				(0)			// 默认消息分发管理
#define RCSP_MSG_DISTRIBUTION_VER_VISUAL_CFG_TOOL		(1)			// 可视化配置工具的消息分发管理

#define		RCSP_SDK_TYPE_AC690X						0x0
#define		RCSP_SDK_TYPE_AC692X						0x1
#define 	RCSP_SDK_TYPE_AC693X						0x2
#define 	RCSP_SDK_TYPE_AC695X 						0x3
#define		RCSP_SDK_TYPE_AC697X 						0x4
#define		RCSP_SDK_TYPE_AC696X 						0x5
#define		RCSP_SDK_TYPE_AC696X_TWS					0x6
#define		RCSP_SDK_TYPE_AC695X_WATCH					0x8
#define		RCSP_SDK_TYPE_JL701N_WATCH					0x9

//===========================================================================================
// RCSP命令码
/***************************************/
/*     注意：不能在这个枚举里加代码    */
/*     注意：不能在这个枚举里加代码    */
/*     注意：不能在这个枚举里加代码    */
/***************************************/
#define JL_OPCODE_SET_ADV 							    0xC0
#define	JL_OPCODE_GET_ADV  								0xC1
#define JL_OPCODE_ADV_DEVICE_NOTIFY 					0xC2
#define JL_OPCODE_ADV_NOTIFY_SETTING 					0xC3
#define JL_OPCODE_ADV_DEVICE_REQUEST					0xC4

#define JL_OPCODE_GET_DEVICE_UPDATE_FILE_INFO_OFFSET    0xE1
#define JL_OPCODE_INQUIRE_DEVICE_IF_CAN_UPDATE          0xE2
#define JL_OPCODE_ENTER_UPDATE_MODE                     0xE3
#define JL_OPCODE_EXIT_UPDATE_MODE                      0xE4
#define JL_OPCODE_SEND_FW_UPDATE_BLOCK                  0xE5
#define JL_OPCODE_GET_DEVICE_REFRESH_FW_STATUS          0xE6
#define JL_OPCODE_SET_DEVICE_REBOOT                     0xE7
#define JL_OPCODE_NOTIFY_UPDATE_CONENT_SIZE				0xE8
/***************************************/
/***************************************/

// 0xC1:获取设备详情信息命令
#define ATTR_TYPE_BAT_VALUE  			(0)				// 小机电量
#define ATTR_TYPE_EDR_NAME   			(1)				// EDR名称
#define ATTR_TYPE_KEY_SETTING  			(2)				// 按键功能
#define ATTR_TYPE_LED_SETTING  			(3)				// LED显示
#define ATTR_TYPE_MIC_SETTING  			(4)				// MIC模式
#define ATTR_TYPE_WORK_MODE  			(5)				// 工作模式
#define ATTR_TYPE_PRODUCT_MESSAGE  		(6)				// 产品信息
#define ATTR_TYPE_TIME_STAMP			(7)				// 连接时间(暂未使用)
#define ATTR_TYPE_EAR_DETECTION			(8)				// 入耳检测(暂未使用)
#define ATTR_TYPE_LANGUAGE				(9)				// 语言(暂未使用)
#define ATTR_TYPE_ANC_VOICE_KEY			(10)			// 详情界面显示ANC按键

// 内部使用
#define ATTR_TYPE_EQ_SETTING			(101)
#define ATTR_TYPE_HIGH_LOW_VOL			(102)
#define ATTR_TYPE_ANC_VOICE			    (103)
#define ATTR_TYPE_ASSISTED_HEARING	    (104)
#define ATTR_TYPE_VOL_SETTING			(105)
#define ATTR_TYPE_MISC_SETTING			(106)
#define ATTR_TYPE_COLOR_LED_SETTING 	(107)
#define ATTR_TYPE_KARAOKE_EQ_SETTING	(108)
#define ATTR_TYPE_KARAOKE_SETTING		(109)

// BT_ADV_SET_EDR_CON_FLAG
#define SECNE_DISMISS			(0x00)
#define SECNE_UNCONNECTED		(0x01)
#define SECNE_CONNECTED			(0x02)
#define SECNE_CONNECTING		(0x03)
#define SECNE_CONNECTINLESS		(0x04)

#define TWS_FUNC_ID_SEQ_RAND_SYNC	(('S' << (3 * 8)) | ('E' << (2 * 8)) | ('Q' << (1 * 8)) | ('\0'))

#endif

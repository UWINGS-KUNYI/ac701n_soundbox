#ifndef __RCSP_UPDATE_MASTER_H__
#define __RCSP_UPDATE_MASTER_H__

#include "system/includes.h"

// 升级状态
typedef enum {
    RCSP_OTAProgressStatusSuccess             = 0x00, //OTA升级成功
    RCSP_OTAProgressStatusFail                = 0x01, //OTA升级失败
    RCSP_OTAProgressStatusDataIsNull          = 0x02, //OTA升级数据为空
    RCSP_OTAProgressStatusCommandFail         = 0x03, //OTA指令失败
    RCSP_OTAProgressStatusSeekFail            = 0x04, //OTA标示偏移查找失败
    RCSP_OTAProgressStatusInfoFail            = 0x05, //OTA升级固件信息错误
    RCSP_OTAProgressStatusLowPower            = 0x06, //OTA升级设备电压低
    RCSP_OTAProgressStatusEnterFail           = 0x07, //未能进入OTA升级模式
    RCSP_OTAProgressStatusUpgrading           = 0x08, //OTA升级中
    RCSP_OTAProgressStatusReconnect           = 0x09, //OTA需重连设备(uuid方式)
    RCSP_OTAProgressStatusReboot              = 0x0a, //OTA需设备重启
    RCSP_OTAProgressStatusPreparing           = 0x0b, //OTA准备中
    RCSP_OTAProgressStatusPrepared            = 0x0f, //OTA准备完成
    RCSP_OTAProgressStatusFailVerification    = 0xf1, //升级数据校验失败
    RCSP_OTAProgressStatusFailCompletely      = 0xf2, //升级失败
    RCSP_OTAProgressStatusFailKey             = 0xf3, //升级数据校验失败
    RCSP_OTAProgressStatusFailErrorFile       = 0xf4, //升级文件出错
    RCSP_OTAProgressStatusFailUboot           = 0xf5, //uboot不匹配
    RCSP_OTAProgressStatusFailLenght          = 0xf6, //升级过程长度出错
    RCSP_OTAProgressStatusFailFlash           = 0xf7, //升级过程flash读写失败
    RCSP_OTAProgressStatusFailCmdTimeout      = 0xf8, //升级过程指令超时
    RCSP_OTAProgressStatusFailSameVersion     = 0xf9, //相同版本
    RCSP_OTAProgressStatusFailTWSDisconnect   = 0xfa, //TWS耳机未连接
    RCSP_OTAProgressStatusFailNotInBin        = 0xfb, //耳机未在充电仓
    RCSP_OTAProgressStatusReconnectWithMacAddr = 0xfc, //OTA需重连设备(mac方式)
    RCSP_OTAProgressStatusUnknown,                    //OTA未知错误
} RCSP_OTAProgressStatus;

/**
 * @brief 设置设备不能升级
 */
void rcsp_set_device_cant_update();

/**
 * @brief 获取升级设备信息，升级前需要调用一次，用于获取设备当前的升级状态
 */
void rcsp_get_device_feature();

/**
 * @brief 获取升级设备信息后传入设备信息
 *
 * @param isDualBankUpdate
 * @param isForceUpdate
 * @param needDownloadLoader
 */
void rcsp_get_device_feature_response(bool isDualBankUpdate, bool isForceUpdate, bool needDownloadLoader);

/**
 * @brief 升级流程信息回调
 *
 * @param status
 * @param progress
 */
__attribute__((weak))
void rcsp_progress_status_with_status_and_progress(RCSP_OTAProgressStatus status, float progress);

/**
 * @brief 开始升级函数
 *
 * @param otaFilePath ota升级文件路径
 * @param otaFd 文件句柄
 * @result 待升级设备数量，如果>0则可以进行升级
 */
void rcsp_ota_with_file_path(char *otaFilePath, void *otaFd);

/**
 * @brief rcsp命令接收
 *
 * @param priv
 * @param opCode
 * @param status
 * @param opCode_SN -1:为收到命令需回复
 * @param data
 * @param len
 * @result ret success:0
 */
int rcsp_m_update_cmd_handle(void *priv, u8 opCode, u8 status, u8 opCode_SN, u8 *data, u16 len);

#endif // #define __RCSP_UPDATE_MASTER_H__


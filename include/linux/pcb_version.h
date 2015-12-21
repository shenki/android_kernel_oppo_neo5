/************************************************************
** Copyright (C), 2008-2012, OPPO Mobile Comm Corp., Ltd
**
** File: - pcb_version.h
* Description: header file for pcb_version.

** Version: 1.0
** Date : 2013/10/15
** Author: yuyi@Dep.Group.Module
************************************************************/
#ifndef _PCB_VERSION_H
#define _PCB_VERSION_H

enum {
	PCB_VERSION_UNKNOWN,
	HW_VERSION__10, 	//1452mV
	HW_VERSION__11, 	//1636 mV
	HW_VERSION__12, 	//1224 mV
	HW_VERSION__13, 	//900 mV
	HW_VERSION__14, 	//720 mV

};
enum {
	RF_VERSION_UNKNOWN,
	RF_VERSION__11,		//WCDMA_GSM_China
	RF_VERSION__12,		//WCDMA_GSM_LTE_Europe
	RF_VERSION__13,		//WCDMA_GSM_LTE_America
	RF_VERSION__21,		//WCDMA_GSM_CDMA_China
	RF_VERSION__22,		//WCDMA_GSM_Europe
	RF_VERSION__23,		//WCDMA_GSM_America
	RF_VERSION__31,		//TD_GSM
	RF_VERSION__32,		//TD_GSM_LTE
	RF_VERSION__33,		//
};

extern int get_pcb_version(void);
extern int get_rf_version(void);

#endif /* _PCB_VERSION_H */
#ifndef __MICROP_DATA_DEF_H
#define __MICROP_DATA_DEF_H


struct microP_platform_data{
        int intr_gpio;
};

enum MICROP_CMD_ID {
    MICROP_HW_ID=0,
    MICROP_FW_VER,
    MICROP_GPIO_INPUT_LEVEL,
    MICROP_GPIO_OUTPUT_LEVEL,
    MICROP_CHARGING_STATUS,
    MICROP_GAUGE_ID,
    MICROP_GAUGE_CAP,
    MICROP_GAUGE_TMP,
    MICROP_USB_DET,
    MICROP_PWM,            
    MICROP_INTR_STATUS,
    MICROP_INTR_EN,
    MICROP_BOOT_SELECTION,
    MICROP_SET_BOOT_LDROM,
    MICROP_SET_BOOT_APROM,
    MICROP_PROGRAM_APROM,
    MICROP_APROM_CHECKSUM,
    MICROP_SOFTWARE_OFF,
    MICROP_LDROM_ID_CODE,
    MICROP_GPIO_OUTPUT_BIT_SET,
    MICROP_GPIO_OUTPUT_BIT_CLR,
    MICROP_INTR_EN_BIT_SET,
    MICROP_INTR_EN_BIT_CLR,
    MICROP_IND_A68_READY,
    MICROP_IND_A68_SLEEP,
    MICROP_IND_A68_RESUME,
    MICROP_ADC_LEVEL,
    MICROP_OPERATING_STATE,
    MICROP_MHL_ID,    
    MICROP_ISN,
    MICROP_LD_PROG_PROGRESS,
    MICROP_POWER_ON_REASON,
    MICROP_DISABLE_CHARGING_FOR_FACTORY,
    MICROP_GET_MVRAM_STATE_FOR_FACTORY,
    MICROP_CALIBRATION_DATA,
    MICROP_GAUGE_VOLTAGE,
    MICROP_GAUGE_AVG_CURRENT,
    MICROP_ALWAYS_IGNORE_A68READY,
    
#if 0    
    MICROP_TEST_MODE,
#endif    
};

enum MICROP_OP_STATE{
        st_MICROP_Off=0,
        st_MICROP_Active=1,
        st_MICROP_Sleep=2,
        st_MICROP_Unknown=3,

};

enum MICROP_POWERON_REASON{
        E_NONE=0,
        E_ON_HS_IN=1,
        E_ON_AC_USB=2,
        E_ON_PWR_KEY_LONGPRESS=3,
};

// define ioctl code
#define ASUS_MICROP_IOC_TYPE    0xa1
#define ASUS_MICROP_FW_UPDATE 	_IOWR(ASUS_MICROP_IOC_TYPE,	1,	int)
#define ASUS_MICROP_CHECK_CONNECTED 	_IOWR(ASUS_MICROP_IOC_TYPE,	2,	int)
#define ASUS_MICROP_GET_FW_VERSION 	_IOWR(ASUS_MICROP_IOC_TYPE,	3,	int)
#define ASUS_MICROP_ON_OFF_GPS_ON_PAD 	_IOWR(ASUS_MICROP_IOC_TYPE,	4,	int)
#define ASUS_MICROP_GET_LDROM_VERSION 	_IOWR(ASUS_MICROP_IOC_TYPE,	5,	int)
#define ASUS_MICROP_GET_PADPHONE_HW_ID 	_IOWR(ASUS_MICROP_IOC_TYPE,	6,	int)
#define ASUS_MICROP_MAX_NR  6




#endif


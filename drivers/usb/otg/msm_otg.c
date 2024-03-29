/* Copyright (c) 2009-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/uaccess.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>

#include <linux/usb.h>
#include <linux/usb/otg.h>
#include <linux/usb/ulpi.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/usb/quirks.h>
#include <linux/usb/msm_hsusb.h>
#include <linux/usb/msm_hsusb_hw.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/pm8xxx/pm8921-charger.h>
#include <linux/mfd/pm8xxx/misc.h>
#include <linux/power_supply.h>

#include <mach/clk.h>
#include <mach/mpm.h>
#include <mach/msm_xo.h>
#include <mach/msm_bus.h>
#include <mach/rpm-regulator.h>
//ASUS_BSP+++ BennyCheng "add proc debug files"
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#define GPIO_APQ_MDM_SW_SEL 45
#define GPIO_USB_MHL_SW_SEL 86
enum usb_apq_mdm_sw {
	USB_MDM = 0,
	USB_APQ,
};

enum usb_mhl_sw {
	USB_PORT = 0,
	MHL_PORT,
};
//ASUS_BSP--- BennyCheng "add proc debug files"
//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
#ifdef CONFIG_EEPROM_NUVOTON
#include <linux/microp_notify.h>
#include <linux/microp_api.h>
#include <linux/microp_pin_def.h>
#include <linux/microp.h>
#endif
#include <linux/fs.h>
#include <linux/earlysuspend.h>
static bool pad_hub_pm_enable = 1;
static bool is_usb_storage_plugged = 0;
static bool msm_otg_bsv = 0;
static bool is_suspend_delay_work = 0;
enum microp_mode_sw {
	MICROP_SLEEP = 0,
	MICROP_ACTIVE,
};
enum host_auto_sw {
	HOST_AUTO_NONE = 0,
	HOST_AUTO_HOST,
};
const char *usb_storage_list[] = {"/Removable/USBdisk1", "/Removable/USBdisk2", "/Removable/SD"};
static struct workqueue_struct *early_suspend_delay_wq;
static struct delayed_work early_suspend_delay_work;
static struct wake_lock early_suspend_wlock;
static struct work_struct late_resume_work;
static struct workqueue_struct *microp_cb_delay_wq;
static struct delayed_work microp_cb_delay_work;
static int msm_otg_usb_mhl_switch(enum usb_mhl_sw req_side);
extern bool pad_exist(void);
//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"
//ASUS_BSP+++ BennyCheng "implement factory usb mode"
#ifdef ASUS_FACTORY_BUILD
#define GPIO_FACTORY_USB_ENABLE 77
#endif
//ASUS_BSP--- BennyCheng "implement factory usb mode"
//ASUS_BSP+++ BennyCheng "prevent otg wakelock caused by vbus interrputs"
#define MSM_OTG_SUSPEND_CHECK_TIMEOUT 10000L
//ASUS_BSP--- BennyCheng "prevent otg wakelock caused by vbus interrputs"
//ASUS_BSP+++ BennyCheng "add mutex protection when pm/runtime suspend"
#include <linux/mutex.h>
struct mutex msm_otg_mutex;
//ASUS_BSP--- BennyCheng "add mutex protection when pm/runtime suspend"

//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
#include <linux/asus_chg.h>
static struct delayed_work asus_chg_work;
static struct work_struct asus_usb_work;
static int g_charger_mode = ASUS_CHG_SRC_NONE;
enum msm_otg_usb_boot_state {
	MSM_OTG_USB_BOOT_INIT,
	MSM_OTG_USB_BOOT_IRQ,//check IRQ to make sure USB is ready
	MSM_OTG_USB_BOOT_DOWN,
};
static int g_usb_boot = MSM_OTG_USB_BOOT_INIT;
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"

//ASUS_BSP+++ "[USB][NA][Other] Add USB event log"
#include <linux/asusdebug.h>
//ASUS_BSP--- "[USB][NA][Other] Add USB event log"

//ASUS_BSP+++ "[USB][NA][Other] avoid charger mode notify in Pad"
static int g_host_mode = 0;
//ASUS_BSP--- "[USB][NA][Other] avoid charger mode notify in Pad"

#define MSM_USB_BASE	(motg->regs)
#define DRIVER_NAME	"msm_otg"

#define ID_TIMER_FREQ		(jiffies + msecs_to_jiffies(500))
#define ULPI_IO_TIMEOUT_USEC	(10 * 1000)
#define USB_PHY_3P3_VOL_MIN	3050000 /* uV */
#define USB_PHY_3P3_VOL_MAX	3300000 /* uV */
#define USB_PHY_3P3_HPM_LOAD	50000	/* uA */
#define USB_PHY_3P3_LPM_LOAD	4000	/* uA */

#define USB_PHY_1P8_VOL_MIN	1800000 /* uV */
#define USB_PHY_1P8_VOL_MAX	1800000 /* uV */
#define USB_PHY_1P8_HPM_LOAD	50000	/* uA */
#define USB_PHY_1P8_LPM_LOAD	4000	/* uA */

#define USB_PHY_VDD_DIG_VOL_NONE	0 /*uV */
#define USB_PHY_VDD_DIG_VOL_MIN	1045000 /* uV */
#define USB_PHY_VDD_DIG_VOL_MAX	1320000 /* uV */

static DECLARE_COMPLETION(pmic_vbus_init);
static struct msm_otg *the_msm_otg;
static bool debug_aca_enabled;
static bool debug_bus_voting_enabled;

//ASUS_BSP+++ BennyCheng "prevent otg wakelock caused by vbus interrputs"
static void msm_otg_suspend_check(struct work_struct *work)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;

	dev_info(otg->dev, "check otg suspend status (%d)\n", pm_runtime_suspended(otg->dev));

	if (!pm_runtime_suspended(otg->dev)) {
		wake_unlock(&motg->wlock);
	}
}

static DECLARE_DELAYED_WORK(msm_otg_suspend_check_work, msm_otg_suspend_check);
//ASUS_BSP--- BennyCheng "prevent otg wakelock caused by vbus interrputs"

//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
static bool msm_otg_check_usb_storage_plugged(void)
{
	struct file *flp = NULL;
	mm_segment_t oldfs;
	int index = 0, num = 0, ret = 0;

	oldfs = get_fs();
	set_fs(get_ds());

	num = sizeof(usb_storage_list)/sizeof(usb_storage_list[0]);

	for(index = 0; index < 3; index++) {
		flp = filp_open(usb_storage_list[index], O_RDONLY, S_IRWXU);
		if(IS_ERR(flp))
			continue;
		else {
			ret = 1;
			filp_close(flp, NULL);
			break;
		}
	}

	set_fs(oldfs);

	return ret;
}

static int msm_otg_get_pad_hub_power(void)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;
	int pin_level = -1;

#ifdef CONFIG_EEPROM_NUVOTON
	if (AX_MicroP_IsP01Connected() && pad_exist()) {
		pin_level = AX_MicroP_getGPIOOutputPinLevel(OUT_uP_HUB_PWR_EN);
		if (pin_level < 0) {
			dev_err(otg->dev, "get pad hub power status failed! (%d)\n", pin_level);
		} else {
			dev_dbg(otg->dev, "get pad hub power status success (%d)\n", pin_level);
		}
	} else {
		dev_dbg(otg->dev, "not in pad, cannot get hub power status! (%d)(%d)\n",
			AX_MicroP_IsP01Connected(), pad_exist());
	}

	return pin_level;
#else
	dev_dbg(otg->dev, "msm_otg_get_pad_hub_power() not support\n");
	return pin_level;
#endif
}

static int msm_otg_set_pad_hub_power(bool on)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;
	int ret = -1;

#ifdef CONFIG_EEPROM_NUVOTON
	if (AX_MicroP_IsP01Connected() && pad_exist()) {
		ret = AX_MicroP_setGPIOOutputPin(OUT_uP_HUB_PWR_EN, on);
		if (ret < 0) {
			dev_err(otg->dev, "fail to set pad hub power! (%d)(%d)\n", on, ret);
		} else {
			dev_dbg(otg->dev, "set pad hub power success (%d)\n", on);
		}
	} else {
		dev_dbg(otg->dev, "not in pad, skip pad hub power control! (%d)(%d)(%d)\n",
			on, AX_MicroP_IsP01Connected(), pad_exist());
	}

	return ret;
#else
	dev_dbg(otg->dev, "msm_otg_set_pad_hub_power() not support\n");
	return ret;
#endif
}

static int msm_otg_get_pad_camera_power(void)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;
	int pin_level = -1;

#ifdef CONFIG_EEPROM_NUVOTON
	if (AX_MicroP_IsP01Connected() && pad_exist()) {
		pin_level = AX_MicroP_getGPIOOutputPinLevel(OUT_uP_CAM_PWR_EN);
		if (pin_level < 0) {
			dev_err(otg->dev, "get pad camera power status failed! (%d)\n", pin_level);
		} else {
			dev_dbg(otg->dev, "get pad camera power status success (%d)\n", pin_level);
		}
	} else {
		dev_dbg(otg->dev, "not in pad, cannot get camera power status! (%d)(%d)\n",
			AX_MicroP_IsP01Connected(), pad_exist());
	}

	return pin_level;
#else
	dev_dbg(otg->dev, "msm_otg_get_pad_camera_power() not support\n");
	return pin_level;
#endif
}

static int msm_otg_set_pad_camera_power(bool on)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;
	int ret = -1;

#ifdef CONFIG_EEPROM_NUVOTON
	if (AX_MicroP_IsP01Connected() && pad_exist()) {
		ret = AX_MicroP_setGPIOOutputPin(OUT_uP_CAM_PWR_EN, on);
		if (ret < 0) {
			dev_err(otg->dev, "fail to set pad camera power! (%d)(%d)\n", on, ret);
		} else {
			dev_dbg(otg->dev, "set pad camera power success (%d)\n", on);
		}
	} else {
		dev_dbg(otg->dev, "not in pad, skip pad camera power control! (%d)(%d)(%d)\n",
			on, AX_MicroP_IsP01Connected(), pad_exist());
	}

	return ret;
#else
	dev_dbg(otg->dev, "msm_otg_set_pad_camera_power() not support\n");
	return ret;
#endif
}

static int msm_otg_get_pad_cbus_en(void)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;
	int pin_level = -1;

#ifdef CONFIG_EEPROM_NUVOTON
	if (AX_MicroP_IsP01Connected() && pad_exist()) {
		pin_level = AX_MicroP_getGPIOOutputPinLevel(OUT_uP_MHL_CBUS_EN);
		if (pin_level < 0) {
			dev_err(otg->dev, "get pad cbus enable status failed! (%d)\n", pin_level);
		} else {
			dev_dbg(otg->dev, "get pad cbus enable status success (%d)\n", pin_level);
		}
	} else {
		dev_dbg(otg->dev, "not in pad, cannot get cbus enable status! (%d)(%d)\n",
			AX_MicroP_IsP01Connected(), pad_exist());
	}

	return pin_level;
#else
	dev_dbg(otg->dev, "msm_otg_get_pad_cbus_en() not support\n");
	return pin_level;
#endif
}

static int msm_otg_set_pad_cbus_en(bool on)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;
	int ret = -1;

#ifdef CONFIG_EEPROM_NUVOTON
	if (AX_MicroP_IsP01Connected() && pad_exist()) {
		ret = AX_MicroP_setGPIOOutputPin(OUT_uP_MHL_CBUS_EN, on);
		if (ret < 0) {
			dev_err(otg->dev, "fail to set pad cbus enable! (%d)(%d)\n", on, ret);
		} else {
			dev_dbg(otg->dev, "set pad cbus enable success (%d)\n", on);
		}
	} else {
		dev_dbg(otg->dev, "not in pad, skip pad cbus enable control! (%d)(%d)(%d)\n",
			on, AX_MicroP_IsP01Connected(), pad_exist());
	}

	return ret;
#else
	dev_dbg(otg->dev, "msm_otg_set_pad_cbus_en() not support\n");
	return ret;
#endif
}

static void msm_otg_set_microp_mode(enum microp_mode_sw mode) {
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;
#ifdef CONFIG_EEPROM_NUVOTON
	int ret = 0, retries = 0;

	if (AX_MicroP_IsP01Connected() && pad_exist()) {
		switch (mode) {
		case MICROP_SLEEP:
			ret = AX_MicroP_enterSleeping();
			if (ret >= 0) {
				while (st_MICROP_Sleep != AX_MicroP_getOPState() && retries++ < 5);

				if (retries <= 5)
					dev_dbg(otg->dev, "microp enter sleep success\n");
				else
					dev_err(otg->dev, "microp fail to enter sleep!\n");
			} else {
				dev_err(otg->dev, "fail to set microp to sleep! (%d)\n", ret);
			}
			break;
		case MICROP_ACTIVE:
			ret = AX_MicroP_enterResuming();
			if (ret >= 0) {
                            dev_dbg(otg->dev, "microp exit sleep success\n");
			} else {
				dev_err(otg->dev, "fail to set microp to active! (%d)\n", ret);
			}
			break;
		default:
				dev_err(otg->dev, "unknown microp mode! (%d)\n", mode);
			break;
		}
	}
#else
	dev_dbg(otg->dev, "msm_otg_set_microp_sleep() not support!\n");
#endif
}

void msm_otg_microp_sleep(void) {
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;

	if (pad_hub_pm_enable && !test_bit(ID, &motg->inputs)) {
		dev_info(otg->dev, "%s()+++ (%d)\n", __func__, is_usb_storage_plugged);

		if (is_usb_storage_plugged) {
			msm_otg_set_microp_mode(MICROP_SLEEP);
		}

		dev_info(otg->dev, "%s()---\n", __func__);
	}
}

static void msm_otg_host_auto_switch(enum host_auto_sw req_mode)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;

	switch (req_mode) {
	case HOST_AUTO_NONE:
		printk("[usb_otg] switch to auto none mode\r\n");
		set_bit(ID, &motg->inputs);
		clear_bit(B_SESS_VLD, &motg->inputs);
		break;
	case HOST_AUTO_HOST:
		printk("[usb_otg] switch to auto host mode\r\n");
		clear_bit(ID, &motg->inputs);
		break;
	default:
		printk("[usb_otg] unknown auto mode!!! (%d)\r\n", req_mode);
		goto out;
	}

	pm_runtime_resume(otg->dev);
	queue_work(system_nrt_wq, &motg->sm_work);
out:
	return;
}

static void msm_otg_microp_sleep_delay_work(struct work_struct *w)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;

	if (pad_hub_pm_enable) {
		is_usb_storage_plugged = msm_otg_check_usb_storage_plugged();

		dev_info(otg->dev, "%s()+++\n", __func__);
		dev_info(otg->dev, "is_usb_storage_plugged (%d)\n", is_usb_storage_plugged);

		is_suspend_delay_work = 1;

		if (!is_usb_storage_plugged) {
			msm_otg_host_auto_switch(HOST_AUTO_NONE);

			if (msm_otg_get_pad_cbus_en()) {
				msm_otg_set_pad_cbus_en(0);
			}

			msm_otg_set_microp_mode(MICROP_SLEEP);
		} else {
			/*
			 * If a usb storage is plugged, unlocking lock here to allow the usb storgae enter pm suspend and
			 * turning off the power of hub in the very end of msm_otg_pm_suspend
			 */
			if (msm_otg_get_pad_cbus_en()) {
				msm_otg_set_pad_cbus_en(0);
			}

			if (msm_otg_get_pad_camera_power()) {
				msm_otg_set_pad_camera_power(0);
			}

			wake_unlock(&motg->wlock);
		}

		dev_info(otg->dev, "%s()---\n", __func__);
	}
}

static void msm_otg_late_resume_work(struct work_struct *w)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;
	int wait = 0;

	if (pad_hub_pm_enable) {
		dev_info(otg->dev, "%s()+++\n", __func__);

		while ((otg->state != OTG_STATE_B_IDLE) && (wait++ < 10)) {
			msleep(100);
		}

		if (wait >= 10) {
			dev_err(otg->dev, "fail to wait right state for switching auto host mode\n");
			return;
		}

		msm_otg_host_auto_switch(HOST_AUTO_HOST);

		dev_info(otg->dev, "%s()--- (%d)\n", __func__, wait);
	}
}

static void usb_pad_hub_early_suspend(struct early_suspend *h)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;

	if ((USB_HOST == motg->otg_mode || USB_AUTO == motg->otg_mode) &&
			AX_MicroP_IsP01Connected() && pad_exist()) {
		dev_info(otg->dev, "%s()+++\n", __func__);

		if (pad_hub_pm_enable) {
			wake_lock_timeout(&early_suspend_wlock, 5 * HZ);
			cancel_work_sync(&late_resume_work);
			queue_delayed_work_on(0, early_suspend_delay_wq, &early_suspend_delay_work, 4 * HZ);
		}

		dev_info(otg->dev, "%s()---\n", __func__);
	}
}

static void usb_pad_hub_late_resume(struct early_suspend *h)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;

	if ((USB_HOST == motg->otg_mode || USB_AUTO == motg->otg_mode) &&
			AX_MicroP_IsP01Connected() && pad_exist()) {
		dev_info(otg->dev, "%s()+++\n", __func__);

		if (pad_hub_pm_enable) {
			cancel_delayed_work_sync(&early_suspend_delay_work);
			if (is_suspend_delay_work) {
				queue_work(system_nrt_wq, &late_resume_work);
				is_suspend_delay_work = 0;
			}

			msm_otg_set_microp_mode(MICROP_ACTIVE);

			if (!msm_otg_get_pad_cbus_en()) {
				msm_otg_set_pad_cbus_en(1);
			}

			msm_otg_set_pad_hub_power(1);
			msm_otg_set_pad_camera_power(1);
		}

		dev_info(otg->dev, "%s()---\n", __func__);
	}
}

struct early_suspend usb_pad_hub_early_suspend_handler = {
    .level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
    .suspend = usb_pad_hub_early_suspend,
    .resume = usb_pad_hub_late_resume,
};
//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"

static struct regulator *hsusb_3p3;
static struct regulator *hsusb_1p8;
static struct regulator *hsusb_vddcx;
static struct regulator *vbus_otg;
static struct regulator *mhl_analog_switch;
static struct power_supply *psy;

static bool aca_id_turned_on;
static inline bool aca_enabled(void)
{
#ifdef CONFIG_USB_MSM_ACA
	return true;
#else
	return debug_aca_enabled;
#endif
}

static const int vdd_val[VDD_TYPE_MAX][VDD_VAL_MAX] = {
		{  /* VDD_CX CORNER Voting */
			[VDD_NONE]	= RPM_VREG_CORNER_NONE,
			[VDD_MIN]	= RPM_VREG_CORNER_NOMINAL,
			[VDD_MAX]	= RPM_VREG_CORNER_HIGH,
		},
		{ /* VDD_CX Voltage Voting */
			[VDD_NONE]	= USB_PHY_VDD_DIG_VOL_NONE,
			[VDD_MIN]	= USB_PHY_VDD_DIG_VOL_MIN,
			[VDD_MAX]	= USB_PHY_VDD_DIG_VOL_MAX,
		},
};

static int msm_hsusb_ldo_init(struct msm_otg *motg, int init)
{
	int rc = 0;

	if (init) {
		hsusb_3p3 = devm_regulator_get(motg->otg.dev, "HSUSB_3p3");
		if (IS_ERR(hsusb_3p3)) {
			dev_err(motg->otg.dev, "unable to get hsusb 3p3\n");
			return PTR_ERR(hsusb_3p3);
		}

		rc = regulator_set_voltage(hsusb_3p3, USB_PHY_3P3_VOL_MIN,
				USB_PHY_3P3_VOL_MAX);
		if (rc) {
			dev_err(motg->otg.dev, "unable to set voltage level for"
					"hsusb 3p3\n");
			return rc;
		}
		hsusb_1p8 = devm_regulator_get(motg->otg.dev, "HSUSB_1p8");
		if (IS_ERR(hsusb_1p8)) {
			dev_err(motg->otg.dev, "unable to get hsusb 1p8\n");
			rc = PTR_ERR(hsusb_1p8);
			goto put_3p3_lpm;
		}
		rc = regulator_set_voltage(hsusb_1p8, USB_PHY_1P8_VOL_MIN,
				USB_PHY_1P8_VOL_MAX);
		if (rc) {
			dev_err(motg->otg.dev, "unable to set voltage level for"
					"hsusb 1p8\n");
			goto put_1p8;
		}

		return 0;
	}

put_1p8:
	regulator_set_voltage(hsusb_1p8, 0, USB_PHY_1P8_VOL_MAX);
put_3p3_lpm:
	regulator_set_voltage(hsusb_3p3, 0, USB_PHY_3P3_VOL_MAX);
	return rc;
}

static int msm_hsusb_config_vddcx(int high)
{
	struct msm_otg *motg = the_msm_otg;
	enum usb_vdd_type vdd_type = motg->vdd_type;
	int max_vol = vdd_val[vdd_type][VDD_MAX];
	int min_vol;
	int ret;

	min_vol = vdd_val[vdd_type][!!high];
	ret = regulator_set_voltage(hsusb_vddcx, min_vol, max_vol);
	if (ret) {
		pr_err("%s: unable to set the voltage for regulator "
			"HSUSB_VDDCX\n", __func__);
		return ret;
	}

	pr_debug("%s: min_vol:%d max_vol:%d\n", __func__, min_vol, max_vol);

	return ret;
}

static int msm_hsusb_ldo_enable(struct msm_otg *motg, int on)
{
	int ret = 0;

	if (IS_ERR(hsusb_1p8)) {
		pr_err("%s: HSUSB_1p8 is not initialized\n", __func__);
		return -ENODEV;
	}

	if (IS_ERR(hsusb_3p3)) {
		pr_err("%s: HSUSB_3p3 is not initialized\n", __func__);
		return -ENODEV;
	}

	if (on) {
		ret = regulator_set_optimum_mode(hsusb_1p8,
				USB_PHY_1P8_HPM_LOAD);
		if (ret < 0) {
			pr_err("%s: Unable to set HPM of the regulator:"
				"HSUSB_1p8\n", __func__);
			return ret;
		}

		ret = regulator_enable(hsusb_1p8);
		if (ret) {
			dev_err(motg->otg.dev, "%s: unable to enable the hsusb 1p8\n",
				__func__);
			regulator_set_optimum_mode(hsusb_1p8, 0);
			return ret;
		}

		ret = regulator_set_optimum_mode(hsusb_3p3,
				USB_PHY_3P3_HPM_LOAD);
		if (ret < 0) {
			pr_err("%s: Unable to set HPM of the regulator:"
				"HSUSB_3p3\n", __func__);
			regulator_set_optimum_mode(hsusb_1p8, 0);
			regulator_disable(hsusb_1p8);
			return ret;
		}

		ret = regulator_enable(hsusb_3p3);
		if (ret) {
			dev_err(motg->otg.dev, "%s: unable to enable the hsusb 3p3\n",
				__func__);
			regulator_set_optimum_mode(hsusb_3p3, 0);
			regulator_set_optimum_mode(hsusb_1p8, 0);
			regulator_disable(hsusb_1p8);
			return ret;
		}

	} else {
		ret = regulator_disable(hsusb_1p8);
		if (ret) {
			dev_err(motg->otg.dev, "%s: unable to disable the hsusb 1p8\n",
				__func__);
			return ret;
		}

		ret = regulator_set_optimum_mode(hsusb_1p8, 0);
		if (ret < 0)
			pr_err("%s: Unable to set LPM of the regulator:"
				"HSUSB_1p8\n", __func__);

		ret = regulator_disable(hsusb_3p3);
		if (ret) {
			dev_err(motg->otg.dev, "%s: unable to disable the hsusb 3p3\n",
				 __func__);
			return ret;
		}
		ret = regulator_set_optimum_mode(hsusb_3p3, 0);
		if (ret < 0)
			pr_err("%s: Unable to set LPM of the regulator:"
				"HSUSB_3p3\n", __func__);
	}

	pr_debug("reg (%s)\n", on ? "HPM" : "LPM");
	return ret < 0 ? ret : 0;
}

static void msm_hsusb_mhl_switch_enable(struct msm_otg *motg, bool on)
{
	struct msm_otg_platform_data *pdata = motg->pdata;

	if (!pdata->mhl_enable)
		return;

	if (!mhl_analog_switch) {
		pr_err("%s: mhl_analog_switch is NULL.\n", __func__);
		return;
	}

	if (on) {
		if (regulator_enable(mhl_analog_switch))
			pr_err("unable to enable mhl_analog_switch\n");
	} else {
		regulator_disable(mhl_analog_switch);
	}
}

static int ulpi_read(struct otg_transceiver *otg, u32 reg)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	int cnt = 0;

	/* initiate read operation */
	writel(ULPI_RUN | ULPI_READ | ULPI_ADDR(reg),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while (cnt < ULPI_IO_TIMEOUT_USEC) {
		if (!(readl(USB_ULPI_VIEWPORT) & ULPI_RUN))
			break;
		udelay(1);
		cnt++;
	}

	if (cnt >= ULPI_IO_TIMEOUT_USEC) {
		dev_err(otg->dev, "ulpi_read: timeout %08x\n",
			readl(USB_ULPI_VIEWPORT));
		return -ETIMEDOUT;
	}
	return ULPI_DATA_READ(readl(USB_ULPI_VIEWPORT));
}

static int ulpi_write(struct otg_transceiver *otg, u32 val, u32 reg)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	int cnt = 0;

	/* initiate write operation */
	writel(ULPI_RUN | ULPI_WRITE |
	       ULPI_ADDR(reg) | ULPI_DATA(val),
	       USB_ULPI_VIEWPORT);

	/* wait for completion */
	while (cnt < ULPI_IO_TIMEOUT_USEC) {
		if (!(readl(USB_ULPI_VIEWPORT) & ULPI_RUN))
			break;
		udelay(1);
		cnt++;
	}

	if (cnt >= ULPI_IO_TIMEOUT_USEC) {
		dev_err(otg->dev, "ulpi_write: timeout\n");
		return -ETIMEDOUT;
	}
	return 0;
}

static struct otg_io_access_ops msm_otg_io_ops = {
	.read = ulpi_read,
	.write = ulpi_write,
};

static void ulpi_init(struct msm_otg *motg)
{
	struct msm_otg_platform_data *pdata = motg->pdata;
	int *seq = pdata->phy_init_seq;

	if (!seq)
		return;

//ASUS_BSP+++ "[USB][NA][Other] modify phy_init_seq for ER1"
	if((g_A68_hwID>=A68_ER1) && (g_A68_hwID<=A68_PR)){
		pdata->phy_init_seq[0]=0x3c;
		pdata->phy_init_seq[1]=0x81;
	}
//ASUS_BSP--- "[USB][NA][Other] modify phy_init_seq for ER1"

	while (seq[0] >= 0) {
		dev_vdbg(motg->otg.dev, "ulpi: write 0x%02x to 0x%02x\n",
				seq[0], seq[1]);
		ulpi_write(&motg->otg, seq[0], seq[1]);
		seq += 2;
	}
}

static int msm_otg_link_clk_reset(struct msm_otg *motg, bool assert)
{
	int ret;

	if (IS_ERR(motg->clk))
		return 0;

	if (assert) {
		ret = clk_reset(motg->clk, CLK_RESET_ASSERT);
		if (ret)
			dev_err(motg->otg.dev, "usb hs_clk assert failed\n");
	} else {
		ret = clk_reset(motg->clk, CLK_RESET_DEASSERT);
		if (ret)
			dev_err(motg->otg.dev, "usb hs_clk deassert failed\n");
	}
	return ret;
}

static int msm_otg_phy_clk_reset(struct msm_otg *motg)
{
	int ret;

	if (IS_ERR(motg->phy_reset_clk))
		return 0;

	ret = clk_reset(motg->phy_reset_clk, CLK_RESET_ASSERT);
	if (ret) {
		dev_err(motg->otg.dev, "usb phy clk assert failed\n");
		return ret;
	}
	usleep_range(10000, 12000);
	ret = clk_reset(motg->phy_reset_clk, CLK_RESET_DEASSERT);
	if (ret)
		dev_err(motg->otg.dev, "usb phy clk deassert failed\n");
	return ret;
}

static int msm_otg_phy_reset(struct msm_otg *motg)
{
	u32 val;
	int ret;
	int retries;

	ret = msm_otg_link_clk_reset(motg, 1);
	if (ret)
		return ret;
	ret = msm_otg_phy_clk_reset(motg);
	if (ret)
		return ret;
	ret = msm_otg_link_clk_reset(motg, 0);
	if (ret)
		return ret;

	val = readl(USB_PORTSC) & ~PORTSC_PTS_MASK;
	writel(val | PORTSC_PTS_ULPI, USB_PORTSC);

	for (retries = 3; retries > 0; retries--) {
		ret = ulpi_write(&motg->otg, ULPI_FUNC_CTRL_SUSPENDM,
				ULPI_CLR(ULPI_FUNC_CTRL));
		if (!ret)
			break;
		ret = msm_otg_phy_clk_reset(motg);
		if (ret)
			return ret;
	}
	if (!retries)
		return -ETIMEDOUT;

	/* This reset calibrates the phy, if the above write succeeded */
	ret = msm_otg_phy_clk_reset(motg);
	if (ret)
		return ret;

	for (retries = 3; retries > 0; retries--) {
		ret = ulpi_read(&motg->otg, ULPI_DEBUG);
		if (ret != -ETIMEDOUT)
			break;
		ret = msm_otg_phy_clk_reset(motg);
		if (ret)
			return ret;
	}
	if (!retries)
		return -ETIMEDOUT;

	dev_info(motg->otg.dev, "phy_reset: success\n");
	return 0;
}

#define LINK_RESET_TIMEOUT_USEC		(250 * 1000)
static int msm_otg_link_reset(struct msm_otg *motg)
{
	int cnt = 0;

	writel_relaxed(USBCMD_RESET, USB_USBCMD);
	while (cnt < LINK_RESET_TIMEOUT_USEC) {
		if (!(readl_relaxed(USB_USBCMD) & USBCMD_RESET))
			break;
		udelay(1);
		cnt++;
	}
	if (cnt >= LINK_RESET_TIMEOUT_USEC)
		return -ETIMEDOUT;

	/* select ULPI phy */
	writel_relaxed(0x80000000, USB_PORTSC);
	writel_relaxed(0x0, USB_AHBBURST);
	writel_relaxed(0x08, USB_AHBMODE);

	return 0;
}

static int msm_otg_reset(struct otg_transceiver *otg)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	struct msm_otg_platform_data *pdata = motg->pdata;
	int ret;
	u32 val = 0;
	u32 ulpi_val = 0;

	/*
	 * USB PHY and Link reset also reset the USB BAM.
	 * Thus perform reset operation only once to avoid
	 * USB BAM reset on other cases e.g. USB cable disconnections.
	 */
	if (pdata->disable_reset_on_disconnect) {
		if (motg->reset_counter)
			return 0;
		else
			motg->reset_counter++;
	}

	if (!IS_ERR(motg->clk))
		clk_prepare_enable(motg->clk);
	ret = msm_otg_phy_reset(motg);
	if (ret) {
		dev_err(otg->dev, "phy_reset failed\n");
		return ret;
	}

	aca_id_turned_on = false;
	ret = msm_otg_link_reset(motg);
	if (ret) {
		dev_err(otg->dev, "link reset failed\n");
		return ret;
	}
	msleep(100);

	ulpi_init(motg);

	/* Ensure that RESET operation is completed before turning off clock */
	mb();

	if (!IS_ERR(motg->clk))
		clk_disable_unprepare(motg->clk);

	if (pdata->otg_control == OTG_PHY_CONTROL) {
		val = readl_relaxed(USB_OTGSC);
		if (pdata->mode == USB_OTG) {
			ulpi_val = ULPI_INT_IDGRD | ULPI_INT_SESS_VALID;
			val |= OTGSC_IDIE | OTGSC_BSVIE;
		} else if (pdata->mode == USB_PERIPHERAL) {
			ulpi_val = ULPI_INT_SESS_VALID;
			val |= OTGSC_BSVIE;
		}
		writel_relaxed(val, USB_OTGSC);
		ulpi_write(otg, ulpi_val, ULPI_USB_INT_EN_RISE);
		ulpi_write(otg, ulpi_val, ULPI_USB_INT_EN_FALL);
	} else if (pdata->otg_control == OTG_PMIC_CONTROL) {
		ulpi_write(otg, OTG_COMP_DISABLE,
			ULPI_SET(ULPI_PWR_CLK_MNG_REG));
		//ASUS_BSP+++ BennyCheng "disable PMIC USB ID pull-up by default"
		pm8xxx_usb_id_pullup(0);
		//ASUS_BSP--- BennyCheng "disable PMIC USB ID pull-up by default"
	}

	return 0;
}

static const char *timer_string(int bit)
{
	switch (bit) {
	case A_WAIT_VRISE:		return "a_wait_vrise";
	case A_WAIT_VFALL:		return "a_wait_vfall";
	case B_SRP_FAIL:		return "b_srp_fail";
	case A_WAIT_BCON:		return "a_wait_bcon";
	case A_AIDL_BDIS:		return "a_aidl_bdis";
	case A_BIDL_ADIS:		return "a_bidl_adis";
	case B_ASE0_BRST:		return "b_ase0_brst";
	case A_TST_MAINT:		return "a_tst_maint";
	case B_TST_SRP:			return "b_tst_srp";
	case B_TST_CONFIG:		return "b_tst_config";
	default:			return "UNDEFINED";
	}
}

static enum hrtimer_restart msm_otg_timer_func(struct hrtimer *hrtimer)
{
	struct msm_otg *motg = container_of(hrtimer, struct msm_otg, timer);

	switch (motg->active_tmout) {
	case A_WAIT_VRISE:
		/* TODO: use vbus_vld interrupt */
		set_bit(A_VBUS_VLD, &motg->inputs);
		break;
	case A_TST_MAINT:
		/* OTG PET: End session after TA_TST_MAINT */
		set_bit(A_BUS_DROP, &motg->inputs);
		break;
	case B_TST_SRP:
		/*
		 * OTG PET: Initiate SRP after TB_TST_SRP of
		 * previous session end.
		 */
		set_bit(B_BUS_REQ, &motg->inputs);
		break;
	case B_TST_CONFIG:
		clear_bit(A_CONN, &motg->inputs);
		break;
	default:
		set_bit(motg->active_tmout, &motg->tmouts);
	}

	pr_debug("expired %s timer\n", timer_string(motg->active_tmout));
	queue_work(system_nrt_wq, &motg->sm_work);
	return HRTIMER_NORESTART;
}

static void msm_otg_del_timer(struct msm_otg *motg)
{
	int bit = motg->active_tmout;

	pr_debug("deleting %s timer. remaining %lld msec\n", timer_string(bit),
			div_s64(ktime_to_us(hrtimer_get_remaining(
					&motg->timer)), 1000));
	hrtimer_cancel(&motg->timer);
	clear_bit(bit, &motg->tmouts);
}

static void msm_otg_start_timer(struct msm_otg *motg, int time, int bit)
{
	time = 0;
	clear_bit(bit, &motg->tmouts);
	motg->active_tmout = bit;
	pr_debug("starting %s timer\n", timer_string(bit));
	hrtimer_start(&motg->timer,
			ktime_set(time / 1000, (time % 1000) * 1000000),
			HRTIMER_MODE_REL);
}

static void msm_otg_init_timer(struct msm_otg *motg)
{
	hrtimer_init(&motg->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	motg->timer.function = msm_otg_timer_func;
}

static int msm_otg_start_hnp(struct otg_transceiver *otg)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);

	if (otg->state != OTG_STATE_A_HOST) {
		pr_err("HNP can not be initiated in %s state\n",
				otg_state_string(otg->state));
		return -EINVAL;
	}

	pr_debug("A-Host: HNP initiated\n");
	clear_bit(A_BUS_REQ, &motg->inputs);
	queue_work(system_nrt_wq, &motg->sm_work);
	return 0;
}

static int msm_otg_start_srp(struct otg_transceiver *otg)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	u32 val;
	int ret = 0;

	if (otg->state != OTG_STATE_B_IDLE) {
		pr_err("SRP can not be initiated in %s state\n",
				otg_state_string(otg->state));
		ret = -EINVAL;
		goto out;
	}

	if ((jiffies - motg->b_last_se0_sess) < msecs_to_jiffies(TB_SRP_INIT)) {
		pr_debug("initial conditions of SRP are not met. Try again"
				"after some time\n");
		ret = -EAGAIN;
		goto out;
	}

	pr_debug("B-Device SRP started\n");

	/*
	 * PHY won't pull D+ high unless it detects Vbus valid.
	 * Since by definition, SRP is only done when Vbus is not valid,
	 * software work-around needs to be used to spoof the PHY into
	 * thinking it is valid. This can be done using the VBUSVLDEXTSEL and
	 * VBUSVLDEXT register bits.
	 */
	ulpi_write(otg, 0x03, 0x97);
	/*
	 * Harware auto assist data pulsing: Data pulse is given
	 * for 7msec; wait for vbus
	 */
	val = readl_relaxed(USB_OTGSC);
	writel_relaxed((val & ~OTGSC_INTSTS_MASK) | OTGSC_HADP, USB_OTGSC);

	/* VBUS plusing is obsoleted in OTG 2.0 supplement */
out:
	return ret;
}

static void msm_otg_host_hnp_enable(struct otg_transceiver *otg, bool enable)
{
	struct usb_hcd *hcd = bus_to_hcd(otg->host);
	struct usb_device *rhub = otg->host->root_hub;

	if (enable) {
		pm_runtime_disable(&rhub->dev);
		rhub->state = USB_STATE_NOTATTACHED;
		hcd->driver->bus_suspend(hcd);
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &hcd->flags);
	} else {
		usb_remove_hcd(hcd);
		msm_otg_reset(otg);
		usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
	}
}

static int msm_otg_set_suspend(struct otg_transceiver *otg, int suspend)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);

	if (aca_enabled())
		return 0;

	if (atomic_read(&motg->in_lpm) == suspend)
		return 0;

	if (suspend) {
		switch (otg->state) {
		case OTG_STATE_A_WAIT_BCON:
			if (TA_WAIT_BCON > 0)
				break;
			/* fall through */
		case OTG_STATE_A_HOST:
			pr_debug("host bus suspend\n");
			clear_bit(A_BUS_REQ, &motg->inputs);
			queue_work(system_nrt_wq, &motg->sm_work);
			break;
		case OTG_STATE_B_PERIPHERAL:
			pr_debug("peripheral bus suspend\n");
			if (!(motg->caps & ALLOW_LPM_ON_DEV_SUSPEND))
				break;
			set_bit(A_BUS_SUSPEND, &motg->inputs);
			queue_work(system_nrt_wq, &motg->sm_work);
			break;

		default:
			break;
		}
	} else {
		switch (otg->state) {
		case OTG_STATE_A_SUSPEND:
			/* Remote wakeup or resume */
			set_bit(A_BUS_REQ, &motg->inputs);
			otg->state = OTG_STATE_A_HOST;

			/* ensure hardware is not in low power mode */
			pm_runtime_resume(otg->dev);
			break;
		case OTG_STATE_B_PERIPHERAL:
			pr_debug("peripheral bus resume\n");
			if (!(motg->caps & ALLOW_LPM_ON_DEV_SUSPEND))
				break;
			clear_bit(A_BUS_SUSPEND, &motg->inputs);
			queue_work(system_nrt_wq, &motg->sm_work);
			break;
		default:
			break;
		}
	}
	return 0;
}

#define PHY_SUSPEND_TIMEOUT_USEC	(500 * 1000)
#define PHY_RESUME_TIMEOUT_USEC	(100 * 1000)

#ifdef CONFIG_PM_SLEEP
//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
static void msm_otg_start_host(struct otg_transceiver *otg, int on);
static void msm_otg_host_pm_suspend(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 phy_ctrl_val = 0;
	unsigned ret;

	ret = msm_xo_mode_vote(motg->xo_handle, MSM_XO_MODE_OFF);
	if (ret)
		dev_err(otg->dev, "%s failed to devote for "
			"TCXO D0 buffer%d\n", __func__, ret);

	if (device_may_wakeup(otg->dev)) {
		disable_irq_wake(motg->irq);
	}

	phy_ctrl_val = readl_relaxed(USB_PHY_CTRL);
	writel_relaxed(phy_ctrl_val & ~PHY_RETEN, USB_PHY_CTRL);

	msm_hsusb_ldo_enable(motg, 0);
	msm_hsusb_config_vddcx(0);
}

static void msm_otg_host_pm_resume(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 phy_ctrl_val = 0;
	unsigned ret;

	ret = msm_xo_mode_vote(motg->xo_handle, MSM_XO_MODE_ON);
	if (ret)
		dev_err(otg->dev, "%s failed to vote for "
			"TCXO D0 buffer%d\n", __func__, ret);

	msm_hsusb_ldo_enable(motg, 1);
	msm_hsusb_config_vddcx(1);

	phy_ctrl_val = readl_relaxed(USB_PHY_CTRL);
	phy_ctrl_val |= PHY_RETEN;
	writel_relaxed(phy_ctrl_val, USB_PHY_CTRL);

	if (device_may_wakeup(otg->dev)) {
		enable_irq_wake(motg->irq);
	}
}
//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"

static int msm_otg_suspend(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	struct usb_bus *bus = otg->host;
	struct msm_otg_platform_data *pdata = motg->pdata;
	int cnt = 0;
	bool host_bus_suspend, device_bus_suspend, dcp;
	u32 phy_ctrl_val = 0, cmd_val;
	unsigned ret;
	u32 portsc;

	if (atomic_read(&motg->in_lpm))
		return 0;

	//ASUS_BSP+++ BennyCheng "add mutex protection when pm/runtime suspend"
	mutex_lock(&msm_otg_mutex);
	//ASUS_BSP--- BennyCheng "add mutex protection when pm/runtime suspend"

	disable_irq(motg->irq);
	host_bus_suspend = otg->host && !test_bit(ID, &motg->inputs);
	device_bus_suspend = otg->gadget && test_bit(ID, &motg->inputs) &&
		test_bit(A_BUS_SUSPEND, &motg->inputs) &&
		motg->caps & ALLOW_LPM_ON_DEV_SUSPEND;
	dcp = motg->chg_type == USB_DCP_CHARGER;
	/*
	 * Chipidea 45-nm PHY suspend sequence:
	 *
	 * Interrupt Latch Register auto-clear feature is not present
	 * in all PHY versions. Latch register is clear on read type.
	 * Clear latch register to avoid spurious wakeup from
	 * low power mode (LPM).
	 *
	 * PHY comparators are disabled when PHY enters into low power
	 * mode (LPM). Keep PHY comparators ON in LPM only when we expect
	 * VBUS/Id notifications from USB PHY. Otherwise turn off USB
	 * PHY comparators. This save significant amount of power.
	 *
	 * PLL is not turned off when PHY enters into low power mode (LPM).
	 * Disable PLL for maximum power savings.
	 */

	if (motg->pdata->phy_type == CI_45NM_INTEGRATED_PHY) {
		ulpi_read(otg, 0x14);
		if (pdata->otg_control == OTG_PHY_CONTROL)
			ulpi_write(otg, 0x01, 0x30);
		ulpi_write(otg, 0x08, 0x09);
	}


	/* Set the PHCD bit, only if it is not set by the controller.
	 * PHY may take some time or even fail to enter into low power
	 * mode (LPM). Hence poll for 500 msec and reset the PHY and link
	 * in failure case.
	 */
	portsc = readl_relaxed(USB_PORTSC);
	if (!(portsc & PORTSC_PHCD)) {
		writel_relaxed(portsc | PORTSC_PHCD,
				USB_PORTSC);
		while (cnt < PHY_SUSPEND_TIMEOUT_USEC) {
			if (readl_relaxed(USB_PORTSC) & PORTSC_PHCD)
				break;
			udelay(1);
			cnt++;
		}
	}

	if (cnt >= PHY_SUSPEND_TIMEOUT_USEC) {
		dev_err(otg->dev, "Unable to suspend PHY\n");
		msm_otg_reset(otg);
		enable_irq(motg->irq);
		return -ETIMEDOUT;
	}

	/*
	 * PHY has capability to generate interrupt asynchronously in low
	 * power mode (LPM). This interrupt is level triggered. So USB IRQ
	 * line must be disabled till async interrupt enable bit is cleared
	 * in USBCMD register. Assert STP (ULPI interface STOP signal) to
	 * block data communication from PHY.
	 *
	 * PHY retention mode is disallowed while entering to LPM with wall
	 * charger connected.  But PHY is put into suspend mode. Hence
	 * enable asynchronous interrupt to detect charger disconnection when
	 * PMIC notifications are unavailable.
	 */
	cmd_val = readl_relaxed(USB_USBCMD);
	if (host_bus_suspend || device_bus_suspend ||
		(motg->pdata->otg_control == OTG_PHY_CONTROL && dcp))
		cmd_val |= ASYNC_INTR_CTRL | ULPI_STP_CTRL;
	else
		cmd_val |= ULPI_STP_CTRL;
	writel_relaxed(cmd_val, USB_USBCMD);

	/*
	 * BC1.2 spec mandates PD to enable VDP_SRC when charging from DCP.
	 * PHY retention and collapse can not happen with VDP_SRC enabled.
	 */
	if (motg->caps & ALLOW_PHY_RETENTION && !host_bus_suspend &&
		!device_bus_suspend && !dcp) {
		phy_ctrl_val = readl_relaxed(USB_PHY_CTRL);
		if (motg->pdata->otg_control == OTG_PHY_CONTROL)
			/* Enable PHY HV interrupts to wake MPM/Link */
			phy_ctrl_val |=
				(PHY_IDHV_INTEN | PHY_OTGSESSVLDHV_INTEN);

		writel_relaxed(phy_ctrl_val & ~PHY_RETEN, USB_PHY_CTRL);
		motg->lpm_flags |= PHY_RETENTIONED;
	}

	/* Ensure that above operation is completed before turning off clocks */
	mb();
	/* Consider clocks on workaround flag only in case of bus suspend */
	if (!(otg->state == OTG_STATE_B_PERIPHERAL &&
		test_bit(A_BUS_SUSPEND, &motg->inputs)) ||
	    !motg->pdata->core_clk_always_on_workaround) {
		clk_disable_unprepare(motg->pclk);
		clk_disable_unprepare(motg->core_clk);
		motg->lpm_flags |= CLOCKS_DOWN;
	}

	/* usb phy no more require TCXO clock, hence vote for TCXO disable */
	if (!host_bus_suspend) {
		ret = msm_xo_mode_vote(motg->xo_handle, MSM_XO_MODE_OFF);
		if (ret)
			dev_err(otg->dev, "%s failed to devote for "
				"TCXO D0 buffer%d\n", __func__, ret);
		else
			motg->lpm_flags |= XO_SHUTDOWN;
	}

	if (motg->caps & ALLOW_PHY_POWER_COLLAPSE &&
			!host_bus_suspend && !dcp) {
		msm_hsusb_ldo_enable(motg, 0);
		motg->lpm_flags |= PHY_PWR_COLLAPSED;
	}

	if (motg->lpm_flags & PHY_RETENTIONED) {
		msm_hsusb_config_vddcx(0);
		msm_hsusb_mhl_switch_enable(motg, 0);
	}

	if (device_may_wakeup(otg->dev)) {
		enable_irq_wake(motg->irq);
		if (motg->pdata->pmic_id_irq)
			enable_irq_wake(motg->pdata->pmic_id_irq);
		if (pdata->otg_control == OTG_PHY_CONTROL &&
			pdata->mpm_otgsessvld_int)
			msm_mpm_set_pin_wake(pdata->mpm_otgsessvld_int, 1);
	}
	if (bus)
		clear_bit(HCD_FLAG_HW_ACCESSIBLE, &(bus_to_hcd(bus))->flags);

	atomic_set(&motg->in_lpm, 1);
	enable_irq(motg->irq);
	wake_unlock(&motg->wlock);

	//ASUS_BSP+++ BennyCheng "prevent otg wakelock caused by vbus interrputs"
	if ((USB_HOST == motg->otg_mode || USB_AUTO == motg->otg_mode) &&
			AX_MicroP_IsP01Connected() && pad_exist()) {
		cancel_delayed_work(&msm_otg_suspend_check_work);
	}
	//ASUS_BSP--- BennyCheng "prevent otg wakelock caused by vbus interrputs"

	//ASUS_BSP+++ BennyCheng "add mutex protection when pm/runtime suspend"
	mutex_unlock(&msm_otg_mutex);
	//ASUS_BSP--- BennyCheng "add mutex protection when pm/runtime suspend"

	dev_info(otg->dev, "USB in low power mode\n");

	return 0;
}

static int msm_otg_resume(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	struct usb_bus *bus = otg->host;
	struct msm_otg_platform_data *pdata = motg->pdata;
	int cnt = 0;
	unsigned temp;
	u32 phy_ctrl_val = 0;
	unsigned ret;

	if (!atomic_read(&motg->in_lpm))
		return 0;

	//ASUS_BSP+++ BennyCheng "add mutex protection when pm/runtime suspend"
	mutex_lock(&msm_otg_mutex);
	//ASUS_BSP--- BennyCheng "add mutex protection when pm/runtime suspend"

	wake_lock(&motg->wlock);

	/* Vote for TCXO when waking up the phy */
	if (motg->lpm_flags & XO_SHUTDOWN) {
		ret = msm_xo_mode_vote(motg->xo_handle, MSM_XO_MODE_ON);
		if (ret)
			dev_err(otg->dev, "%s failed to vote for "
				"TCXO D0 buffer%d\n", __func__, ret);
		motg->lpm_flags &= ~XO_SHUTDOWN;
	}

	if (motg->lpm_flags & CLOCKS_DOWN) {
		clk_prepare_enable(motg->core_clk);
		clk_prepare_enable(motg->pclk);
		motg->lpm_flags &= ~CLOCKS_DOWN;
	}

	if (motg->lpm_flags & PHY_PWR_COLLAPSED) {
		msm_hsusb_ldo_enable(motg, 1);
		motg->lpm_flags &= ~PHY_PWR_COLLAPSED;
	}

	if (motg->lpm_flags & PHY_RETENTIONED) {
		msm_hsusb_mhl_switch_enable(motg, 1);
		msm_hsusb_config_vddcx(1);
		phy_ctrl_val = readl_relaxed(USB_PHY_CTRL);
		phy_ctrl_val |= PHY_RETEN;
		if (motg->pdata->otg_control == OTG_PHY_CONTROL)
			/* Disable PHY HV interrupts */
			phy_ctrl_val &=
				~(PHY_IDHV_INTEN | PHY_OTGSESSVLDHV_INTEN);
		writel_relaxed(phy_ctrl_val, USB_PHY_CTRL);
		motg->lpm_flags &= ~PHY_RETENTIONED;
	}

	temp = readl(USB_USBCMD);
	temp &= ~ASYNC_INTR_CTRL;
	temp &= ~ULPI_STP_CTRL;
	writel(temp, USB_USBCMD);

	/*
	 * PHY comes out of low power mode (LPM) in case of wakeup
	 * from asynchronous interrupt.
	 */
	if (!(readl(USB_PORTSC) & PORTSC_PHCD))
		goto skip_phy_resume;

	writel(readl(USB_PORTSC) & ~PORTSC_PHCD, USB_PORTSC);
	while (cnt < PHY_RESUME_TIMEOUT_USEC) {
		if (!(readl(USB_PORTSC) & PORTSC_PHCD))
			break;
		udelay(1);
		cnt++;
	}

	if (cnt >= PHY_RESUME_TIMEOUT_USEC) {
		/*
		 * This is a fatal error. Reset the link and
		 * PHY. USB state can not be restored. Re-insertion
		 * of USB cable is the only way to get USB working.
		 */
		dev_err(otg->dev, "Unable to resume USB."
				"Re-plugin the cable\n");
		msm_otg_reset(otg);
	}

skip_phy_resume:
	if (device_may_wakeup(otg->dev)) {
		disable_irq_wake(motg->irq);
		if (motg->pdata->pmic_id_irq)
			disable_irq_wake(motg->pdata->pmic_id_irq);
		if (pdata->otg_control == OTG_PHY_CONTROL &&
			pdata->mpm_otgsessvld_int)
			msm_mpm_set_pin_wake(pdata->mpm_otgsessvld_int, 0);
	}
	if (bus)
		set_bit(HCD_FLAG_HW_ACCESSIBLE, &(bus_to_hcd(bus))->flags);

	atomic_set(&motg->in_lpm, 0);

	if (motg->async_int) {
		motg->async_int = 0;
		enable_irq(motg->irq);
	}

	//ASUS_BSP+++ BennyCheng "prevent otg wakelock caused by vbus interrputs"
	if ((USB_HOST == motg->otg_mode || USB_AUTO == motg->otg_mode) &&
			AX_MicroP_IsP01Connected() && pad_exist()) {
		schedule_delayed_work(&msm_otg_suspend_check_work,
			msecs_to_jiffies(MSM_OTG_SUSPEND_CHECK_TIMEOUT));
	}
	//ASUS_BSP--- BennyCheng "prevent otg wakelock caused by vbus interrputs"

	//ASUS_BSP+++ BennyCheng "add mutex protection when pm/runtime suspend"
	mutex_unlock(&msm_otg_mutex);
	//ASUS_BSP--- BennyCheng "add mutex protection when pm/runtime suspend"

	dev_info(otg->dev, "USB exited from low power mode\n");

	return 0;
}
#endif

#ifndef CONFIG_BATTERY_ASUS
static int msm_otg_notify_chg_type(struct msm_otg *motg)
{
	static int charger_type;
	/*
	 * TODO
	 * Unify OTG driver charger types and power supply charger types
	 */
	if (charger_type == motg->chg_type)
		return 0;

	if (motg->chg_type == USB_SDP_CHARGER)
		charger_type = POWER_SUPPLY_TYPE_USB;
	else if (motg->chg_type == USB_CDP_CHARGER)
		charger_type = POWER_SUPPLY_TYPE_USB_CDP;
	else if (motg->chg_type == USB_DCP_CHARGER ||
			motg->chg_type == USB_PROPRIETARY_CHARGER)
		charger_type = POWER_SUPPLY_TYPE_USB_DCP;
	else if ((motg->chg_type == USB_ACA_DOCK_CHARGER ||
		motg->chg_type == USB_ACA_A_CHARGER ||
		motg->chg_type == USB_ACA_B_CHARGER ||
		motg->chg_type == USB_ACA_C_CHARGER))
		charger_type = POWER_SUPPLY_TYPE_USB_ACA;
	else
		charger_type = POWER_SUPPLY_TYPE_BATTERY;

	return pm8921_set_usb_power_supply_type(charger_type);
}
#endif

#ifndef CONFIG_CHARGER_ASUS
static int msm_otg_notify_power_supply(struct msm_otg *motg, unsigned mA)
{

	if (!psy)
		goto psy_not_supported;

	if (motg->cur_power == 0 && mA > 0) {
		/* Enable charging */
		if (power_supply_set_online(psy, true))
			goto psy_not_supported;
	} else if (motg->cur_power > 0 && mA == 0) {
		/* Disable charging */
		if (power_supply_set_online(psy, false))
			goto psy_not_supported;
		return 0;
	}
	/* Set max current limit */
	if (power_supply_set_current_limit(psy, 1000*mA))
		goto psy_not_supported;

	return 0;

psy_not_supported:
	dev_dbg(motg->otg.dev, "Power Supply doesn't support USB charger\n");
	return -ENXIO;
}
#endif
static void msm_otg_notify_charger(struct msm_otg *motg, unsigned mA)
{
	struct usb_gadget *g = motg->otg.gadget;

	if (g && g->is_a_peripheral)
		return;

	if ((motg->chg_type == USB_ACA_DOCK_CHARGER ||
		motg->chg_type == USB_ACA_A_CHARGER ||
		motg->chg_type == USB_ACA_B_CHARGER ||
		motg->chg_type == USB_ACA_C_CHARGER) &&
			mA > IDEV_ACA_CHG_LIMIT)
		mA = IDEV_ACA_CHG_LIMIT;
#ifndef CONFIG_BATTERY_ASUS
	if (msm_otg_notify_chg_type(motg))
		dev_err(motg->otg.dev,
			"Failed notifying %d charger type to PMIC\n",
							motg->chg_type);
#endif
	if (motg->cur_power == mA)
		return;
	//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
	#ifndef CONFIG_CHARGER_ASUS
	dev_info(motg->otg.dev, "Avail curr from USB = %u\n", mA);
	/*
	 *  Use Power Supply API if supported, otherwise fallback
	 *  to legacy pm8921 API.
	 */
	if (msm_otg_notify_power_supply(motg, mA))
		pm8921_charger_vbus_draw(mA);
	#endif
	//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"
	motg->cur_power = mA;
}

static int msm_otg_set_power(struct otg_transceiver *otg, unsigned mA)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);

	/*
	 * Gadget driver uses set_power method to notify about the
	 * available current based on suspend/configured states.
	 *
	 * IDEV_CHG can be drawn irrespective of suspend/un-configured
	 * states when CDP/ACA is connected.
	 */
	if (motg->chg_type == USB_SDP_CHARGER)
		msm_otg_notify_charger(motg, mA);

	return 0;
}

static void msm_otg_start_host(struct otg_transceiver *otg, int on)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	struct msm_otg_platform_data *pdata = motg->pdata;
	struct usb_hcd *hcd;

	if (!otg->host)
		return;

	hcd = bus_to_hcd(otg->host);

	if (on) {
		dev_dbg(otg->dev, "host on\n");

		if (pdata->otg_control == OTG_PHY_CONTROL)
			ulpi_write(otg, OTG_COMP_DISABLE,
				ULPI_SET(ULPI_PWR_CLK_MNG_REG));

		/*
		 * Some boards have a switch cotrolled by gpio
		 * to enable/disable internal HUB. Enable internal
		 * HUB before kicking the host.
		 */
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_A_HOST);
		usb_add_hcd(hcd, hcd->irq, IRQF_SHARED);
	} else {
		dev_dbg(otg->dev, "host off\n");

		usb_remove_hcd(hcd);
		/* HCD core reset all bits of PORTSC. select ULPI phy */
		writel_relaxed(0x80000000, USB_PORTSC);

		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_UNDEFINED);

		if (pdata->otg_control == OTG_PHY_CONTROL)
			ulpi_write(otg, OTG_COMP_DISABLE,
				ULPI_CLR(ULPI_PWR_CLK_MNG_REG));
	}
}

static int msm_otg_usbdev_notify(struct notifier_block *self,
			unsigned long action, void *priv)
{
	struct msm_otg *motg = container_of(self, struct msm_otg, usbdev_nb);
	struct otg_transceiver *otg = &motg->otg;
	struct usb_device *udev = priv;

	if (action == USB_BUS_ADD || action == USB_BUS_REMOVE)
		goto out;

	if (udev->bus != motg->otg.host)
		goto out;
	/*
	 * Interested in devices connected directly to the root hub.
	 * ACA dock can supply IDEV_CHG irrespective devices connected
	 * on the accessory port.
	 */
	if (!udev->parent || udev->parent->parent ||
			motg->chg_type == USB_ACA_DOCK_CHARGER)
		goto out;

	switch (action) {
	case USB_DEVICE_ADD:
		if (aca_enabled())
			usb_disable_autosuspend(udev);
		if (otg->state == OTG_STATE_A_WAIT_BCON) {
			pr_debug("B_CONN set\n");
			set_bit(B_CONN, &motg->inputs);
			msm_otg_del_timer(motg);
			otg->state = OTG_STATE_A_HOST;
			/*
			 * OTG PET: A-device must end session within
			 * 10 sec after PET enumeration.
			 */
			if (udev->quirks & USB_QUIRK_OTG_PET)
				msm_otg_start_timer(motg, TA_TST_MAINT,
						A_TST_MAINT);
		}
		/* fall through */
	case USB_DEVICE_CONFIG:
		if (udev->actconfig)
			motg->mA_port = udev->actconfig->desc.bMaxPower * 2;
		else
			motg->mA_port = IUNIT;
		if (otg->state == OTG_STATE_B_HOST)
			msm_otg_del_timer(motg);
		break;
	case USB_DEVICE_REMOVE:
		if ((otg->state == OTG_STATE_A_HOST) ||
			(otg->state == OTG_STATE_A_SUSPEND)) {
			pr_debug("B_CONN clear\n");
			clear_bit(B_CONN, &motg->inputs);
			/*
			 * OTG PET: A-device must end session after
			 * PET disconnection if it is enumerated
			 * with bcdDevice[0] = 1. USB core sets
			 * bus->otg_vbus_off for us. clear it here.
			 */
			if (udev->bus->otg_vbus_off) {
				udev->bus->otg_vbus_off = 0;
				set_bit(A_BUS_DROP, &motg->inputs);
			}
			queue_work(system_nrt_wq, &motg->sm_work);
		}
	default:
		break;
	}
	if (test_bit(ID_A, &motg->inputs))
		msm_otg_notify_charger(motg, IDEV_ACA_CHG_MAX -
				motg->mA_port);
out:
	return NOTIFY_OK;
}

static void msm_hsusb_vbus_power(struct msm_otg *motg, bool on)
{
	int ret;
	static bool vbus_is_on;

	//ASUS_BSP+++ BennyCheng "remove vbus power control for usb host"
	/*
	 * A68 does not support USB host mode, so it is not required to request power from PMIC.
	 * USB host mode only supports when A68 is plugged to a Pad or a Pad with a dock. For these cases,
	 * Vbus power is provided by external power source.
	 */
	return;
	//ASUS_BSP--- BennyCheng "remove vbus power control for usb host"

	if (vbus_is_on == on)
		return;

	if (motg->pdata->vbus_power) {
		ret = motg->pdata->vbus_power(on);
		if (!ret)
			vbus_is_on = on;
		return;
	}

	if (!vbus_otg) {
		pr_err("vbus_otg is NULL.");
		return;
	}

	/*
	 * if entering host mode tell the charger to not draw any current
	 * from usb before turning on the boost.
	 * if exiting host mode disable the boost before enabling to draw
	 * current from the source.
	 */
	if (on) {
		pm8921_disable_source_current(on);
		ret = regulator_enable(vbus_otg);
		if (ret) {
			pr_err("unable to enable vbus_otg\n");
			return;
		}
		vbus_is_on = true;
	} else {
		ret = regulator_disable(vbus_otg);
		if (ret) {
			pr_err("unable to disable vbus_otg\n");
			return;
		}
		pm8921_disable_source_current(on);
		vbus_is_on = false;
	}
}

static int msm_otg_set_host(struct otg_transceiver *otg, struct usb_bus *host)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	struct usb_hcd *hcd;

	/*
	 * Fail host registration if this board can support
	 * only peripheral configuration.
	 */
	if (motg->pdata->mode == USB_PERIPHERAL) {
		dev_info(otg->dev, "Host mode is not supported\n");
		return -ENODEV;
	}

	//ASUS_BSP+++ BennyCheng "remove vbus power control for usb host"
	/*
	 * A68 does not support USB host mode, so it is not required to request power from PMIC.
	 * USB host mode only supports when A68 is plugged to a Pad or a Pad with a dock. For these cases,
	 * Vbus power is provided by external power source.
	if (!motg->pdata->vbus_power && host) {
		vbus_otg = devm_regulator_get(motg->otg.dev, "vbus_otg");
		if (IS_ERR(vbus_otg)) {
			pr_err("Unable to get vbus_otg\n");
			return -ENODEV;
		}
	}
	*/
	//ASUS_BSP--- BennyCheng "remove vbus power control for usb host"

	if (!host) {
		if (otg->state == OTG_STATE_A_HOST) {
			pm_runtime_get_sync(otg->dev);
			usb_unregister_notify(&motg->usbdev_nb);
			msm_otg_start_host(otg, 0);
			msm_hsusb_vbus_power(motg, 0);
			otg->host = NULL;
			otg->state = OTG_STATE_UNDEFINED;
			queue_work(system_nrt_wq, &motg->sm_work);
		} else {
			otg->host = NULL;
		}

		return 0;
	}

	hcd = bus_to_hcd(host);
	hcd->power_budget = motg->pdata->power_budget;

#ifdef CONFIG_USB_OTG
	host->otg_port = 1;
#endif
	motg->usbdev_nb.notifier_call = msm_otg_usbdev_notify;
	usb_register_notify(&motg->usbdev_nb);
	otg->host = host;
	dev_dbg(otg->dev, "host driver registered w/ tranceiver\n");

	/*
	 * Kick the state machine work, if peripheral is not supported
	 * or peripheral is already registered with us.
	 */
	if (motg->pdata->mode == USB_HOST || otg->gadget) {
		pm_runtime_get_sync(otg->dev);
		queue_work(system_nrt_wq, &motg->sm_work);
	}

	return 0;
}

static void msm_otg_start_peripheral(struct otg_transceiver *otg, int on)
{
	int ret;
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);
	struct msm_otg_platform_data *pdata = motg->pdata;

	if (!otg->gadget)
		return;

	if (on) {
		dev_dbg(otg->dev, "gadget on\n");
		/*
		 * Some boards have a switch cotrolled by gpio
		 * to enable/disable internal HUB. Disable internal
		 * HUB before kicking the gadget.
		 */
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_B_PERIPHERAL);

		/* Configure BUS performance parameters for MAX bandwidth */
		if (motg->bus_perf_client && debug_bus_voting_enabled) {
			ret = msm_bus_scale_client_update_request(
					motg->bus_perf_client, 1);
			if (ret)
				dev_err(motg->otg.dev, "%s: Failed to vote for "
					   "bus bandwidth %d\n", __func__, ret);
		}
		usb_gadget_vbus_connect(otg->gadget);
	} else {
		dev_dbg(otg->dev, "gadget off\n");
		usb_gadget_vbus_disconnect(otg->gadget);
		/* Configure BUS performance parameters to default */
		if (motg->bus_perf_client) {
			ret = msm_bus_scale_client_update_request(
					motg->bus_perf_client, 0);
			if (ret)
				dev_err(motg->otg.dev, "%s: Failed to devote "
					   "for bus bw %d\n", __func__, ret);
		}
		if (pdata->setup_gpio)
			pdata->setup_gpio(OTG_STATE_UNDEFINED);
	}

}

static int msm_otg_set_peripheral(struct otg_transceiver *otg,
			struct usb_gadget *gadget)
{
	struct msm_otg *motg = container_of(otg, struct msm_otg, otg);

	/*
	 * Fail peripheral registration if this board can support
	 * only host configuration.
	 */
	if (motg->pdata->mode == USB_HOST) {
		dev_info(otg->dev, "Peripheral mode is not supported\n");
		return -ENODEV;
	}

	if (!gadget) {
		if (otg->state == OTG_STATE_B_PERIPHERAL) {
			pm_runtime_get_sync(otg->dev);
			msm_otg_start_peripheral(otg, 0);
			otg->gadget = NULL;
			otg->state = OTG_STATE_UNDEFINED;
			queue_work(system_nrt_wq, &motg->sm_work);
		} else {
			otg->gadget = NULL;
		}

		return 0;
	}
	otg->gadget = gadget;
	dev_dbg(otg->dev, "peripheral driver registered w/ tranceiver\n");

	/*
	 * Kick the state machine work, if host is not supported
	 * or host is already registered with us.
	 */
	if (motg->pdata->mode == USB_PERIPHERAL || otg->host) {
		pm_runtime_get_sync(otg->dev);
		queue_work(system_nrt_wq, &motg->sm_work);
	}

	return 0;
}

static bool msm_chg_aca_detect(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 int_sts;
	bool ret = false;

	if (!aca_enabled())
		goto out;

	if (motg->pdata->phy_type == CI_45NM_INTEGRATED_PHY)
		goto out;

	int_sts = ulpi_read(otg, 0x87);
	switch (int_sts & 0x1C) {
	case 0x08:
		if (!test_and_set_bit(ID_A, &motg->inputs)) {
			dev_dbg(otg->dev, "ID_A\n");
			motg->chg_type = USB_ACA_A_CHARGER;
			motg->chg_state = USB_CHG_STATE_DETECTED;
			clear_bit(ID_B, &motg->inputs);
			clear_bit(ID_C, &motg->inputs);
			set_bit(ID, &motg->inputs);
			ret = true;
		}
		break;
	case 0x0C:
		if (!test_and_set_bit(ID_B, &motg->inputs)) {
			dev_dbg(otg->dev, "ID_B\n");
			motg->chg_type = USB_ACA_B_CHARGER;
			motg->chg_state = USB_CHG_STATE_DETECTED;
			clear_bit(ID_A, &motg->inputs);
			clear_bit(ID_C, &motg->inputs);
			set_bit(ID, &motg->inputs);
			ret = true;
		}
		break;
	case 0x10:
		if (!test_and_set_bit(ID_C, &motg->inputs)) {
			dev_dbg(otg->dev, "ID_C\n");
			motg->chg_type = USB_ACA_C_CHARGER;
			motg->chg_state = USB_CHG_STATE_DETECTED;
			clear_bit(ID_A, &motg->inputs);
			clear_bit(ID_B, &motg->inputs);
			set_bit(ID, &motg->inputs);
			ret = true;
		}
		break;
	case 0x04:
		if (test_and_clear_bit(ID, &motg->inputs)) {
			dev_dbg(otg->dev, "ID_GND\n");
			motg->chg_type = USB_INVALID_CHARGER;
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			clear_bit(ID_A, &motg->inputs);
			clear_bit(ID_B, &motg->inputs);
			clear_bit(ID_C, &motg->inputs);
			ret = true;
		}
		break;
	default:
		ret = test_and_clear_bit(ID_A, &motg->inputs) |
			test_and_clear_bit(ID_B, &motg->inputs) |
			test_and_clear_bit(ID_C, &motg->inputs) |
			!test_and_set_bit(ID, &motg->inputs);
		if (ret) {
			dev_dbg(otg->dev, "ID A/B/C/GND is no more\n");
			motg->chg_type = USB_INVALID_CHARGER;
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
		}
	}
out:
	return ret;
}

static void msm_chg_enable_aca_det(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;

	if (!aca_enabled())
		return;

	switch (motg->pdata->phy_type) {
	case SNPS_28NM_INTEGRATED_PHY:
		/* Disable ID_GND in link and PHY */
		writel_relaxed(readl_relaxed(USB_OTGSC) & ~(OTGSC_IDPU |
				OTGSC_IDIE), USB_OTGSC);
		ulpi_write(otg, 0x01, 0x0C);
		ulpi_write(otg, 0x10, 0x0F);
		ulpi_write(otg, 0x10, 0x12);
		/* Disable PMIC ID pull-up */
		pm8xxx_usb_id_pullup(0);
		/* Enable ACA ID detection */
		ulpi_write(otg, 0x20, 0x85);
		aca_id_turned_on = true;
		break;
	default:
		break;
	}
}

static void msm_chg_enable_aca_intr(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;

	if (!aca_enabled())
		return;

	switch (motg->pdata->phy_type) {
	case SNPS_28NM_INTEGRATED_PHY:
		/* Enable ACA Detection interrupt (on any RID change) */
		ulpi_write(otg, 0x01, 0x94);
		break;
	default:
		break;
	}
}

static void msm_chg_disable_aca_intr(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;

	if (!aca_enabled())
		return;

	switch (motg->pdata->phy_type) {
	case SNPS_28NM_INTEGRATED_PHY:
		ulpi_write(otg, 0x01, 0x95);
		break;
	default:
		break;
	}
}

static bool msm_chg_check_aca_intr(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	bool ret = false;

	if (!aca_enabled())
		return ret;

	switch (motg->pdata->phy_type) {
	case SNPS_28NM_INTEGRATED_PHY:
		if (ulpi_read(otg, 0x91) & 1) {
			dev_dbg(otg->dev, "RID change\n");
			ulpi_write(otg, 0x01, 0x92);
			ret = msm_chg_aca_detect(motg);
		}
	default:
		break;
	}
	return ret;
}

static void msm_otg_id_timer_func(unsigned long data)
{
	struct msm_otg *motg = (struct msm_otg *) data;
	struct otg_transceiver *otg = &motg->otg;

	if (!aca_enabled())
		return;

	if (atomic_read(&motg->in_lpm)) {
		dev_dbg(motg->otg.dev, "timer: in lpm\n");
		return;
	}

	if (otg->state == OTG_STATE_A_SUSPEND)
		goto out;

	if (msm_chg_check_aca_intr(motg)) {
		dev_dbg(motg->otg.dev, "timer: aca work\n");
		queue_work(system_nrt_wq, &motg->sm_work);
	}

out:
	if (!test_bit(ID, &motg->inputs) || test_bit(ID_A, &motg->inputs))
		mod_timer(&motg->id_timer, ID_TIMER_FREQ);
}

static bool msm_chg_check_secondary_det(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		ret = chg_det & (1 << 4);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x87);
		ret = chg_det & 1;
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_enable_secondary_det(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		/* Turn off charger block */
		chg_det |= ~(1 << 1);
		ulpi_write(otg, chg_det, 0x34);
		udelay(20);
		/* control chg block via ULPI */
		chg_det &= ~(1 << 3);
		ulpi_write(otg, chg_det, 0x34);
		/* put it in host mode for enabling D- source */
		chg_det &= ~(1 << 2);
		ulpi_write(otg, chg_det, 0x34);
		/* Turn on chg detect block */
		chg_det &= ~(1 << 1);
		ulpi_write(otg, chg_det, 0x34);
		udelay(20);
		/* enable chg detection */
		chg_det &= ~(1 << 0);
		ulpi_write(otg, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/*
		 * Configure DM as current source, DP as current sink
		 * and enable battery charging comparators.
		 */
		ulpi_write(otg, 0x8, 0x85);
		ulpi_write(otg, 0x2, 0x85);
		ulpi_write(otg, 0x1, 0x85);
		break;
	default:
		break;
	}
}

static bool msm_chg_check_primary_det(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		ret = chg_det & (1 << 4);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x87);
		ret = chg_det & 1;
		/* Turn off VDP_SRC */
		ulpi_write(otg, 0x3, 0x86);
		msleep(20);
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_enable_primary_det(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		/* enable chg detection */
		chg_det &= ~(1 << 0);
		ulpi_write(otg, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/*
		 * Configure DP as current source, DM as current sink
		 * and enable battery charging comparators.
		 */
		ulpi_write(otg, 0x2, 0x85);
		ulpi_write(otg, 0x1, 0x85);
		break;
	default:
		break;
	}
}

static bool msm_chg_check_dcd(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 line_state;
	bool ret = false;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		line_state = ulpi_read(otg, 0x15);
		ret = !(line_state & 1);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		line_state = ulpi_read(otg, 0x87);
		ret = line_state & 2;
		break;
	default:
		break;
	}
	return ret;
}

static void msm_chg_disable_dcd(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		chg_det &= ~(1 << 5);
		ulpi_write(otg, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		ulpi_write(otg, 0x10, 0x86);
		break;
	default:
		break;
	}
}

static void msm_chg_enable_dcd(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		/* Turn on D+ current source */
		chg_det |= (1 << 5);
		ulpi_write(otg, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* Data contact detection enable */
		ulpi_write(otg, 0x10, 0x85);
		break;
	default:
		break;
	}
}

static void msm_chg_block_on(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 func_ctrl, chg_det;

	/* put the controller in non-driving mode */
	func_ctrl = ulpi_read(otg, ULPI_FUNC_CTRL);
	func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
	func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NONDRIVING;
	ulpi_write(otg, func_ctrl, ULPI_FUNC_CTRL);

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		/* control chg block via ULPI */
		chg_det &= ~(1 << 3);
		ulpi_write(otg, chg_det, 0x34);
		/* Turn on chg detect block */
		chg_det &= ~(1 << 1);
		ulpi_write(otg, chg_det, 0x34);
		udelay(20);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* Clear charger detecting control bits */
		ulpi_write(otg, 0x1F, 0x86);
		/* Clear alt interrupt latch and enable bits */
		ulpi_write(otg, 0x1F, 0x92);
		ulpi_write(otg, 0x1F, 0x95);
		udelay(100);
		break;
	default:
		break;
	}
}

static void msm_chg_block_off(struct msm_otg *motg)
{
	struct otg_transceiver *otg = &motg->otg;
	u32 func_ctrl, chg_det;

	switch (motg->pdata->phy_type) {
	case CI_45NM_INTEGRATED_PHY:
		chg_det = ulpi_read(otg, 0x34);
		/* Turn off charger block */
		chg_det |= ~(1 << 1);
		ulpi_write(otg, chg_det, 0x34);
		break;
	case SNPS_28NM_INTEGRATED_PHY:
		/* Clear charger detecting control bits */
		ulpi_write(otg, 0x3F, 0x86);
		/* Clear alt interrupt latch and enable bits */
		ulpi_write(otg, 0x1F, 0x92);
		ulpi_write(otg, 0x1F, 0x95);
		break;
	default:
		break;
	}

	/* put the controller in normal mode */
	func_ctrl = ulpi_read(otg, ULPI_FUNC_CTRL);
	func_ctrl &= ~ULPI_FUNC_CTRL_OPMODE_MASK;
	func_ctrl |= ULPI_FUNC_CTRL_OPMODE_NORMAL;
	ulpi_write(otg, func_ctrl, ULPI_FUNC_CTRL);
}

static const char *chg_to_string(enum usb_chg_type chg_type)
{
	switch (chg_type) {
	case USB_SDP_CHARGER:		return "USB_SDP_CHARGER";
	case USB_DCP_CHARGER:		return "USB_DCP_CHARGER";
	case USB_CDP_CHARGER:		return "USB_CDP_CHARGER";
	case USB_ACA_A_CHARGER:		return "USB_ACA_A_CHARGER";
	case USB_ACA_B_CHARGER:		return "USB_ACA_B_CHARGER";
	case USB_ACA_C_CHARGER:		return "USB_ACA_C_CHARGER";
	case USB_ACA_DOCK_CHARGER:	return "USB_ACA_DOCK_CHARGER";
	case USB_PROPRIETARY_CHARGER:	return "USB_PROPRIETARY_CHARGER";
	default:			return "INVALID_CHARGER";
	}
}

#define MSM_CHG_DCD_POLL_TIME		(100 * HZ/1000) /* 100 msec */
#define MSM_CHG_DCD_MAX_RETRIES		6 /* Tdcd_tmout = 6 * 100 msec */
#define MSM_CHG_PRIMARY_DET_TIME	(50 * HZ/1000) /* TVDPSRC_ON */
#define MSM_CHG_SECONDARY_DET_TIME	(50 * HZ/1000) /* TVDMSRC_ON */

//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
static void asus_usb_detect_work(struct work_struct *w)
{
	cancel_delayed_work_sync(&asus_chg_work);
	g_charger_mode = ASUS_CHG_SRC_USB;
	if(!g_host_mode){
		asus_chg_set_chg_mode(ASUS_CHG_SRC_USB);
		//ASUS_BSP+++ "[USB][NA][Other] Add USB event log"
		ASUSEvtlog("[USB] set_chg_mode: USB\n");
		//ASUS_BSP--- "[USB][NA][Other] Add USB event log"
		printk("[USB] set_chg_mode: USB\n");
	}else{
		ASUSEvtlog("[USB] InPad not notify set_chg_mode: USB\n");
		printk("[USB] InPad not notify set_chg_mode: USB\n");
	}
}
static void asus_chg_detect_work(struct work_struct *w)
{
	if(g_usb_boot == MSM_OTG_USB_BOOT_DOWN){
		g_charger_mode = ASUS_CHG_SRC_UNKNOWN;
		if(!g_host_mode){
			asus_chg_set_chg_mode(ASUS_CHG_SRC_UNKNOWN);
			//ASUS_BSP+++ "[USB][NA][Other] Add USB event log"
			ASUSEvtlog("[USB] set_chg_mode: UNKNOWN\n");
			//ASUS_BSP--- "[USB][NA][Other] Add USB event log"
			printk("[USB] set_chg_mode: UNKNOWN\n");
		}else{
			ASUSEvtlog("[USB] InPad not notify set_chg_mode: UNKNOWN\n");
			printk("[USB] InPad not notify set_chg_mode: UNKNOWN\n");
		}
	}else{
		if(g_usb_boot == MSM_OTG_USB_BOOT_IRQ){
			g_usb_boot = MSM_OTG_USB_BOOT_DOWN;
		}
		schedule_delayed_work(&asus_chg_work, (2000 * HZ/1000));
	}
}
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"

static void msm_chg_detect_work(struct work_struct *w)
{
	struct msm_otg *motg = container_of(w, struct msm_otg, chg_work.work);
	struct otg_transceiver *otg = &motg->otg;
	bool is_dcd = false, tmout, vout, is_aca;
	u32 line_state, dm_vlgc;
	unsigned long delay;

	dev_dbg(otg->dev, "chg detection work\n");
	switch (motg->chg_state) {
	case USB_CHG_STATE_UNDEFINED:
		msm_chg_block_on(motg);
		if (motg->pdata->enable_dcd)
			msm_chg_enable_dcd(motg);
		msm_chg_enable_aca_det(motg);
		motg->chg_state = USB_CHG_STATE_WAIT_FOR_DCD;
		motg->dcd_retries = 0;
		delay = MSM_CHG_DCD_POLL_TIME;
		break;
	case USB_CHG_STATE_WAIT_FOR_DCD:
		is_aca = msm_chg_aca_detect(motg);
		if (is_aca) {
			/*
			 * ID_A can be ACA dock too. continue
			 * primary detection after DCD.
			 */
			if (test_bit(ID_A, &motg->inputs)) {
				motg->chg_state = USB_CHG_STATE_WAIT_FOR_DCD;
			} else {
				delay = 0;
				break;
			}
		}
		if (motg->pdata->enable_dcd)
			is_dcd = msm_chg_check_dcd(motg);
		tmout = ++motg->dcd_retries == MSM_CHG_DCD_MAX_RETRIES;
		if (is_dcd || tmout) {
			if (motg->pdata->enable_dcd)
				msm_chg_disable_dcd(motg);
			msm_chg_enable_primary_det(motg);
			delay = MSM_CHG_PRIMARY_DET_TIME;
			motg->chg_state = USB_CHG_STATE_DCD_DONE;
		} else {
			delay = MSM_CHG_DCD_POLL_TIME;
		}
		break;
	case USB_CHG_STATE_DCD_DONE:
		vout = msm_chg_check_primary_det(motg);
		line_state = readl_relaxed(USB_PORTSC) & PORTSC_LS;
		dm_vlgc = line_state & PORTSC_LS_DM;
		if (vout && !dm_vlgc) { /* VDAT_REF < DM < VLGC */
			if (test_bit(ID_A, &motg->inputs)) {
				motg->chg_type = USB_ACA_DOCK_CHARGER;
				motg->chg_state = USB_CHG_STATE_DETECTED;
				delay = 0;
				break;
			}
			if (line_state) { /* DP > VLGC */
				motg->chg_type = USB_PROPRIETARY_CHARGER;
				motg->chg_state = USB_CHG_STATE_DETECTED;
				delay = 0;
			} else {
				msm_chg_enable_secondary_det(motg);
				delay = MSM_CHG_SECONDARY_DET_TIME;
				motg->chg_state = USB_CHG_STATE_PRIMARY_DONE;
			}
		} else { /* DM < VDAT_REF || DM > VLGC */
			if (test_bit(ID_A, &motg->inputs)) {
				motg->chg_type = USB_ACA_A_CHARGER;
				motg->chg_state = USB_CHG_STATE_DETECTED;
				delay = 0;
				break;
			}

			if (line_state) /* DP > VLGC OR/AND DM > VLGC */
				motg->chg_type = USB_PROPRIETARY_CHARGER;
			else
				motg->chg_type = USB_SDP_CHARGER;

			motg->chg_state = USB_CHG_STATE_DETECTED;
			delay = 0;
		}
		break;
	case USB_CHG_STATE_PRIMARY_DONE:
		vout = msm_chg_check_secondary_det(motg);
		if (vout)
			motg->chg_type = USB_DCP_CHARGER;
		else
			motg->chg_type = USB_CDP_CHARGER;
		motg->chg_state = USB_CHG_STATE_SECONDARY_DONE;
		/* fall through */
	case USB_CHG_STATE_SECONDARY_DONE:
		motg->chg_state = USB_CHG_STATE_DETECTED;
	case USB_CHG_STATE_DETECTED:
		msm_chg_block_off(motg);
		msm_chg_enable_aca_det(motg);
		/*
		 * Spurious interrupt is seen after enabling ACA detection
		 * due to which charger detection fails in case of PET.
		 * Add delay of 100 microsec to avoid that.
		 */
		udelay(100);
		msm_chg_enable_aca_intr(motg);
		dev_dbg(otg->dev, "chg_type = %s\n",
			chg_to_string(motg->chg_type));
		queue_work(system_nrt_wq, &motg->sm_work);
		//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
		#ifdef CONFIG_CHARGER_ASUS
		if(motg->chg_type != USB_SDP_CHARGER){
			if(!g_host_mode){
				asus_chg_set_chg_mode(ASUS_CHG_SRC_DC);
				//ASUS_BSP+++ "[USB][NA][Other] Add USB event log"
				ASUSEvtlog("[USB] set_chg_mode: ASUS AC\n");
				//ASUS_BSP--- "[USB][NA][Other] Add USB event log"
				printk("[USB] set_chg_mode: ASUS AC\n");
			}else{
				ASUSEvtlog("[USB] InPad not notify set_chg_mode: ASUS AC\n");
				printk("[USB] InPad not notify set_chg_mode: ASUS AC\n");
			}
		}
		else{
			if(g_usb_boot == MSM_OTG_USB_BOOT_IRQ){
				g_usb_boot = MSM_OTG_USB_BOOT_DOWN;
			}
			//wait 2 sec to check non-asus charger
			schedule_delayed_work(&asus_chg_work, (2000 * HZ/1000));
		}
		#endif
		//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"
		return;
	default:
		return;
	}

	queue_delayed_work(system_nrt_wq, &motg->chg_work, delay);
}

/*
 * We support OTG, Peripheral only and Host only configurations. In case
 * of OTG, mode switch (host-->peripheral/peripheral-->host) can happen
 * via Id pin status or user request (debugfs). Id/BSV interrupts are not
 * enabled when switch is controlled by user and default mode is supplied
 * by board file, which can be changed by userspace later.
 */
static void msm_otg_init_sm(struct msm_otg *motg)
{
	struct msm_otg_platform_data *pdata = motg->pdata;
	u32 otgsc = readl(USB_OTGSC);

	switch (pdata->mode) {
	case USB_OTG:
		if (pdata->otg_control == OTG_USER_CONTROL) {
			if (pdata->default_mode == USB_HOST) {
				clear_bit(ID, &motg->inputs);
			} else if (pdata->default_mode == USB_PERIPHERAL) {
				set_bit(ID, &motg->inputs);
				set_bit(B_SESS_VLD, &motg->inputs);
			} else {
				set_bit(ID, &motg->inputs);
				clear_bit(B_SESS_VLD, &motg->inputs);
			}
		} else if (pdata->otg_control == OTG_PHY_CONTROL) {
			if (otgsc & OTGSC_ID) {
				set_bit(ID, &motg->inputs);
			} else {
				clear_bit(ID, &motg->inputs);
				set_bit(A_BUS_REQ, &motg->inputs);
			}
			if (otgsc & OTGSC_BSV)
				set_bit(B_SESS_VLD, &motg->inputs);
			else
				clear_bit(B_SESS_VLD, &motg->inputs);
		} else if (pdata->otg_control == OTG_PMIC_CONTROL) {
			if (pdata->pmic_id_irq) {
				unsigned long flags;
				local_irq_save(flags);
				if (irq_read_line(pdata->pmic_id_irq))
					set_bit(ID, &motg->inputs);
				else
					clear_bit(ID, &motg->inputs);
				local_irq_restore(flags);
			//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
			} else {
#ifdef CONFIG_EEPROM_NUVOTON
			if (AX_MicroP_IsP01Connected() && pad_exist()) {
				printk("[usb_otg] switch to host mode (boot)\r\n");
				clear_bit(ID, &motg->inputs);
			} else {
				printk("[usb_otg] switch to peripheral mode (boot)\r\n");
				set_bit(ID, &motg->inputs);
			}
#else
			// set to peripheral
			printk("[usb_otg] switch to peripheral mode by default (boot)\r\n");
			set_bit(ID, &motg->inputs);
#endif
			}
			//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"

			/*
			 * VBUS initial state is reported after PMIC
			 * driver initialization. Wait for it.
			 */
			wait_for_completion(&pmic_vbus_init);
		}
		break;
	case USB_HOST:
		clear_bit(ID, &motg->inputs);
		break;
	case USB_PERIPHERAL:
		set_bit(ID, &motg->inputs);
		if (pdata->otg_control == OTG_PHY_CONTROL) {
			if (otgsc & OTGSC_BSV)
				set_bit(B_SESS_VLD, &motg->inputs);
			else
				clear_bit(B_SESS_VLD, &motg->inputs);
		} else if (pdata->otg_control == OTG_PMIC_CONTROL) {
			/*
			 * VBUS initial state is reported after PMIC
			 * driver initialization. Wait for it.
			 */
			wait_for_completion(&pmic_vbus_init);
		}
		break;
	default:
		break;
	}
}

static void msm_otg_sm_work(struct work_struct *w)
{
	struct msm_otg *motg = container_of(w, struct msm_otg, sm_work);
	struct otg_transceiver *otg = &motg->otg;
	bool work = 0, srp_reqd;

	pm_runtime_resume(otg->dev);
	dev_dbg(otg->dev, "%s work\n", otg_state_string(otg->state));
	switch (otg->state) {
	case OTG_STATE_UNDEFINED:
		msm_otg_reset(otg);
		msm_otg_init_sm(motg);
		psy = power_supply_get_by_name("usb");
		if (!psy)
			dev_err(otg->dev, "couldn't get usb power supply\n");
		otg->state = OTG_STATE_B_IDLE;
		if (!test_bit(B_SESS_VLD, &motg->inputs) &&
				test_bit(ID, &motg->inputs)) {
			pm_runtime_put_noidle(otg->dev);
			pm_runtime_suspend(otg->dev);
			break;
		}
		/* FALL THROUGH */
	case OTG_STATE_B_IDLE:
		if ((!test_bit(ID, &motg->inputs) ||
				test_bit(ID_A, &motg->inputs)) && otg->host) {
			dev_dbg(otg->dev, "!id || id_A\n");
			clear_bit(B_BUS_REQ, &motg->inputs);
			set_bit(A_BUS_REQ, &motg->inputs);
			otg->state = OTG_STATE_A_IDLE;
			work = 1;

			//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
			#ifdef CONFIG_CHARGER_ASUS
			cancel_delayed_work_sync(&asus_chg_work);
			#endif
			//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"

		} else if (test_bit(B_SESS_VLD, &motg->inputs)) {
			dev_dbg(otg->dev, "b_sess_vld\n");
			switch (motg->chg_state) {
			case USB_CHG_STATE_UNDEFINED:
				//msm_chg_detect_work(&motg->chg_work.work);
				queue_delayed_work(system_nrt_wq, &motg->chg_work, (500 * HZ/1000));//for smd charger detect
				break;
			case USB_CHG_STATE_DETECTED:
				switch (motg->chg_type) {
				case USB_DCP_CHARGER:
					/* Enable VDP_SRC */
					ulpi_write(otg, 0x2, 0x85);
					/* fall through */
				case USB_PROPRIETARY_CHARGER:
					msm_otg_notify_charger(motg,
							IDEV_CHG_MAX);
					pm_runtime_put_noidle(otg->dev);
					pm_runtime_suspend(otg->dev);
					break;
				case USB_ACA_B_CHARGER:
					msm_otg_notify_charger(motg,
							IDEV_ACA_CHG_MAX);
					/*
					 * (ID_B --> ID_C) PHY_ALT interrupt can
					 * not be detected in LPM.
					 */
					break;
				case USB_CDP_CHARGER:
					msm_otg_notify_charger(motg,
							IDEV_CHG_MAX);
					msm_otg_start_peripheral(otg, 1);
					otg->state = OTG_STATE_B_PERIPHERAL;
					break;
				case USB_ACA_C_CHARGER:
					msm_otg_notify_charger(motg,
							IDEV_ACA_CHG_MAX);
					msm_otg_start_peripheral(otg, 1);
					otg->state = OTG_STATE_B_PERIPHERAL;
					break;
				case USB_SDP_CHARGER:
					msm_otg_start_peripheral(otg, 1);
					otg->state = OTG_STATE_B_PERIPHERAL;
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
		} else if (test_bit(B_BUS_REQ, &motg->inputs)) {
			dev_dbg(otg->dev, "b_sess_end && b_bus_req\n");
			if (msm_otg_start_srp(otg) < 0) {
				clear_bit(B_BUS_REQ, &motg->inputs);
				work = 1;
				break;
			}
			otg->state = OTG_STATE_B_SRP_INIT;
			msm_otg_start_timer(motg, TB_SRP_FAIL, B_SRP_FAIL);
			break;
		} else {
			dev_dbg(otg->dev, "chg_work cancel");
			cancel_delayed_work_sync(&motg->chg_work);

			//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
			#ifdef CONFIG_CHARGER_ASUS
			cancel_delayed_work_sync(&asus_chg_work);
			g_charger_mode = ASUS_CHG_SRC_NONE;
			if(!g_host_mode){
				asus_chg_set_chg_mode(ASUS_CHG_SRC_NONE);
				//ASUS_BSP+++ "[USB][NA][Other] Add USB event log"
				ASUSEvtlog("[USB] set_chg_mode: None\n");
				//ASUS_BSP--- "[USB][NA][Other] Add USB event log"
				printk("[USB] set_chg_mode: None\n");
			}else{
				ASUSEvtlog("[USB] InPad not notify set_chg_mode: None\n");
				printk("[USB] InPad not notify set_chg_mode: None\n");
			}
			#endif
			//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"

			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
			msm_otg_notify_charger(motg, 0);
			msm_otg_reset(otg);
			pm_runtime_put_noidle(otg->dev);
			pm_runtime_suspend(otg->dev);
		}
		break;
	case OTG_STATE_B_SRP_INIT:
		if (!test_bit(ID, &motg->inputs) ||
				test_bit(ID_A, &motg->inputs) ||
				test_bit(ID_C, &motg->inputs) ||
				(test_bit(B_SESS_VLD, &motg->inputs) &&
				!test_bit(ID_B, &motg->inputs))) {
			dev_dbg(otg->dev, "!id || id_a/c || b_sess_vld+!id_b\n");
			msm_otg_del_timer(motg);
			otg->state = OTG_STATE_B_IDLE;
			/*
			 * clear VBUSVLDEXTSEL and VBUSVLDEXT register
			 * bits after SRP initiation.
			 */
			ulpi_write(otg, 0x0, 0x98);
			work = 1;
		} else if (test_bit(B_SRP_FAIL, &motg->tmouts)) {
			dev_dbg(otg->dev, "b_srp_fail\n");
			pr_info("A-device did not respond to SRP\n");
			clear_bit(B_BUS_REQ, &motg->inputs);
			clear_bit(B_SRP_FAIL, &motg->tmouts);
			otg_send_event(OTG_EVENT_NO_RESP_FOR_SRP);
			ulpi_write(otg, 0x0, 0x98);
			otg->state = OTG_STATE_B_IDLE;
			motg->b_last_se0_sess = jiffies;
			work = 1;
		}
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (!test_bit(ID, &motg->inputs) ||
				test_bit(ID_A, &motg->inputs) ||
				test_bit(ID_B, &motg->inputs) ||
				!test_bit(B_SESS_VLD, &motg->inputs)) {
			dev_dbg(otg->dev, "!id  || id_a/b || !b_sess_vld\n");
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
			msm_otg_notify_charger(motg, 0);
			srp_reqd = otg->gadget->otg_srp_reqd;
			msm_otg_start_peripheral(otg, 0);
			if (test_bit(ID_B, &motg->inputs))
				clear_bit(ID_B, &motg->inputs);
			clear_bit(B_BUS_REQ, &motg->inputs);
			otg->state = OTG_STATE_B_IDLE;
			motg->b_last_se0_sess = jiffies;
			if (srp_reqd)
				msm_otg_start_timer(motg,
					TB_TST_SRP, B_TST_SRP);
			else
				work = 1;
		} else if (test_bit(B_BUS_REQ, &motg->inputs) &&
				otg->gadget->b_hnp_enable &&
				test_bit(A_BUS_SUSPEND, &motg->inputs)) {
			dev_dbg(otg->dev, "b_bus_req && b_hnp_en && a_bus_suspend\n");
			msm_otg_start_timer(motg, TB_ASE0_BRST, B_ASE0_BRST);
			/* D+ pullup should not be disconnected within 4msec
			 * after A device suspends the bus. Otherwise PET will
			 * fail the compliance test.
			 */
			udelay(1000);
			msm_otg_start_peripheral(otg, 0);
			otg->state = OTG_STATE_B_WAIT_ACON;
			/*
			 * start HCD even before A-device enable
			 * pull-up to meet HNP timings.
			 */
			otg->host->is_b_host = 1;
			msm_otg_start_host(otg, 1);
		} else if (test_bit(A_BUS_SUSPEND, &motg->inputs) &&
				   test_bit(B_SESS_VLD, &motg->inputs)) {
			dev_dbg(otg->dev, "a_bus_suspend && b_sess_vld\n");
			if (motg->caps & ALLOW_LPM_ON_DEV_SUSPEND) {
				pm_runtime_put_noidle(otg->dev);
				pm_runtime_suspend(otg->dev);
			}
		} else if (test_bit(ID_C, &motg->inputs)) {
			msm_otg_notify_charger(motg, IDEV_ACA_CHG_MAX);
		}
		break;
	case OTG_STATE_B_WAIT_ACON:
		if (!test_bit(ID, &motg->inputs) ||
				test_bit(ID_A, &motg->inputs) ||
				test_bit(ID_B, &motg->inputs) ||
				!test_bit(B_SESS_VLD, &motg->inputs)) {
			dev_dbg(otg->dev, "!id || id_a/b || !b_sess_vld\n");
			msm_otg_del_timer(motg);
			/*
			 * A-device is physically disconnected during
			 * HNP. Remove HCD.
			 */
			msm_otg_start_host(otg, 0);
			otg->host->is_b_host = 0;

			clear_bit(B_BUS_REQ, &motg->inputs);
			clear_bit(A_BUS_SUSPEND, &motg->inputs);
			motg->b_last_se0_sess = jiffies;
			otg->state = OTG_STATE_B_IDLE;
			msm_otg_reset(otg);
			work = 1;
		} else if (test_bit(A_CONN, &motg->inputs)) {
			dev_dbg(otg->dev, "a_conn\n");
			clear_bit(A_BUS_SUSPEND, &motg->inputs);
			otg->state = OTG_STATE_B_HOST;
			/*
			 * PET disconnects D+ pullup after reset is generated
			 * by B device in B_HOST role which is not detected by
			 * B device. As workaorund , start timer of 300msec
			 * and stop timer if A device is enumerated else clear
			 * A_CONN.
			 */
			msm_otg_start_timer(motg, TB_TST_CONFIG,
						B_TST_CONFIG);
		} else if (test_bit(B_ASE0_BRST, &motg->tmouts)) {
			dev_dbg(otg->dev, "b_ase0_brst_tmout\n");
			dev_info(otg->dev, "B HNP fail:No response from A device\n");
			msm_otg_start_host(otg, 0);
			msm_otg_reset(otg);
			otg->host->is_b_host = 0;
			clear_bit(B_ASE0_BRST, &motg->tmouts);
			clear_bit(A_BUS_SUSPEND, &motg->inputs);
			clear_bit(B_BUS_REQ, &motg->inputs);
			otg_send_event(OTG_EVENT_HNP_FAILED);
			otg->state = OTG_STATE_B_IDLE;
			work = 1;
		} else if (test_bit(ID_C, &motg->inputs)) {
			msm_otg_notify_charger(motg, IDEV_ACA_CHG_MAX);
		}
		break;
	case OTG_STATE_B_HOST:
		if (!test_bit(B_BUS_REQ, &motg->inputs) ||
				!test_bit(A_CONN, &motg->inputs) ||
				!test_bit(B_SESS_VLD, &motg->inputs)) {
			dev_dbg(otg->dev, "!b_bus_req || !a_conn || !b_sess_vld\n");
			clear_bit(A_CONN, &motg->inputs);
			clear_bit(B_BUS_REQ, &motg->inputs);
			msm_otg_start_host(otg, 0);
			otg->host->is_b_host = 0;
			otg->state = OTG_STATE_B_IDLE;
			msm_otg_reset(otg);
			work = 1;
		} else if (test_bit(ID_C, &motg->inputs)) {
			msm_otg_notify_charger(motg, IDEV_ACA_CHG_MAX);
		}
		break;
	case OTG_STATE_A_IDLE:
		otg->default_a = 1;
		if (test_bit(ID, &motg->inputs) &&
			!test_bit(ID_A, &motg->inputs)) {
			dev_dbg(otg->dev, "id && !id_a\n");
			otg->default_a = 0;
			clear_bit(A_BUS_DROP, &motg->inputs);
			otg->state = OTG_STATE_B_IDLE;
			del_timer_sync(&motg->id_timer);
			msm_otg_link_reset(motg);
			msm_chg_enable_aca_intr(motg);
			msm_otg_notify_charger(motg, 0);
			work = 1;
		} else if (!test_bit(A_BUS_DROP, &motg->inputs) &&
				(test_bit(A_SRP_DET, &motg->inputs) ||
				 test_bit(A_BUS_REQ, &motg->inputs))) {
			dev_dbg(otg->dev, "!a_bus_drop && (a_srp_det || a_bus_req)\n");

			clear_bit(A_SRP_DET, &motg->inputs);
			/* Disable SRP detection */
			writel_relaxed((readl_relaxed(USB_OTGSC) &
					~OTGSC_INTSTS_MASK) &
					~OTGSC_DPIE, USB_OTGSC);

			otg->state = OTG_STATE_A_WAIT_VRISE;
			/* VBUS should not be supplied before end of SRP pulse
			 * generated by PET, if not complaince test fail.
			 */
			usleep_range(10000, 12000);
			/* ACA: ID_A: Stop charging untill enumeration */
			if (test_bit(ID_A, &motg->inputs))
				msm_otg_notify_charger(motg, 0);
			else
				msm_hsusb_vbus_power(motg, 1);
			msm_otg_start_timer(motg, TA_WAIT_VRISE, A_WAIT_VRISE);
		} else {
			dev_dbg(otg->dev, "No session requested\n");
			clear_bit(A_BUS_DROP, &motg->inputs);
			if (test_bit(ID_A, &motg->inputs)) {
					msm_otg_notify_charger(motg,
							IDEV_ACA_CHG_MAX);
			} else if (!test_bit(ID, &motg->inputs)) {
				msm_otg_notify_charger(motg, 0);
				/*
				 * A-device is not providing power on VBUS.
				 * Enable SRP detection.
				 */
				writel_relaxed(0x13, USB_USBMODE);
				writel_relaxed((readl_relaxed(USB_OTGSC) &
						~OTGSC_INTSTS_MASK) |
						OTGSC_DPIE, USB_OTGSC);
				mb();
			}
		}
		break;
	case OTG_STATE_A_WAIT_VRISE:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs) ||
				test_bit(A_WAIT_VRISE, &motg->tmouts)) {
			dev_dbg(otg->dev, "id || a_bus_drop || a_wait_vrise_tmout\n");
			clear_bit(A_BUS_REQ, &motg->inputs);
			msm_otg_del_timer(motg);
			msm_hsusb_vbus_power(motg, 0);
			otg->state = OTG_STATE_A_WAIT_VFALL;
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
		} else if (test_bit(A_VBUS_VLD, &motg->inputs)) {
			dev_dbg(otg->dev, "a_vbus_vld\n");
			otg->state = OTG_STATE_A_WAIT_BCON;
			if (TA_WAIT_BCON > 0)
				msm_otg_start_timer(motg, TA_WAIT_BCON,
					A_WAIT_BCON);
			msm_otg_start_host(otg, 1);
			msm_chg_enable_aca_det(motg);
			msm_chg_disable_aca_intr(motg);
			mod_timer(&motg->id_timer, ID_TIMER_FREQ);
			if (msm_chg_check_aca_intr(motg))
				work = 1;
		}
		break;
	case OTG_STATE_A_WAIT_BCON:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs) ||
				test_bit(A_WAIT_BCON, &motg->tmouts)) {
			dev_dbg(otg->dev, "(id && id_a/b/c) || a_bus_drop ||"
					"a_wait_bcon_tmout\n");
			if (test_bit(A_WAIT_BCON, &motg->tmouts)) {
				dev_info(otg->dev, "Device No Response\n");
				otg_send_event(OTG_EVENT_DEV_CONN_TMOUT);
			}
			msm_otg_del_timer(motg);
			clear_bit(A_BUS_REQ, &motg->inputs);
			clear_bit(B_CONN, &motg->inputs);
			msm_otg_start_host(otg, 0);
			/*
			 * ACA: ID_A with NO accessory, just the A plug is
			 * attached to ACA: Use IDCHG_MAX for charging
			 */
			if (test_bit(ID_A, &motg->inputs))
				msm_otg_notify_charger(motg, IDEV_CHG_MIN);
			else
				msm_hsusb_vbus_power(motg, 0);
			otg->state = OTG_STATE_A_WAIT_VFALL;
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
		} else if (!test_bit(A_VBUS_VLD, &motg->inputs)) {
			dev_dbg(otg->dev, "!a_vbus_vld\n");
			clear_bit(B_CONN, &motg->inputs);
			msm_otg_del_timer(motg);
			msm_otg_start_host(otg, 0);
			otg->state = OTG_STATE_A_VBUS_ERR;
			msm_otg_reset(otg);
		} else if (test_bit(ID_A, &motg->inputs)) {
			msm_hsusb_vbus_power(motg, 0);
		} else if (!test_bit(A_BUS_REQ, &motg->inputs)) {
			/*
			 * If TA_WAIT_BCON is infinite, we don;t
			 * turn off VBUS. Enter low power mode.
			 */
			if (TA_WAIT_BCON < 0)
				pm_runtime_put_sync(otg->dev);
		} else if (!test_bit(ID, &motg->inputs)) {
			msm_hsusb_vbus_power(motg, 1);
		}
		break;
	case OTG_STATE_A_HOST:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs)) {
			dev_dbg(otg->dev, "id_a/b/c || a_bus_drop\n");
			clear_bit(B_CONN, &motg->inputs);
			clear_bit(A_BUS_REQ, &motg->inputs);
			msm_otg_del_timer(motg);
			otg->state = OTG_STATE_A_WAIT_VFALL;
			msm_otg_start_host(otg, 0);
			if (!test_bit(ID_A, &motg->inputs))
				msm_hsusb_vbus_power(motg, 0);
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
		} else if (!test_bit(A_VBUS_VLD, &motg->inputs)) {
			dev_dbg(otg->dev, "!a_vbus_vld\n");
			clear_bit(B_CONN, &motg->inputs);
			msm_otg_del_timer(motg);
			otg->state = OTG_STATE_A_VBUS_ERR;
			msm_otg_start_host(otg, 0);
			msm_otg_reset(otg);
		} else if (!test_bit(A_BUS_REQ, &motg->inputs)) {
			/*
			 * a_bus_req is de-asserted when root hub is
			 * suspended or HNP is in progress.
			 */
			dev_dbg(otg->dev, "!a_bus_req\n");
			msm_otg_del_timer(motg);
			otg->state = OTG_STATE_A_SUSPEND;
			if (otg->host->b_hnp_enable)
				msm_otg_start_timer(motg, TA_AIDL_BDIS,
						A_AIDL_BDIS);
			else
				pm_runtime_put_sync(otg->dev);
		} else if (!test_bit(B_CONN, &motg->inputs)) {
			dev_dbg(otg->dev, "!b_conn\n");
			msm_otg_del_timer(motg);
			otg->state = OTG_STATE_A_WAIT_BCON;
			if (TA_WAIT_BCON > 0)
				msm_otg_start_timer(motg, TA_WAIT_BCON,
					A_WAIT_BCON);
			if (msm_chg_check_aca_intr(motg))
				work = 1;
		} else if (test_bit(ID_A, &motg->inputs)) {
			msm_otg_del_timer(motg);
			msm_hsusb_vbus_power(motg, 0);
			if (motg->chg_type == USB_ACA_DOCK_CHARGER)
				msm_otg_notify_charger(motg,
						IDEV_ACA_CHG_MAX);
			else
				msm_otg_notify_charger(motg,
						IDEV_CHG_MIN - motg->mA_port);
		} else if (!test_bit(ID, &motg->inputs)) {
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
			msm_otg_notify_charger(motg, 0);
			msm_hsusb_vbus_power(motg, 1);
		}
		break;
	case OTG_STATE_A_SUSPEND:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs) ||
				test_bit(A_AIDL_BDIS, &motg->tmouts)) {
			dev_dbg(otg->dev, "id_a/b/c || a_bus_drop ||"
					"a_aidl_bdis_tmout\n");
			msm_otg_del_timer(motg);
			clear_bit(B_CONN, &motg->inputs);
			otg->state = OTG_STATE_A_WAIT_VFALL;
			msm_otg_start_host(otg, 0);
			msm_otg_reset(otg);
			if (!test_bit(ID_A, &motg->inputs))
				msm_hsusb_vbus_power(motg, 0);
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
		} else if (!test_bit(A_VBUS_VLD, &motg->inputs)) {
			dev_dbg(otg->dev, "!a_vbus_vld\n");
			msm_otg_del_timer(motg);
			clear_bit(B_CONN, &motg->inputs);
			otg->state = OTG_STATE_A_VBUS_ERR;
			msm_otg_start_host(otg, 0);
			msm_otg_reset(otg);
		} else if (!test_bit(B_CONN, &motg->inputs) &&
				otg->host->b_hnp_enable) {
			dev_dbg(otg->dev, "!b_conn && b_hnp_enable");
			otg->state = OTG_STATE_A_PERIPHERAL;
			msm_otg_host_hnp_enable(otg, 1);
			otg->gadget->is_a_peripheral = 1;
			msm_otg_start_peripheral(otg, 1);
		} else if (!test_bit(B_CONN, &motg->inputs) &&
				!otg->host->b_hnp_enable) {
			dev_dbg(otg->dev, "!b_conn && !b_hnp_enable");
			/*
			 * bus request is dropped during suspend.
			 * acquire again for next device.
			 */
			set_bit(A_BUS_REQ, &motg->inputs);
			otg->state = OTG_STATE_A_WAIT_BCON;
			if (TA_WAIT_BCON > 0)
				msm_otg_start_timer(motg, TA_WAIT_BCON,
					A_WAIT_BCON);
		} else if (test_bit(ID_A, &motg->inputs)) {
			msm_hsusb_vbus_power(motg, 0);
			msm_otg_notify_charger(motg,
					IDEV_CHG_MIN - motg->mA_port);
		} else if (!test_bit(ID, &motg->inputs)) {
			msm_otg_notify_charger(motg, 0);
			msm_hsusb_vbus_power(motg, 1);
		}
		break;
	case OTG_STATE_A_PERIPHERAL:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs)) {
			dev_dbg(otg->dev, "id _f/b/c || a_bus_drop\n");
			/* Clear BIDL_ADIS timer */
			msm_otg_del_timer(motg);
			otg->state = OTG_STATE_A_WAIT_VFALL;
			msm_otg_start_peripheral(otg, 0);
			otg->gadget->is_a_peripheral = 0;
			msm_otg_start_host(otg, 0);
			msm_otg_reset(otg);
			if (!test_bit(ID_A, &motg->inputs))
				msm_hsusb_vbus_power(motg, 0);
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
		} else if (!test_bit(A_VBUS_VLD, &motg->inputs)) {
			dev_dbg(otg->dev, "!a_vbus_vld\n");
			/* Clear BIDL_ADIS timer */
			msm_otg_del_timer(motg);
			otg->state = OTG_STATE_A_VBUS_ERR;
			msm_otg_start_peripheral(otg, 0);
			otg->gadget->is_a_peripheral = 0;
			msm_otg_start_host(otg, 0);
		} else if (test_bit(A_BIDL_ADIS, &motg->tmouts)) {
			dev_dbg(otg->dev, "a_bidl_adis_tmout\n");
			msm_otg_start_peripheral(otg, 0);
			otg->gadget->is_a_peripheral = 0;
			otg->state = OTG_STATE_A_WAIT_BCON;
			set_bit(A_BUS_REQ, &motg->inputs);
			msm_otg_host_hnp_enable(otg, 0);
			if (TA_WAIT_BCON > 0)
				msm_otg_start_timer(motg, TA_WAIT_BCON,
					A_WAIT_BCON);
		} else if (test_bit(ID_A, &motg->inputs)) {
			msm_hsusb_vbus_power(motg, 0);
			msm_otg_notify_charger(motg,
					IDEV_CHG_MIN - motg->mA_port);
		} else if (!test_bit(ID, &motg->inputs)) {
			msm_otg_notify_charger(motg, 0);
			msm_hsusb_vbus_power(motg, 1);
		}
		break;
	case OTG_STATE_A_WAIT_VFALL:
		if (test_bit(A_WAIT_VFALL, &motg->tmouts)) {
			clear_bit(A_VBUS_VLD, &motg->inputs);
			otg->state = OTG_STATE_A_IDLE;
			work = 1;
		}
		break;
	case OTG_STATE_A_VBUS_ERR:
		if ((test_bit(ID, &motg->inputs) &&
				!test_bit(ID_A, &motg->inputs)) ||
				test_bit(A_BUS_DROP, &motg->inputs) ||
				test_bit(A_CLR_ERR, &motg->inputs)) {
			otg->state = OTG_STATE_A_WAIT_VFALL;
			if (!test_bit(ID_A, &motg->inputs))
				msm_hsusb_vbus_power(motg, 0);
			msm_otg_start_timer(motg, TA_WAIT_VFALL, A_WAIT_VFALL);
			motg->chg_state = USB_CHG_STATE_UNDEFINED;
			motg->chg_type = USB_INVALID_CHARGER;
			msm_otg_notify_charger(motg, 0);
		}
		break;
	default:
		break;
	}
	if (work)
		queue_work(system_nrt_wq, &motg->sm_work);
}

static irqreturn_t msm_otg_irq(int irq, void *data)
{
	struct msm_otg *motg = data;
	struct otg_transceiver *otg = &motg->otg;
	u32 otgsc = 0, usbsts, pc;
	bool work = 0;
	irqreturn_t ret = IRQ_HANDLED;

	/* The fix is for the issue that while plugging in Pad Station, sometimes
	   two successive IRQ requests would be received in LPM mode. One
	   is from PMIC callback funtion "msm_otg_set_vbus_state()" and the
	   other one is from CPU IRQ interrupt. That means the pm_request_resume()
	   in following code section would be executed for twice. This causes
	   the usage counter to unbalance and causes otg driver not to receive
	   IRQ requests any more. */
	//ASUS_BSP+++ BennyCheng "add async_int protection to avoid two successive IRQ requests"
	if (atomic_read(&motg->in_lpm) && motg->async_int == 0) {
	//ASUS_BSP--- BennyCheng "add async_int protection to avoid two successive IRQ requests"
		pr_debug("OTG IRQ: in LPM\n");
		disable_irq_nosync(irq);
		motg->async_int = 1;

		if (atomic_read(&motg->pm_suspended)) {
			motg->sm_work_pending = true;
			if ((otg->state == OTG_STATE_A_SUSPEND) ||
				(otg->state == OTG_STATE_A_WAIT_BCON))
				set_bit(A_BUS_REQ, &motg->inputs);
		} else {
			pm_request_resume(otg->dev);
		}
		return IRQ_HANDLED;
	}

	usbsts = readl(USB_USBSTS);
	//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
	#ifdef CONFIG_CHARGER_ASUS
	if(usbsts & (1<<6)){//check usb reset
		if(g_charger_mode!=ASUS_CHG_SRC_USB){
			schedule_work(&asus_usb_work);
		}
	}
	if(g_usb_boot == MSM_OTG_USB_BOOT_INIT){
		g_usb_boot = MSM_OTG_USB_BOOT_IRQ;
	}
	#endif
	//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"
	otgsc = readl(USB_OTGSC);

	if (!(otgsc & OTG_OTGSTS_MASK) && !(usbsts & OTG_USBSTS_MASK))
		return IRQ_NONE;

	if ((otgsc & OTGSC_IDIS) && (otgsc & OTGSC_IDIE)) {
		if (otgsc & OTGSC_ID) {
			pr_debug("Id set\n");
			set_bit(ID, &motg->inputs);
		} else {
			pr_debug("Id clear\n");
			/*
			 * Assert a_bus_req to supply power on
			 * VBUS when Micro/Mini-A cable is connected
			 * with out user intervention.
			 */
			set_bit(A_BUS_REQ, &motg->inputs);
			clear_bit(ID, &motg->inputs);
			msm_chg_enable_aca_det(motg);
		}
		writel_relaxed(otgsc, USB_OTGSC);
		work = 1;
	} else if (otgsc & OTGSC_DPIS) {
		pr_debug("DPIS detected\n");
		writel_relaxed(otgsc, USB_OTGSC);
		set_bit(A_SRP_DET, &motg->inputs);
		set_bit(A_BUS_REQ, &motg->inputs);
		work = 1;
	} else if (otgsc & OTGSC_BSVIS) {
		writel_relaxed(otgsc, USB_OTGSC);
		/*
		 * BSV interrupt comes when operating as an A-device
		 * (VBUS on/off).
		 * But, handle BSV when charger is removed from ACA in ID_A
		 */
		if ((otg->state >= OTG_STATE_A_IDLE) &&
			!test_bit(ID_A, &motg->inputs))
			return IRQ_HANDLED;
		//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
		if ((USB_AUTO == motg->otg_mode || USB_AUTO == motg->otg_mode) &&
				AX_MicroP_IsP01Connected() && pad_exist()) {
			if (otgsc & OTGSC_BSV) {
				pr_debug("BSV set\n");
				msm_otg_bsv = 1;
			} else {
				pr_debug("BSV clear\n");
				msm_otg_bsv = 0;
				clear_bit(A_BUS_SUSPEND, &motg->inputs);

				msm_chg_check_aca_intr(motg);
			}
		} else {
			if (otgsc & OTGSC_BSV) {
				pr_debug("BSV set\n");
				set_bit(B_SESS_VLD, &motg->inputs);
			} else {
				pr_debug("BSV clear\n");
				clear_bit(B_SESS_VLD, &motg->inputs);
				clear_bit(A_BUS_SUSPEND, &motg->inputs);

				msm_chg_check_aca_intr(motg);
			}
		}
		//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"
		work = 1;
	} else if (usbsts & STS_PCI) {
		pc = readl_relaxed(USB_PORTSC);
		pr_debug("portsc = %x\n", pc);
		ret = IRQ_NONE;
		/*
		 * HCD Acks PCI interrupt. We use this to switch
		 * between different OTG states.
		 */
		work = 1;
		switch (otg->state) {
		case OTG_STATE_A_SUSPEND:
			if (otg->host->b_hnp_enable && (pc & PORTSC_CSC) &&
					!(pc & PORTSC_CCS)) {
				pr_debug("B_CONN clear\n");
				clear_bit(B_CONN, &motg->inputs);
				msm_otg_del_timer(motg);
			}
			break;
		case OTG_STATE_A_PERIPHERAL:
			/*
			 * A-peripheral observed activity on bus.
			 * clear A_BIDL_ADIS timer.
			 */
			msm_otg_del_timer(motg);
			work = 0;
			break;
		case OTG_STATE_B_WAIT_ACON:
			if ((pc & PORTSC_CSC) && (pc & PORTSC_CCS)) {
				pr_debug("A_CONN set\n");
				set_bit(A_CONN, &motg->inputs);
				/* Clear ASE0_BRST timer */
				msm_otg_del_timer(motg);
			}
			break;
		case OTG_STATE_B_HOST:
			if ((pc & PORTSC_CSC) && !(pc & PORTSC_CCS)) {
				pr_debug("A_CONN clear\n");
				clear_bit(A_CONN, &motg->inputs);
				msm_otg_del_timer(motg);
			}
			break;
		case OTG_STATE_A_WAIT_BCON:
			if (TA_WAIT_BCON < 0)
				set_bit(A_BUS_REQ, &motg->inputs);
		default:
			work = 0;
			break;
		}
	} else if (usbsts & STS_URI) {
		ret = IRQ_NONE;
		switch (otg->state) {
		case OTG_STATE_A_PERIPHERAL:
			/*
			 * A-peripheral observed activity on bus.
			 * clear A_BIDL_ADIS timer.
			 */
			msm_otg_del_timer(motg);
			work = 0;
			break;
		default:
			work = 0;
			break;
		}
	} else if (usbsts & STS_SLI) {
		ret = IRQ_NONE;
		work = 0;
		switch (otg->state) {
		case OTG_STATE_B_PERIPHERAL:
			if (otg->gadget->b_hnp_enable) {
				set_bit(A_BUS_SUSPEND, &motg->inputs);
				set_bit(B_BUS_REQ, &motg->inputs);
				work = 1;
			}
			break;
		case OTG_STATE_A_PERIPHERAL:
			msm_otg_start_timer(motg, TA_BIDL_ADIS,
					A_BIDL_ADIS);
			break;
		default:
			break;
		}
	} else if ((usbsts & PHY_ALT_INT)) {
		writel_relaxed(PHY_ALT_INT, USB_USBSTS);
		if (msm_chg_check_aca_intr(motg))
			work = 1;
		ret = IRQ_HANDLED;
	}
	if (work)
		queue_work(system_nrt_wq, &motg->sm_work);

	return ret;
}

static void msm_otg_set_vbus_state(int online)
{
	static bool init;
	struct msm_otg *motg = the_msm_otg;

	/* In A Host Mode, ignore received BSV interrupts */
	//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
	msm_otg_bsv = online;
	if (g_host_mode) {
		pr_debug("PMIC: ignore bsv events in host mode (%d)\n",online);
		return;
	}
	//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"

//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
	if((test_bit(B_SESS_VLD, &motg->inputs) && online) ||
		(!test_bit(B_SESS_VLD, &motg->inputs) && !online)){
		if(init){
			pr_debug("PMIC: BSV already set to %d\n",online);
			return;
		}
	}
//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"

	if (online) {
		pr_debug("PMIC: BSV set\n");
		set_bit(B_SESS_VLD, &motg->inputs);
//ASUS_BSP+++ "[USB][NA][Other] Add USB event log"
		ASUSEvtlog("[USB] plugin\n");
//ASUS_BSP--- "[USB][NA][Other] Add USB event log"
	} else {
		pr_debug("PMIC: BSV clear\n");
		clear_bit(B_SESS_VLD, &motg->inputs);
//ASUS_BSP+++ "[USB][NA][Other] Add USB event log"
		ASUSEvtlog("[USB] unplug\n");
//ASUS_BSP--- "[USB][NA][Other] Add USB event log"
	}

	if (!init) {
		init = true;
		complete(&pmic_vbus_init);
		pr_debug("PMIC: BSV init complete\n");
		return;
	}

	if (atomic_read(&motg->pm_suspended))
		motg->sm_work_pending = true;
	else
		queue_work(system_nrt_wq, &motg->sm_work);
}

static void msm_pmic_id_status_w(struct work_struct *w)
{
	struct msm_otg *motg = container_of(w, struct msm_otg,
						pmic_id_status_work.work);
	int work = 0;
	unsigned long flags;

	local_irq_save(flags);
	if (irq_read_line(motg->pdata->pmic_id_irq)) {
		if (!test_and_set_bit(ID, &motg->inputs)) {
			pr_debug("PMIC: ID set\n");
			work = 1;
		}
	} else {
		if (test_and_clear_bit(ID, &motg->inputs)) {
			pr_debug("PMIC: ID clear\n");
			set_bit(A_BUS_REQ, &motg->inputs);
			work = 1;
		}
	}

	if (work && (motg->otg.state != OTG_STATE_UNDEFINED)) {
		if (atomic_read(&motg->pm_suspended))
			motg->sm_work_pending = true;
		else
			queue_work(system_nrt_wq, &motg->sm_work);
	}
	local_irq_restore(flags);

}

#define MSM_PMIC_ID_STATUS_DELAY	5 /* 5msec */
static irqreturn_t msm_pmic_id_irq(int irq, void *data)
{
	struct msm_otg *motg = data;

	if (!aca_id_turned_on)
		/*schedule delayed work for 5msec for ID line state to settle*/
		queue_delayed_work(system_nrt_wq, &motg->pmic_id_status_work,
				msecs_to_jiffies(MSM_PMIC_ID_STATUS_DELAY));

	return IRQ_HANDLED;
}

//ASUS_BSP+++ BennyCheng "add proc debug files"
static int msm_otg_apq_mdm_switch(enum usb_apq_mdm_sw req_side)
{
	int ret = -1;

	switch (req_side) {
	case USB_MDM:
		ret = gpio_direction_output(GPIO_APQ_MDM_SW_SEL, 0);
		if(ret) {
			printk("[usb_otg] switch to mdm usb fail!!!(%d)(%d)\r\n", req_side, ret);
			goto out;
		}
		printk("[usb_otg] switch to mdm usb (%d)\r\n", gpio_get_value(GPIO_APQ_MDM_SW_SEL));
		break;
	case USB_APQ:
		ret = gpio_direction_output(GPIO_APQ_MDM_SW_SEL, 1);
		if(ret) {
			printk("[usb_otg] switch to apq usb fail!!!(%d)(%d)\r\n", req_side, ret);
			goto out;
		}
		printk("[usb_otg] switch to apq usb (%d)\r\n", gpio_get_value(GPIO_APQ_MDM_SW_SEL));
		break;
	default:
		printk("[usb_otg] unknown switch!!! (%d)\r\n", req_side);
		goto out;
	}

out:
	return ret;
}

static int msm_otg_usb_mhl_switch(enum usb_mhl_sw req_side)
{
	int ret = -1;

	switch (req_side) {
	case USB_PORT:
		ret = gpio_direction_output(GPIO_USB_MHL_SW_SEL, 0);
		if(ret) {
			printk("[usb_otg] switch to usb port fail!!!(%d)(%d)\r\n", req_side, ret);
			goto out;
		}
		printk("[usb_otg] switch to usb port (%d)\r\n", gpio_get_value(GPIO_USB_MHL_SW_SEL));
		break;
	case MHL_PORT:
		ret = gpio_direction_output(GPIO_USB_MHL_SW_SEL, 1);
		if(ret) {
			printk("[usb_otg] switch to mhl port fail!!!(%d)(%d)\r\n", req_side, ret);
			goto out;
		}
		printk("[usb_otg] switch to mhl port (%d)\r\n", gpio_get_value(GPIO_USB_MHL_SW_SEL));
		break;
	default:
		printk("[usb_otg] unknown switch!!! (%d)\r\n", req_side);
		goto out;
	}

out:
	return ret;
}
//ASUS_BSP--- BennyCheng "add proc debug files"

//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
static int msm_otg_mode_show(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;

	if (USB_AUTO == motg->otg_mode) {
		if(!test_bit(ID, &motg->inputs)) {
			seq_printf(s, "host (auto)\n");
		} else {
			seq_printf(s, "peripheral (auto)\n");
		}
	} else {
		if(!test_bit(ID, &motg->inputs)) {
			seq_printf(s, "host\n");
		} else {
			seq_printf(s, "peripheral\n");
		}
	}

	return 0;
}

static int msm_otg_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_mode_show, inode->i_private);
}

static void msm_otg_mode_switch(enum usb_mode_type req_mode)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;

	switch (req_mode) {
	case USB_NONE:
		printk("[usb_otg] switch to none mode\r\n");
		set_bit(ID, &motg->inputs);
		clear_bit(B_SESS_VLD, &motg->inputs);
		g_host_mode = 0;
		break;
	case USB_PERIPHERAL:
		printk("[usb_otg] switch to peripheral mode\r\n");
		set_bit(ID, &motg->inputs);
		g_host_mode = 0;
		break;
	case USB_HOST:
		printk("[usb_otg] switch to host mode\r\n");
		clear_bit(ID, &motg->inputs);
		g_host_mode = 1;
		break;
	case USB_AUTO:
#ifdef CONFIG_EEPROM_NUVOTON
		if (AX_MicroP_IsP01Connected() && pad_exist()) {
			printk("[usb_otg] switch to host mode (auto)\r\n");
			clear_bit(ID, &motg->inputs);
			g_host_mode = 1;
		} else {
			printk("[usb_otg] switch to peripheral mode (auto)\r\n");
			set_bit(ID, &motg->inputs);
			g_host_mode = 0;
		}
		break;
#else
		printk("[usb_otg] auto mode not support\r\n");
		goto out;
#endif
	default:
		printk("[usb_otg] unknown mode!!! (%d)\r\n", req_mode);
		goto out;
	}

	pm_runtime_resume(otg->dev);
	queue_work(system_nrt_wq, &motg->sm_work);
out:
	return;
}

static ssize_t msm_otg_mode_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct msm_otg *motg = s->private;
	char buf[16];
	int status = count;
	enum usb_mode_type req_mode;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "host", 4)) {
		req_mode = USB_HOST;
	} else if (!strncmp(buf, "peripheral", 10)) {
		req_mode = USB_PERIPHERAL;
	} else if (!strncmp(buf, "none", 4)) {
		req_mode = USB_NONE;
	} else if (!strncmp(buf, "auto", 4)) {
		req_mode = USB_AUTO;
	} else {
		status = -EINVAL;
		goto out;
	}

	motg->otg_mode = req_mode;
	msm_otg_mode_switch(req_mode);
out:
	return status;
}

#ifdef CONFIG_EEPROM_NUVOTON
static void msm_otg_microp_cb_delay_work(struct work_struct *w)
{
	struct msm_otg *motg = the_msm_otg;
	struct otg_transceiver *otg = &motg->otg;

	dev_info(otg->dev, "%s()+++\n", __func__);

	msm_otg_usb_mhl_switch(MHL_PORT);

	if (USB_AUTO == motg->otg_mode) {
		msm_otg_mode_switch(USB_AUTO);

		msm_otg_set_pad_hub_power(1);
		msm_otg_set_pad_camera_power(1);
	} else {
		if (USB_HOST == motg->otg_mode) {
			msm_otg_set_pad_hub_power(1);
			msm_otg_set_pad_camera_power(1);
		}
		printk("[usb_otg] not auto mode! skip switch! (%d)\r\n", motg->otg_mode);
	}

	dev_info(otg->dev, "%s()---\n", __func__);
}

static int usb_otg_microp_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct msm_otg *motg = the_msm_otg;

	switch (event) {
		case P01_ADD:
			printk("[usb_otg] Microp ADD Event +++\n");

			msm_otg_apq_mdm_switch(USB_APQ);
			msm_otg_usb_mhl_switch(MHL_PORT);

			msm_otg_set_pad_camera_power(0);
			msm_otg_set_pad_hub_power(0);

			queue_delayed_work_on(0, microp_cb_delay_wq, &microp_cb_delay_work, 2 * HZ);

			printk("[usb_otg] Microp ADD Event ---\n");
		break;
		case P01_REMOVE:
			printk("[usb_otg] Microp REMOVE Event +++\n");

			cancel_delayed_work_sync(&early_suspend_delay_work);
			cancel_work_sync(&late_resume_work);
			cancel_delayed_work_sync(&microp_cb_delay_work);
			cancel_delayed_work_sync(&msm_otg_suspend_check_work);

			if (msm_otg_bsv) {
				set_bit(B_SESS_VLD, &motg->inputs);
			} else {
				clear_bit(B_SESS_VLD, &motg->inputs);
			}

			if (USB_AUTO == motg->otg_mode) {
				msm_otg_mode_switch(USB_AUTO);
			} else {
				printk("[usb_otg] not auto mode! skip switch! (%d)\r\n", motg->otg_mode);
			}

			msm_otg_usb_mhl_switch(USB_PORT);
			msm_otg_apq_mdm_switch(USB_MDM);

			printk("[usb_otg] Microp REMOVE Event ---\n");
		break;
	default:
		break;
	}

        return NOTIFY_DONE;
}

static struct notifier_block usb_otg_microp_notifier = {
        .notifier_call = usb_otg_microp_event,
        .priority = USB_MP_NOTIFY,
};
#endif

static ssize_t msm_otg_pad_hub_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];
	int status = count;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "on", 2)) {
		msm_otg_set_pad_hub_power(1);
	} else if (!strncmp(buf, "off", 3)) {
		msm_otg_set_pad_hub_power(0);
	} else {
		status = -EINVAL;
		goto out;
	}
out:
	return status;
}

static int msm_otg_pad_hub_show(struct seq_file *s, void *unused)
{
	int pin_level = -1;

	pin_level = msm_otg_get_pad_hub_power();

	if (pin_level >= 0) {
		if (pin_level) {
			seq_printf(s, "on\n");
		} else {
			seq_printf(s, "off\n");
		}
	} else {
		seq_printf(s, "err\n");
	}

	return 0;
}

static int msm_otg_pad_hub_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_pad_hub_show, inode->i_private);
}

const struct file_operations msm_otg_pad_hub_fops = {
	.open = msm_otg_pad_hub_open,
	.read = seq_read,
	.write = msm_otg_pad_hub_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t msm_otg_pad_camera_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];
	int status = count;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "on", 2)) {
		msm_otg_set_pad_camera_power(1);
	} else if (!strncmp(buf, "off", 3)) {
		msm_otg_set_pad_camera_power(0);
	} else {
		status = -EINVAL;
		goto out;
	}
out:
	return status;
}

static int msm_otg_pad_camera_show(struct seq_file *s, void *unused)
{
	int pin_level = -1;

	pin_level = msm_otg_get_pad_camera_power();

	if (pin_level >= 0) {
		if (pin_level) {
			seq_printf(s, "on\n");
		} else {
			seq_printf(s, "off\n");
		}
	} else {
		seq_printf(s, "err\n");
	}

	return 0;
}

static int msm_otg_pad_camera_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_pad_camera_show, inode->i_private);
}

const struct file_operations msm_otg_pad_camera_fops = {
	.open = msm_otg_pad_camera_open,
	.read = seq_read,
	.write = msm_otg_pad_camera_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t msm_otg_pad_hub_pm_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];
	int status = count;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "enable", 6)) {
		pad_hub_pm_enable = 1;
	} else if (!strncmp(buf, "disable", 7)) {
		pad_hub_pm_enable = 0;
	} else {
		status = -EINVAL;
		goto out;
	}
out:
	return status;
}

static int msm_otg_pad_hub_pm_show(struct seq_file *s, void *unused)
{
	if (pad_hub_pm_enable) {
		seq_printf(s, "enable\n");
	} else {
		seq_printf(s, "disable\n");
	}

	return 0;
}

static int msm_otg_pad_hub_pm_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_pad_hub_pm_show, inode->i_private);
}

const struct file_operations msm_otg_pad_hub_pm_fops = {
	.open = msm_otg_pad_hub_pm_open,
	.read = seq_read,
	.write = msm_otg_pad_hub_pm_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_gpio_apq_mdm_sw_show(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;
	int gpio_value = 0;

	if (g_A68_hwID == A68_EVB || g_A68_hwID == A68_UNKNOWN) {
		dev_err(motg->otg.dev, "not support for the HW!(%d)\n", g_A68_hwID);
		return -EPERM;
	}

	gpio_value = gpio_get_value(GPIO_APQ_MDM_SW_SEL);

	if (1 == gpio_value) {
		seq_printf(s, "apq (%d)(%d)\n", GPIO_APQ_MDM_SW_SEL, gpio_value);
	} else if (0 == gpio_value) {
		seq_printf(s, "mdm (%d)(%d)\n", GPIO_APQ_MDM_SW_SEL, gpio_value);
	} else {
		seq_printf(s, "unknown (%d)(%d)\n", GPIO_APQ_MDM_SW_SEL, gpio_value);
	}

	return 0;
}

static int msm_otg_gpio_apq_mdm_sw_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_gpio_apq_mdm_sw_show, inode->i_private);
}

static ssize_t msm_otg_gpio_apq_mdm_sw_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct msm_otg *motg = s->private;
	char buf[16];
	int status = count;

	if (g_A68_hwID == A68_EVB || g_A68_hwID == A68_UNKNOWN) {
		dev_err(motg->otg.dev, "not support for the HW!(%d)\n", g_A68_hwID);
		return -EPERM;
	}

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "0", 1)) {
		gpio_direction_output(GPIO_APQ_MDM_SW_SEL, 0);
	} else if (!strncmp(buf, "1", 1)) {
		gpio_direction_output(GPIO_APQ_MDM_SW_SEL, 1);
	} else {
		status = -EINVAL;
		goto out;
	}
out:
	return status;
}

const struct file_operations msm_otg_gpio_apq_mdm_sw_fops = {
	.open = msm_otg_gpio_apq_mdm_sw_open,
	.read = seq_read,
	.write = msm_otg_gpio_apq_mdm_sw_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_gpio_usb_mhl_sw_show(struct seq_file *s, void *unused)
{
	int gpio_value = 0;

	gpio_value = gpio_get_value(GPIO_USB_MHL_SW_SEL);

	if (1 == gpio_value) {
		seq_printf(s, "mhl (%d)(%d)\n", GPIO_USB_MHL_SW_SEL, gpio_value);
	} else if (0 == gpio_value) {
		seq_printf(s, "usb (%d)(%d)\n", GPIO_USB_MHL_SW_SEL, gpio_value);
	} else {
		seq_printf(s, "unknown (%d)(%d)\n", GPIO_USB_MHL_SW_SEL, gpio_value);
	}

	return 0;
}

static int msm_otg_gpio_usb_mhl_sw_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_gpio_usb_mhl_sw_show, inode->i_private);
}

static ssize_t msm_otg_gpio_usb_mhl_sw_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];
	int status = count;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "0", 1)) {
		gpio_direction_output(GPIO_USB_MHL_SW_SEL, 0);
	} else if (!strncmp(buf, "1", 1)) {
		gpio_direction_output(GPIO_USB_MHL_SW_SEL, 1);
	} else {
		status = -EINVAL;
		goto out;
	}
out:
	return status;
}

const struct file_operations msm_otg_gpio_usb_mhl_sw_fops = {
	.open = msm_otg_gpio_usb_mhl_sw_open,
	.read = seq_read,
	.write = msm_otg_gpio_usb_mhl_sw_write,
	.llseek = seq_lseek,
	.release = single_release,
};
//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"

//ASUS_BSP+++ BennyCheng "implement factory usb mode"
#ifdef ASUS_FACTORY_BUILD
static int msm_otg_gpio_factory_usb_enable_show(struct seq_file *s, void *unused)
{
	int gpio_value = 0;

	gpio_value = gpio_get_value(GPIO_FACTORY_USB_ENABLE);

	if (1 == gpio_value) {
		seq_printf(s, "disable (%d)(%d)\n", GPIO_FACTORY_USB_ENABLE, gpio_value);
	} else if (0 == gpio_value) {
		seq_printf(s, "enable (%d)(%d)\n", GPIO_FACTORY_USB_ENABLE, gpio_value);
	} else {
		seq_printf(s, "unknown (%d)(%d)\n", GPIO_FACTORY_USB_ENABLE, gpio_value);
	}

	return 0;
}

static int msm_otg_gpio_factory_usb_enable_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_gpio_factory_usb_enable_show, inode->i_private);
}

static ssize_t msm_otg_gpio_factory_usb_enable_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];
	int status = count;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "0", 1)) {
		gpio_direction_output(GPIO_FACTORY_USB_ENABLE, 0);
	} else if (!strncmp(buf, "1", 1)) {
		gpio_direction_output(GPIO_FACTORY_USB_ENABLE, 1);
	} else {
		status = -EINVAL;
		goto out;
	}
out:
	return status;
}

const struct file_operations msm_otg_gpio_factory_usb_enable_fops = {
	.open = msm_otg_gpio_factory_usb_enable_open,
	.read = seq_read,
	.write = msm_otg_gpio_factory_usb_enable_write,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif
//ASUS_BSP--- BennyCheng "implement factory usb mode"

#if 0
static int msm_otg_mode_show(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;
	struct otg_transceiver *otg = &motg->otg;

	switch (otg->state) {
	case OTG_STATE_A_HOST:
		seq_printf(s, "host\n");
		break;
	case OTG_STATE_B_PERIPHERAL:
		seq_printf(s, "peripheral\n");
		break;
	default:
		seq_printf(s, "none\n");
		break;
	}

	return 0;
}

static int msm_otg_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_mode_show, inode->i_private);
}

static ssize_t msm_otg_mode_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct msm_otg *motg = s->private;
	char buf[16];
	struct otg_transceiver *otg = &motg->otg;
	int status = count;
	enum usb_mode_type req_mode;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "host", 4)) {
		req_mode = USB_HOST;
	} else if (!strncmp(buf, "peripheral", 10)) {
		req_mode = USB_PERIPHERAL;
	} else if (!strncmp(buf, "none", 4)) {
		req_mode = USB_NONE;
	} else {
		status = -EINVAL;
		goto out;
	}

	switch (req_mode) {
	case USB_NONE:
		switch (otg->state) {
		case OTG_STATE_A_HOST:
		case OTG_STATE_B_PERIPHERAL:
			set_bit(ID, &motg->inputs);
			clear_bit(B_SESS_VLD, &motg->inputs);
			break;
		default:
			goto out;
		}
		break;
	case USB_PERIPHERAL:
		switch (otg->state) {
		case OTG_STATE_B_IDLE:
		case OTG_STATE_A_HOST:
			set_bit(ID, &motg->inputs);
			set_bit(B_SESS_VLD, &motg->inputs);
			break;
		default:
			goto out;
		}
		break;
	case USB_HOST:
		switch (otg->state) {
		case OTG_STATE_B_IDLE:
		case OTG_STATE_B_PERIPHERAL:
			clear_bit(ID, &motg->inputs);
			break;
		default:
			goto out;
		}
		break;
	default:
		goto out;
	}

	pm_runtime_resume(otg->dev);
	queue_work(system_nrt_wq, &motg->sm_work);
out:
	return status;
}
#endif

const struct file_operations msm_otg_mode_fops = {
	.open = msm_otg_mode_open,
	.read = seq_read,
	.write = msm_otg_mode_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_show_otg_state(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;
	struct otg_transceiver *otg = &motg->otg;

	seq_printf(s, "%s\n", otg_state_string(otg->state));
	return 0;
}

static int msm_otg_otg_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_show_otg_state, inode->i_private);
}

const struct file_operations msm_otg_state_fops = {
	.open = msm_otg_otg_state_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_show_chg_type(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;

	seq_printf(s, "%s\n", chg_to_string(motg->chg_type));
	return 0;
}

static int msm_otg_chg_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_show_chg_type, inode->i_private);
}

const struct file_operations msm_otg_chg_fops = {
	.open = msm_otg_chg_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_aca_show(struct seq_file *s, void *unused)
{
	if (debug_aca_enabled)
		seq_printf(s, "enabled\n");
	else
		seq_printf(s, "disabled\n");

	return 0;
}

static int msm_otg_aca_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_aca_show, inode->i_private);
}

static ssize_t msm_otg_aca_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[8];

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "enable", 6))
		debug_aca_enabled = true;
	else
		debug_aca_enabled = false;

	return count;
}

const struct file_operations msm_otg_aca_fops = {
	.open = msm_otg_aca_open,
	.read = seq_read,
	.write = msm_otg_aca_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_bus_show(struct seq_file *s, void *unused)
{
	if (debug_bus_voting_enabled)
		seq_printf(s, "enabled\n");
	else
		seq_printf(s, "disabled\n");

	return 0;
}

static int msm_otg_bus_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_bus_show, inode->i_private);
}

static ssize_t msm_otg_bus_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[8];
	int ret;
	struct seq_file *s = file->private_data;
	struct msm_otg *motg = s->private;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (!strncmp(buf, "enable", 6)) {
		/* Do not vote here. Let OTG statemachine decide when to vote */
		debug_bus_voting_enabled = true;
	} else {
		debug_bus_voting_enabled = false;
		if (motg->bus_perf_client) {
			ret = msm_bus_scale_client_update_request(
					motg->bus_perf_client, 0);
			if (ret)
				dev_err(motg->otg.dev, "%s: Failed to devote "
					   "for bus bw %d\n", __func__, ret);
		}
	}

	return count;
}

const struct file_operations msm_otg_bus_fops = {
	.open = msm_otg_bus_open,
	.read = seq_read,
	.write = msm_otg_bus_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct dentry *msm_otg_dbg_root;

static int msm_otg_debugfs_init(struct msm_otg *motg)
{
	struct dentry *msm_otg_dentry;

	msm_otg_dbg_root = debugfs_create_dir("msm_otg", NULL);

	if (!msm_otg_dbg_root || IS_ERR(msm_otg_dbg_root))
		return -ENODEV;

	//ASUS_BSP+++ BennyCheng "enable otg debugfs"
	if (motg->pdata->mode == USB_OTG) {
	//ASUS_BSP--- BennyCheng "enable otg debugfs"

		msm_otg_dentry = debugfs_create_file("mode", S_IRUGO |
			S_IWUSR, msm_otg_dbg_root, motg,
			&msm_otg_mode_fops);

		if (!msm_otg_dentry) {
			debugfs_remove(msm_otg_dbg_root);
			msm_otg_dbg_root = NULL;
			return -ENODEV;
		}
	}

	msm_otg_dentry = debugfs_create_file("chg_type", S_IRUGO,
		msm_otg_dbg_root, motg,
		&msm_otg_chg_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("aca", S_IRUGO | S_IWUSR,
		msm_otg_dbg_root, motg,
		&msm_otg_aca_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("bus_voting", S_IRUGO | S_IWUSR,
		msm_otg_dbg_root, motg,
		&msm_otg_bus_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("otg_state", S_IRUGO,
				msm_otg_dbg_root, motg, &msm_otg_state_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
	msm_otg_dentry = debugfs_create_file("hub", S_IRUGO |
		S_IWUSR, msm_otg_dbg_root, motg,
		&msm_otg_pad_hub_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("camera", S_IRUGO |
		S_IWUSR, msm_otg_dbg_root, motg,
		&msm_otg_pad_camera_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("hub_pm", S_IRUGO |
		S_IWUSR, msm_otg_dbg_root, motg,
		&msm_otg_pad_hub_pm_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("gpio_apq_mdm_sw", S_IRUGO |
		S_IWUSR, msm_otg_dbg_root, motg,
		&msm_otg_gpio_apq_mdm_sw_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}

	msm_otg_dentry = debugfs_create_file("gpio_usb_mhl_sw", S_IRUGO |
		S_IWUSR, msm_otg_dbg_root, motg,
		&msm_otg_gpio_usb_mhl_sw_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}
	//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"

	//ASUS_BSP+++ BennyCheng "implement factory usb mode"
#ifdef ASUS_FACTORY_BUILD
	msm_otg_dentry = debugfs_create_file("gpio_factory_usb_enable", S_IRUGO |
		S_IWUSR, msm_otg_dbg_root, motg,
		&msm_otg_gpio_factory_usb_enable_fops);

	if (!msm_otg_dentry) {
		debugfs_remove_recursive(msm_otg_dbg_root);
		return -ENODEV;
	}
#endif
	//ASUS_BSP--- BennyCheng "implement factory usb mode"

	return 0;
}

static void msm_otg_debugfs_cleanup(void)
{
	debugfs_remove_recursive(msm_otg_dbg_root);
}

//ASUS_BSP+++ BennyCheng "add proc debug files"
static int msm_otg_proc_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_mode_show, PDE(inode)->data);
}

const struct file_operations msm_otg_proc_mode_fops = {
	.open = msm_otg_proc_mode_open,
	.read = seq_read,
	.write = msm_otg_mode_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static int msm_otg_proc_apq_mdm_sw_show(struct seq_file *s, void *unused)
{
	struct msm_otg *motg = s->private;
	int apq_mdm_sw_gpio_value = 0;
	int usb_mhl_sw_gpio_value = 0;

	if (g_A68_hwID == A68_EVB || g_A68_hwID == A68_UNKNOWN) {
		dev_err(motg->otg.dev, "not support for the HW!(%d)\n", g_A68_hwID);
		return -EPERM;
	}

	apq_mdm_sw_gpio_value = gpio_get_value(GPIO_APQ_MDM_SW_SEL);
	usb_mhl_sw_gpio_value = gpio_get_value(GPIO_USB_MHL_SW_SEL);

	if (1 == apq_mdm_sw_gpio_value) {
		seq_printf(s, "apq (%d)\n", apq_mdm_sw_gpio_value);
	} else if (0 == apq_mdm_sw_gpio_value) {
		seq_printf(s, "mdm (%d)\n", apq_mdm_sw_gpio_value);
	} else {
		seq_printf(s, "unknown (%d)\n", apq_mdm_sw_gpio_value);
	}

	if (1 == usb_mhl_sw_gpio_value) {
		seq_printf(s, "mhl (%d)\n", usb_mhl_sw_gpio_value);
	} else if (0 == usb_mhl_sw_gpio_value) {
		seq_printf(s, "usb (%d)\n", usb_mhl_sw_gpio_value);
	} else {
		seq_printf(s, "unknown (%d)\n", usb_mhl_sw_gpio_value);
	}

	return 0;
}

static int msm_otg_proc_apq_mdm_sw_open(struct inode *inode, struct file *file)
{
	return single_open(file, msm_otg_proc_apq_mdm_sw_show, PDE(inode)->data);
}

static ssize_t msm_otg_proc_apq_mdm_sw_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct msm_otg *motg = s->private;
	char buf[16];
	int status = count;

	if (g_A68_hwID == A68_EVB || g_A68_hwID == A68_UNKNOWN) {
		dev_err(motg->otg.dev, "not support for the HW!(%d)\n", g_A68_hwID);
		return -EPERM;
	}

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count))) {
		status = -EFAULT;
		goto out;
	}

	if (!strncmp(buf, "apq", 3)) {
		msm_otg_apq_mdm_switch(USB_APQ);
		msm_otg_usb_mhl_switch(MHL_PORT);
	} else if (!strncmp(buf, "mdm", 3)) {
		msm_otg_apq_mdm_switch(USB_MDM);
		msm_otg_usb_mhl_switch(USB_PORT);
	} else {
		status = -EINVAL;
		goto out;
	}
out:
	return status;
}

const struct file_operations msm_otg_proc_apq_mdm_sw_fops = {
	.open = msm_otg_proc_apq_mdm_sw_open,
	.read = seq_read,
	.write = msm_otg_proc_apq_mdm_sw_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct proc_dir_entry *msm_otg_proc_root;

static int msm_otg_proc_init(struct msm_otg *motg)
{
	struct proc_dir_entry *proc_entry;

	msm_otg_proc_root = proc_mkdir("msm_otg", NULL);
	if (!msm_otg_proc_root) {
		return -ENODEV;
	}

	proc_entry = proc_create_data("mode", S_IRUGO |S_IWUSR, msm_otg_proc_root,
			&msm_otg_proc_mode_fops, motg);
	if (!proc_entry) {
		remove_proc_entry("mode", msm_otg_proc_root);
		msm_otg_proc_root = NULL;
		return -ENODEV;
	}

	proc_entry = proc_create_data("apq_mdm_sw", S_IRUGO |S_IWUSR, msm_otg_proc_root,
			&msm_otg_proc_apq_mdm_sw_fops, motg);
	if (!proc_entry) {
		remove_proc_entry("apq_mdm_sw", msm_otg_proc_root);
		msm_otg_proc_root = NULL;
		return -ENODEV;
	}

	return 0;
}

static void msm_otg_proc_cleanup(void)
{
	remove_proc_entry("mode", msm_otg_proc_root);
	remove_proc_entry("apq_mdm_sw", msm_otg_proc_root);
}
//ASUS_BSP--- BennyCheng "add proc debug files"

static u64 msm_otg_dma_mask = DMA_BIT_MASK(64);
static struct platform_device *msm_otg_add_pdev(
		struct platform_device *ofdev, const char *name)
{
	struct platform_device *pdev;
	const struct resource *res = ofdev->resource;
	unsigned int num = ofdev->num_resources;
	int retval;

	pdev = platform_device_alloc(name, -1);
	if (!pdev) {
		retval = -ENOMEM;
		goto error;
	}

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	pdev->dev.dma_mask = &msm_otg_dma_mask;

	if (num) {
		retval = platform_device_add_resources(pdev, res, num);
		if (retval)
			goto error;
	}

	retval = platform_device_add(pdev);
	if (retval)
		goto error;

	return pdev;

error:
	platform_device_put(pdev);
	return ERR_PTR(retval);
}

static int msm_otg_setup_devices(struct platform_device *ofdev,
		enum usb_mode_type mode, bool init)
{
	const char *gadget_name = "msm_hsusb";
	const char *host_name = "msm_hsusb_host";
	static struct platform_device *gadget_pdev;
	static struct platform_device *host_pdev;
	int retval = 0;

	if (!init) {
		if (gadget_pdev)
			platform_device_unregister(gadget_pdev);
		if (host_pdev)
			platform_device_unregister(host_pdev);
		return 0;
	}

	switch (mode) {
	case USB_OTG:
		/* fall through */
	case USB_PERIPHERAL:
		gadget_pdev = msm_otg_add_pdev(ofdev, gadget_name);
		if (IS_ERR(gadget_pdev)) {
			retval = PTR_ERR(gadget_pdev);
			break;
		}
		if (mode == USB_PERIPHERAL)
			break;
		/* fall through */
	case USB_HOST:
		host_pdev = msm_otg_add_pdev(ofdev, host_name);
		if (IS_ERR(host_pdev)) {
			retval = PTR_ERR(host_pdev);
			if (mode == USB_OTG)
				platform_device_unregister(gadget_pdev);
		}
		break;
	default:
		break;
	}

	return retval;
}

struct msm_otg_platform_data *msm_otg_dt_to_pdata(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct msm_otg_platform_data *pdata;
	int len = 0;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		pr_err("unable to allocate platform data\n");
		return NULL;
	}
	of_get_property(node, "qcom,hsusb-otg-phy-init-seq", &len);
	if (len) {
		pdata->phy_init_seq = devm_kzalloc(&pdev->dev, len, GFP_KERNEL);
		if (!pdata->phy_init_seq)
			return NULL;
		of_property_read_u32_array(node, "qcom,hsusb-otg-phy-init-seq",
				pdata->phy_init_seq,
				len/sizeof(*pdata->phy_init_seq));
	}
	of_property_read_u32(node, "qcom,hsusb-otg-power-budget",
				&pdata->power_budget);
	of_property_read_u32(node, "qcom,hsusb-otg-mode",
				&pdata->mode);
	of_property_read_u32(node, "qcom,hsusb-otg-otg-control",
				&pdata->otg_control);
	of_property_read_u32(node, "qcom,hsusb-otg-default-mode",
				&pdata->default_mode);
	of_property_read_u32(node, "qcom,hsusb-otg-phy-type",
				&pdata->phy_type);
	of_property_read_u32(node, "qcom,hsusb-otg-pmic-id-irq",
				&pdata->pmic_id_irq);
	return pdata;
}

//ASUS_BSP+++ BennyCheng "implement factory usb mode"
#ifdef ASUS_FACTORY_BUILD
static irqreturn_t msm_otg_factory_usb_mode_change(int irq, void *dev_id)
{
	int value;

	value = gpio_get_value(GPIO_FACTORY_USB_ENABLE);

	printk("[usb_otg] factory usb enable change interrupt (%d)\n", value);

	if (value == 0) {
		printk("[usb_otg] enable factory usb mode\n");
		msm_otg_apq_mdm_switch(USB_MDM);
		msm_otg_usb_mhl_switch(MHL_PORT);
	} else if (value == 1) {
		printk("[usb_otg] disable factory usb mode\n");
		msm_otg_apq_mdm_switch(USB_MDM);
		msm_otg_usb_mhl_switch(USB_PORT);
	}

	return IRQ_HANDLED;
}
#endif
//ASUS_BSP--- BennyCheng "implement factory usb mode"

static int __init msm_otg_probe(struct platform_device *pdev)
{
	int ret = 0;
	//ASUS_BSP+++ BennyCheng "implement factory usb mode"
#ifdef ASUS_FACTORY_BUILD
	int irq;
#endif
	//ASUS_BSP--- BennyCheng "implement factory usb mode"
	struct resource *res;
	struct msm_otg *motg;
	struct otg_transceiver *otg;
	struct msm_otg_platform_data *pdata;

	dev_info(&pdev->dev, "msm_otg probe\n");

	if (pdev->dev.of_node) {
		dev_dbg(&pdev->dev, "device tree enabled\n");
		pdata = msm_otg_dt_to_pdata(pdev);
		if (!pdata)
			return -ENOMEM;
		ret = msm_otg_setup_devices(pdev, pdata->mode, true);
		if (ret) {
			dev_err(&pdev->dev, "devices setup failed\n");
			return ret;
		}
	} else if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "No platform data given. Bailing out\n");
		return -ENODEV;
	} else {
		pdata = pdev->dev.platform_data;
	}

	motg = kzalloc(sizeof(struct msm_otg), GFP_KERNEL);
	if (!motg) {
		dev_err(&pdev->dev, "unable to allocate msm_otg\n");
		return -ENOMEM;
	}

	the_msm_otg = motg;
	motg->pdata = pdata;
	otg = &motg->otg;
	otg->dev = &pdev->dev;

	/*
	 * ACA ID_GND threshold range is overlapped with OTG ID_FLOAT.  Hence
	 * PHY treat ACA ID_GND as float and no interrupt is generated.  But
	 * PMIC can detect ACA ID_GND and generate an interrupt.
	 */
	if (aca_enabled() && motg->pdata->otg_control != OTG_PMIC_CONTROL) {
		dev_err(&pdev->dev, "ACA can not be enabled without PMIC\n");
		ret = -EINVAL;
		goto free_motg;
	}

	/* initialize reset counter */
	motg->reset_counter = 0;

	/* Some targets don't support PHY clock. */
	motg->phy_reset_clk = clk_get(&pdev->dev, "phy_clk");
	if (IS_ERR(motg->phy_reset_clk))
		dev_err(&pdev->dev, "failed to get phy_clk\n");

	/*
	 * Targets on which link uses asynchronous reset methodology,
	 * free running clock is not required during the reset.
	 */
	motg->clk = clk_get(&pdev->dev, "alt_core_clk");
	if (IS_ERR(motg->clk))
		dev_dbg(&pdev->dev, "alt_core_clk is not present\n");
	else
		clk_set_rate(motg->clk, 60000000);

	/*
	 * USB Core is running its protocol engine based on CORE CLK,
	 * CORE CLK  must be running at >55Mhz for correct HSUSB
	 * operation and USB core cannot tolerate frequency changes on
	 * CORE CLK. For such USB cores, vote for maximum clk frequency
	 * on pclk source
	 */
	motg->core_clk = clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(motg->core_clk)) {
		motg->core_clk = NULL;
		dev_err(&pdev->dev, "failed to get core_clk\n");
		ret = PTR_ERR(motg->core_clk);
		goto put_clk;
	}
	clk_set_rate(motg->core_clk, INT_MAX);

	motg->pclk = clk_get(&pdev->dev, "iface_clk");
	if (IS_ERR(motg->pclk)) {
		dev_err(&pdev->dev, "failed to get iface_clk\n");
		ret = PTR_ERR(motg->pclk);
		goto put_core_clk;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "failed to get platform resource mem\n");
		ret = -ENODEV;
		goto put_pclk;
	}

	motg->regs = ioremap(res->start, resource_size(res));
	if (!motg->regs) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto put_pclk;
	}
	dev_info(&pdev->dev, "OTG regs = %p\n", motg->regs);

	motg->irq = platform_get_irq(pdev, 0);
	if (!motg->irq) {
		dev_err(&pdev->dev, "platform_get_irq failed\n");
		ret = -ENODEV;
		goto free_regs;
	}

	motg->xo_handle = msm_xo_get(MSM_XO_TCXO_D0, "usb");
	if (IS_ERR(motg->xo_handle)) {
		dev_err(&pdev->dev, "%s not able to get the handle "
			"to vote for TCXO D0 buffer\n", __func__);
		ret = PTR_ERR(motg->xo_handle);
		goto free_regs;
	}

	ret = msm_xo_mode_vote(motg->xo_handle, MSM_XO_MODE_ON);
	if (ret) {
		dev_err(&pdev->dev, "%s failed to vote for TCXO "
			"D0 buffer%d\n", __func__, ret);
		goto free_xo_handle;
	}

	clk_prepare_enable(motg->pclk);

	motg->vdd_type = VDDCX_CORNER;
	hsusb_vddcx = devm_regulator_get(motg->otg.dev, "hsusb_vdd_dig");
	if (IS_ERR(hsusb_vddcx)) {
		hsusb_vddcx = devm_regulator_get(motg->otg.dev, "HSUSB_VDDCX");
		if (IS_ERR(hsusb_vddcx)) {
			dev_err(motg->otg.dev, "unable to get hsusb vddcx\n");
			goto devote_xo_handle;
		}
		motg->vdd_type = VDDCX;
	}

	ret = msm_hsusb_config_vddcx(1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vddcx configuration failed\n");
		goto devote_xo_handle;
	}

	ret = regulator_enable(hsusb_vddcx);
	if (ret) {
		dev_err(&pdev->dev, "unable to enable the hsusb vddcx\n");
		goto free_config_vddcx;
	}

	ret = msm_hsusb_ldo_init(motg, 1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vreg configuration failed\n");
		goto free_hsusb_vddcx;
	}

	if (pdata->mhl_enable) {
		mhl_analog_switch = devm_regulator_get(motg->otg.dev,
							"mhl_ext_3p3v");
		if (IS_ERR(mhl_analog_switch)) {
			dev_err(&pdev->dev, "Unable to get mhl_analog_switch\n");
			goto free_ldo_init;
		}
	}

	ret = msm_hsusb_ldo_enable(motg, 1);
	if (ret) {
		dev_err(&pdev->dev, "hsusb vreg enable failed\n");
		goto free_ldo_init;
	}
	clk_prepare_enable(motg->core_clk);

	writel(0, USB_USBINTR);
	writel(0, USB_OTGSC);
	/* Ensure that above STOREs are completed before enabling interrupts */
	mb();

	wake_lock_init(&motg->wlock, WAKE_LOCK_SUSPEND, "msm_otg");
	msm_otg_init_timer(motg);
	INIT_WORK(&motg->sm_work, msm_otg_sm_work);
	INIT_DELAYED_WORK(&motg->chg_work, msm_chg_detect_work);
	INIT_DELAYED_WORK(&motg->pmic_id_status_work, msm_pmic_id_status_w);
	//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
	if (!early_suspend_delay_wq)
		early_suspend_delay_wq = create_singlethread_workqueue("msm_otg_early_suspend_delay_wq");
	wake_lock_init(&early_suspend_wlock, WAKE_LOCK_SUSPEND, "msm_otg_early_suspend_wlock");
	INIT_DELAYED_WORK_DEFERRABLE(&early_suspend_delay_work, msm_otg_microp_sleep_delay_work);

	if (!microp_cb_delay_wq)
		microp_cb_delay_wq = create_singlethread_workqueue("msm_otg_microp_cb_delay_wq");
	INIT_DELAYED_WORK_DEFERRABLE(&microp_cb_delay_work, msm_otg_microp_cb_delay_work);
	INIT_WORK(&late_resume_work, msm_otg_late_resume_work);
	//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"

	//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
	#ifdef CONFIG_CHARGER_ASUS
	INIT_WORK(&asus_usb_work, asus_usb_detect_work);
	INIT_DELAYED_WORK(&asus_chg_work, asus_chg_detect_work);
	#endif
	//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"

	setup_timer(&motg->id_timer, msm_otg_id_timer_func,
				(unsigned long) motg);
	ret = request_irq(motg->irq, msm_otg_irq, IRQF_SHARED,
					"msm_otg", motg);
	if (ret) {
		dev_err(&pdev->dev, "request irq failed\n");
		goto destroy_wlock;
	}

	otg->init = msm_otg_reset;
	otg->set_host = msm_otg_set_host;
	otg->set_peripheral = msm_otg_set_peripheral;
	otg->set_power = msm_otg_set_power;
	otg->start_hnp = msm_otg_start_hnp;
	otg->start_srp = msm_otg_start_srp;
	otg->set_suspend = msm_otg_set_suspend;
	
	if (pdata->otg_control == OTG_PHY_CONTROL && pdata->mpm_otgsessvld_int)
		msm_mpm_enable_pin(pdata->mpm_otgsessvld_int, 1);

	otg->io_ops = &msm_otg_io_ops;

	ret = otg_set_transceiver(&motg->otg);
	if (ret) {
		dev_err(&pdev->dev, "otg_set_transceiver failed\n");
		goto free_irq;
	}

	if (motg->pdata->mode == USB_OTG &&
		motg->pdata->otg_control == OTG_PMIC_CONTROL) {
		if (motg->pdata->pmic_id_irq) {
			ret = request_irq(motg->pdata->pmic_id_irq,
						msm_pmic_id_irq,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING,
						"msm_otg", motg);
			if (ret) {
				dev_err(&pdev->dev, "request irq failed for PMIC ID\n");
				goto remove_otg;
			}
		} else {
			//ASUS_BSP+++ BennyCheng "not checking ID bit for switching host/client mode"
			//ret = -ENODEV;
			dev_err(&pdev->dev, "PMIC IRQ for ID notifications doesn't exist\n");
			//goto remove_otg;
			//ASUS_BSP--- BennyCheng "not checking ID bit for switching host/client mode"
		}
	}

	msm_hsusb_mhl_switch_enable(motg, 1);

	platform_set_drvdata(pdev, motg);
	device_init_wakeup(&pdev->dev, 1);
	motg->mA_port = IUNIT;

	//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
	gpio_request(GPIO_APQ_MDM_SW_SEL, "APQ_MDM_SW_SEL");
	gpio_request(GPIO_USB_MHL_SW_SEL, "USB_SW_SEL");
	//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"
	//ASUS_BSP+++ BennyCheng "implement factory usb mode"
#ifdef ASUS_FACTORY_BUILD
	gpio_request(GPIO_FACTORY_USB_ENABLE, "FACTORY_USB_ENABLE");
#endif
	//ASUS_BSP+++ BennyCheng "implement factory usb mode"

	ret = msm_otg_debugfs_init(motg);
	if (ret)
		dev_dbg(&pdev->dev, "mode debugfs file is"
			"not available\n");

	//ASUS_BSP+++ BennyCheng "add proc debug files"
	ret = msm_otg_proc_init(motg);
	if (ret) {
		dev_dbg(&pdev->dev, "proc file init fail (%d)\n", ret);
	}
	//ASUS_BSP--- BennyCheng "add proc debug files"

	if (motg->pdata->otg_control == OTG_PMIC_CONTROL){
//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
#ifdef CONFIG_CHARGER_ASUS
		registerChargerInOutNotificaition(&msm_otg_set_vbus_state);
#else
		pm8921_charger_register_vbus_sn(&msm_otg_set_vbus_state);
#endif
//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"
	}

	if (motg->pdata->phy_type == SNPS_28NM_INTEGRATED_PHY) {
		//ASUS_BSP+++ BennyCheng "not checking ID bit for switching host/client mode"
		if (motg->pdata->otg_control == OTG_PMIC_CONTROL)
		//ASUS_BSP--- BennyCheng "not checking ID bit for switching host/client mode"
			motg->caps = ALLOW_PHY_POWER_COLLAPSE |
				ALLOW_PHY_RETENTION;

		if (motg->pdata->otg_control == OTG_PHY_CONTROL)
			motg->caps = ALLOW_PHY_RETENTION;
	}

	if (motg->pdata->enable_lpm_on_dev_suspend)
		motg->caps |= ALLOW_LPM_ON_DEV_SUSPEND;
	else
		motg->caps &= ~ALLOW_LPM_ON_DEV_SUSPEND;

	//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
	motg->otg_mode = USB_AUTO;
#ifdef CONFIG_EEPROM_NUVOTON
	register_microp_notifier(&usb_otg_microp_notifier);
#endif
	register_early_suspend(&usb_pad_hub_early_suspend_handler);
	//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"

	wake_lock(&motg->wlock);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	if (motg->pdata->bus_scale_table) {
		motg->bus_perf_client =
		    msm_bus_scale_register_client(motg->pdata->bus_scale_table);
		if (!motg->bus_perf_client)
			dev_err(motg->otg.dev, "%s: Failed to register BUS "
						"scaling client!!\n", __func__);
		else
			debug_bus_voting_enabled = true;
	}

	//ASUS_BSP+++ BennyCheng "add mutex protection when pm/runtime suspend"
	mutex_init(&msm_otg_mutex);
	//ASUS_BSP--- BennyCheng "add mutex protection when pm/runtime suspend"

	//ASUS_BSP+++ BennyCheng "implement factory usb mode"
#ifdef ASUS_FACTORY_BUILD
	irq = MSM_GPIO_TO_INT(GPIO_FACTORY_USB_ENABLE);
	if (irq < 0) {
		dev_err(motg->otg.dev, "%s: could not get FACTORY USB IRQ resource. "
			"error=%d No IRQ will be generated on factory_usb_enable.",
			__func__, irq);
	}
	ret = request_irq(irq, msm_otg_factory_usb_mode_change,
		IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING , "factory usb mode", NULL);

	if (ret < 0) {
		dev_err(motg->otg.dev, "%s: FACTORY USB IRQ#%d request failed with error=%d"
			". No IRQ will be generated on factory_usb_enable.",
			__func__, irq, ret);
	}
#endif
	//ASUS_BSP--- BennyCheng "implement factory usb mode"
	return 0;

remove_otg:
	otg_set_transceiver(NULL);
free_irq:
	free_irq(motg->irq, motg);
destroy_wlock:
	wake_lock_destroy(&motg->wlock);
	clk_disable_unprepare(motg->core_clk);
	msm_hsusb_ldo_enable(motg, 0);
free_ldo_init:
	msm_hsusb_ldo_init(motg, 0);
free_hsusb_vddcx:
	regulator_disable(hsusb_vddcx);
free_config_vddcx:
	regulator_set_voltage(hsusb_vddcx,
		vdd_val[motg->vdd_type][VDD_NONE],
		vdd_val[motg->vdd_type][VDD_MAX]);
devote_xo_handle:
	clk_disable_unprepare(motg->pclk);
	msm_xo_mode_vote(motg->xo_handle, MSM_XO_MODE_OFF);
free_xo_handle:
	msm_xo_put(motg->xo_handle);
free_regs:
	iounmap(motg->regs);
put_pclk:
	clk_put(motg->pclk);
put_core_clk:
	clk_put(motg->core_clk);
put_clk:
	if (!IS_ERR(motg->clk))
		clk_put(motg->clk);
	if (!IS_ERR(motg->phy_reset_clk))
		clk_put(motg->phy_reset_clk);
free_motg:
	kfree(motg);
	return ret;
}

static int __devexit msm_otg_remove(struct platform_device *pdev)
{
	struct msm_otg *motg = platform_get_drvdata(pdev);
	struct otg_transceiver *otg = &motg->otg;
	int cnt = 0;

	if (otg->host || otg->gadget)
		return -EBUSY;

	if (pdev->dev.of_node)
		msm_otg_setup_devices(pdev, motg->pdata->mode, false);
	if (motg->pdata->otg_control == OTG_PMIC_CONTROL)
		pm8921_charger_unregister_vbus_sn(0);
	msm_otg_debugfs_cleanup();
	//ASUS_BSP+++ BennyCheng "add proc debug files"
	msm_otg_proc_cleanup();
	//ASUS_BSP--- BennyCheng "add proc debug files"

	//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
	gpio_free(GPIO_APQ_MDM_SW_SEL);
	gpio_free(GPIO_USB_MHL_SW_SEL);
	cancel_delayed_work_sync(&early_suspend_delay_work);
	wake_lock_destroy(&early_suspend_wlock);
	destroy_workqueue(early_suspend_delay_wq);
	cancel_delayed_work_sync(&microp_cb_delay_work);
	destroy_workqueue(microp_cb_delay_wq);
	cancel_work_sync(&late_resume_work);
	//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"
	//ASUS_BSP+++ BennyCheng "implement factory usb mode"
#ifdef ASUS_FACTORY_BUILD
	gpio_free(GPIO_FACTORY_USB_ENABLE);
#endif
	//ASUS_BSP--- BennyCheng "implement factory usb mode"

	cancel_delayed_work_sync(&motg->chg_work);
	cancel_delayed_work_sync(&motg->pmic_id_status_work);

	//ASUS_BSP+++ "[USB][NA][Spec] Add ASUS charger mode support"
	#ifdef CONFIG_CHARGER_ASUS
	cancel_delayed_work_sync(&asus_chg_work);
	#endif
	//ASUS_BSP--- "[USB][NA][Spec] Add ASUS charger mode support"

	cancel_work_sync(&motg->sm_work);

	pm_runtime_resume(&pdev->dev);

	device_init_wakeup(&pdev->dev, 0);
	pm_runtime_disable(&pdev->dev);
	wake_lock_destroy(&motg->wlock);

	msm_hsusb_mhl_switch_enable(motg, 0);
	if (motg->pdata->pmic_id_irq)
		free_irq(motg->pdata->pmic_id_irq, motg);
	otg_set_transceiver(NULL);
	free_irq(motg->irq, motg);

	if (motg->pdata->otg_control == OTG_PHY_CONTROL &&
		motg->pdata->mpm_otgsessvld_int)
		msm_mpm_enable_pin(motg->pdata->mpm_otgsessvld_int, 0);

	/*
	 * Put PHY in low power mode.
	 */
	ulpi_read(otg, 0x14);
	ulpi_write(otg, 0x08, 0x09);

	writel(readl(USB_PORTSC) | PORTSC_PHCD, USB_PORTSC);
	while (cnt < PHY_SUSPEND_TIMEOUT_USEC) {
		if (readl(USB_PORTSC) & PORTSC_PHCD)
			break;
		udelay(1);
		cnt++;
	}
	if (cnt >= PHY_SUSPEND_TIMEOUT_USEC)
		dev_err(otg->dev, "Unable to suspend PHY\n");

	clk_disable_unprepare(motg->pclk);
	clk_disable_unprepare(motg->core_clk);
	msm_xo_put(motg->xo_handle);
	msm_hsusb_ldo_enable(motg, 0);
	msm_hsusb_ldo_init(motg, 0);
	regulator_disable(hsusb_vddcx);
	regulator_set_voltage(hsusb_vddcx,
		vdd_val[motg->vdd_type][VDD_NONE],
		vdd_val[motg->vdd_type][VDD_MAX]);

	iounmap(motg->regs);
	pm_runtime_set_suspended(&pdev->dev);

	if (!IS_ERR(motg->phy_reset_clk))
		clk_put(motg->phy_reset_clk);
	clk_put(motg->pclk);
	if (!IS_ERR(motg->clk))
		clk_put(motg->clk);
	clk_put(motg->core_clk);

	if (motg->bus_perf_client)
		msm_bus_scale_unregister_client(motg->bus_perf_client);

//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
#ifdef CONFIG_EEPROM_NUVOTON
	unregister_microp_notifier(&usb_otg_microp_notifier);
#endif
	unregister_early_suspend(&usb_pad_hub_early_suspend_handler);
//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"

	//ASUS_BSP+++ BennyCheng "add mutex protection when pm/runtime suspend"
	mutex_destroy(&msm_otg_mutex);
	//ASUS_BSP--- BennyCheng "add mutex protection when pm/runtime suspend"

	kfree(motg);
	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int msm_otg_runtime_idle(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);
	struct otg_transceiver *otg = &motg->otg;

	dev_dbg(dev, "OTG runtime idle\n");

	if (otg->state == OTG_STATE_UNDEFINED)
		return -EAGAIN;
	else
		return 0;
}

static int msm_otg_runtime_suspend(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);

	dev_dbg(dev, "OTG runtime suspend\n");
	return msm_otg_suspend(motg);
}

static int msm_otg_runtime_resume(struct device *dev)
{
	struct msm_otg *motg = dev_get_drvdata(dev);

	dev_dbg(dev, "OTG runtime resume\n");
	pm_runtime_get_noresume(dev);
	return msm_otg_resume(motg);
}
#endif

#ifdef CONFIG_PM_SLEEP
static int msm_otg_pm_suspend(struct device *dev)
{
	int ret = 0;
	struct msm_otg *motg = dev_get_drvdata(dev);

	dev_dbg(dev, "OTG PM suspend\n");

	atomic_set(&motg->pm_suspended, 1);
	ret = msm_otg_suspend(motg);
	if (ret)
		atomic_set(&motg->pm_suspended, 0);

	//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
	if (pad_hub_pm_enable) {
		if(!test_bit(ID, &motg->inputs) && atomic_read(&motg->pm_suspended)) {
			msm_otg_host_pm_suspend(motg);
			dev_info(dev, "OTG Host PM suspend\n");
		}
	}
	//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"

	return ret;
}

static int msm_otg_pm_resume(struct device *dev)
{
	int ret = 0;
	struct msm_otg *motg = dev_get_drvdata(dev);

	dev_dbg(dev, "OTG PM resume\n");

	//ASUS_BSP+++ BennyCheng "add microp notification & padstation hub power control"
	if (pad_hub_pm_enable) {
		if(!test_bit(ID, &motg->inputs) && atomic_read(&motg->pm_suspended)) {
			msm_otg_host_pm_resume(motg);
			dev_info(dev, "OTG Host PM Resume\n");
		}
	}
	//ASUS_BSP--- BennyCheng "add microp notification & padstation hub power control"

	atomic_set(&motg->pm_suspended, 0);
	if (motg->sm_work_pending) {
		motg->sm_work_pending = false;

		pm_runtime_get_noresume(dev);
		ret = msm_otg_resume(motg);

		/* Update runtime PM status */
		pm_runtime_disable(dev);
		pm_runtime_set_active(dev);
		pm_runtime_enable(dev);

		queue_work(system_nrt_wq, &motg->sm_work);
	}

	return ret;
}
#endif

#ifdef CONFIG_PM
static const struct dev_pm_ops msm_otg_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(msm_otg_pm_suspend, msm_otg_pm_resume)
	SET_RUNTIME_PM_OPS(msm_otg_runtime_suspend, msm_otg_runtime_resume,
				msm_otg_runtime_idle)
};
#endif

static struct of_device_id msm_otg_dt_match[] = {
	{	.compatible = "qcom,hsusb-otg",
	},
	{}
};

static struct platform_driver msm_otg_driver = {
	.remove = __devexit_p(msm_otg_remove),
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &msm_otg_dev_pm_ops,
#endif
		.of_match_table = msm_otg_dt_match,
	},
};

static int __init msm_otg_init(void)
{
	return platform_driver_probe(&msm_otg_driver, msm_otg_probe);
}

static void __exit msm_otg_exit(void)
{
	platform_driver_unregister(&msm_otg_driver);
}

module_init(msm_otg_init);
module_exit(msm_otg_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MSM USB transceiver driver");

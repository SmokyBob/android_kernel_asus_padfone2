#include <linux/kernel.h>
#include <mach/gpiomux.h>

#include "a68_evb_gpio_pinmux.h"
#include "a68_sr1_1_gpio_pinmux.h"
#include "a68_sr1_2_gpio_pinmux.h"
#include "a68_sr2_gpio_pinmux.h"
#include "a68_er1_gpio_pinmux.h"
#include "a68_er2_gpio_pinmux.h"
#include "a68_er3_gpio_pinmux.h"
#include "a68_pr_gpio_pinmux.h"
#include "a68_mp_gpio_pinmux.h"

#define PM8921_GPIO_BASE        NR_GPIO_IRQS
#define PM8921_GPIO_PM_TO_SYS(pm_gpio)  (pm_gpio - 1 + PM8921_GPIO_BASE)
#define PM8921_MPP_BASE         (PM8921_GPIO_BASE + PM8921_NR_GPIOS)
#define PM8921_MPP_PM_TO_SYS(pm_gpio)   (pm_gpio - 1 + PM8921_MPP_BASE)
#define PM8921_IRQ_BASE         (NR_MSM_IRQS + NR_GPIO_IRQS)
unsigned g_GPIO_HDMI_5V_ENABLE;//Mickey+++
unsigned g_GPIO_P01_I2C_LS_OE;
unsigned g_GPIO_MIC2_BIAS_EN;
unsigned g_GPIO_SPK_AMP_EN;
unsigned g_GPIO_HS_PATH_EN;
unsigned g_GPIO_HOOK_DET;
unsigned g_GPIO_JACK_IN_DET;
unsigned g_GPIO_LCD_TE;
unsigned g_GPIO_8M_WP;
unsigned g_GPIO_CAM_8M_RST_N;
unsigned g_GPIO_FLASH_ENT;
//ASUS_BSP larry_lai : EVB/SR1_1 for MHL Reset
unsigned g_GPIO_MHL_RST_N;

int __init device_gpio_init(void)
{   
   switch (g_A68_hwID)
   {
        case A68_EVB:
               printk("a68 gpio config table = EVB\n");  
/*
                g_GPIO_HDMI_5V_ENABLE = 4;
                g_GPIO_P01_I2C_LS_OE = 3;
                g_GPIO_MIC2_BIAS_EN = 0;
                g_GPIO_SPK_AMP_EN = 46;
                g_GPIO_HS_PATH_EN = 18; 
                g_GPIO_HOOK_DET = 12; 
                g_GPIO_LCD_TE = 19;
                g_GPIO_JACK_IN_DET = (PM8921_GPIO_PM_TO_SYS(38));//Rice	 */

				g_GPIO_MHL_RST_N = (PM8921_GPIO_PM_TO_SYS(44)); /* ASUS_BSP larry_lai : EVB/SR1_1 for MHL Reset */
		   
               msm_gpiomux_install(a68_evb_msm8960_gpio_configs,
               ARRAY_SIZE(a68_evb_msm8960_gpio_configs));	 	  

               break;
         case A68_SR1_1:
                printk("a68 gpio config table = SR1_1\n");  

		g_GPIO_MHL_RST_N = 43;  /* ASUS_BSP larry_lai : EVB/SR1_1 for MHL Reset */
		   
		   msm_gpiomux_install(a68_sr1_1_msm8960_gpio_configs,
                ARRAY_SIZE(a68_sr1_1_msm8960_gpio_configs));	 	  
               break;
	    case A68_SR1_2:
                printk("a68 gpio config table = SR1_2\n");  

		g_GPIO_MHL_RST_N = 43;  /* ASUS_BSP larry_lai : EVB/SR1_1 for MHL Reset */
		   
		   msm_gpiomux_install(a68_sr1_2_msm8960_gpio_configs,
                ARRAY_SIZE(a68_sr1_2_msm8960_gpio_configs));	 	  
               break;
		case A68_SR2:
                printk("a68 gpio config table = SR2\n");  

		g_GPIO_MHL_RST_N = 43;  /* ASUS_BSP larry_lai : EVB/SR1_1 for MHL Reset */
		   
		   msm_gpiomux_install(a68_sr2_msm8960_gpio_configs,
                ARRAY_SIZE(a68_sr2_msm8960_gpio_configs));	 	  
               break;	
		case A68_ER1:
                printk("a68 gpio config table = ER1\n");  

		g_GPIO_MHL_RST_N = 43;  /* ASUS_BSP larry_lai : EVB/SR1_1 for MHL Reset */
		   
		   msm_gpiomux_install(a68_er1_msm8960_gpio_configs,
                ARRAY_SIZE(a68_er1_msm8960_gpio_configs));	 	  
               break;		
		case A68_ER2:
                printk("a68 gpio config table = ER2\n");  

		g_GPIO_MHL_RST_N = 43;  /* ASUS_BSP larry_lai : EVB/SR1_1 for MHL Reset */
		   
		   msm_gpiomux_install(a68_er2_msm8960_gpio_configs,
                ARRAY_SIZE(a68_er2_msm8960_gpio_configs));	 	  
               break;	
		case A68_ER3:
                printk("a68 gpio config table = ER3\n");  

		g_GPIO_MHL_RST_N = 43;  /* ASUS_BSP larry_lai : EVB/SR1_1 for MHL Reset */
		   
		   msm_gpiomux_install(a68_er3_msm8960_gpio_configs,
                ARRAY_SIZE(a68_er3_msm8960_gpio_configs));	 	  
               break;	
		case A68_PR:
                printk("a68 gpio config table = PR\n");  

		g_GPIO_MHL_RST_N = 43;  /* ASUS_BSP larry_lai : EVB/SR1_1 for MHL Reset */
		   
		   msm_gpiomux_install(a68_pr_msm8960_gpio_configs,
                ARRAY_SIZE(a68_pr_msm8960_gpio_configs));	 	  
               break;
		case A68_MP:
                printk("a68 gpio config table = MP\n");  

		g_GPIO_MHL_RST_N = 43;  /* ASUS_BSP larry_lai : EVB/SR1_1 for MHL Reset */
		   
		   msm_gpiomux_install(a68_mp_msm8960_gpio_configs,
                ARRAY_SIZE(a68_mp_msm8960_gpio_configs));	 	  
               break;
			   
		case A80_EVB:
		case A80_SR1:
		case A80_SR2:
		case A80_SR3:
		case A80_ER1:
		case A80_ER2:
		case A80_ER3:
		case A80_PR:
			printk("[BSP] A68 project is not yet!!!!\n");

	 default:
               printk(KERN_ERR "[ERROR] There is NO valid hardware ID\n");
			   g_GPIO_MHL_RST_N = 43;  /* ASUS_BSP larry_lai : EVB/SR1_1 for MHL Reset */
		   msm_gpiomux_install(a68_sr2_msm8960_gpio_configs,
               ARRAY_SIZE(a68_sr2_msm8960_gpio_configs));
               break;
   }
   return 0;
}




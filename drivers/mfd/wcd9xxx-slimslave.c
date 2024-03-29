/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/mfd/wcd9xxx/wcd9xxx-slimslave.h>

struct wcd9xxx_slim_sch_rx {
	u32 sph;
	u32 ch_num;
	u16 ch_h;
	u16 grph;
};

struct wcd9xxx_slim_sch_tx {
	u32 sph;
	u32 ch_num;
	u16 ch_h;
	u16 grph;
};

struct wcd9xxx_slim_sch {
	struct wcd9xxx_slim_sch_rx rx[SLIM_MAX_RX_PORTS];
	struct wcd9xxx_slim_sch_tx tx[SLIM_MAX_TX_PORTS];
};

static struct wcd9xxx_slim_sch sh_ch;

static int wcd9xxx_alloc_slim_sh_ch_rx(struct wcd9xxx *wcd9xxx,
					u8 wcd9xxx_pgd_la);
static int wcd9xxx_alloc_slim_sh_ch_tx(struct wcd9xxx *wcd9xxx,
					u8 wcd9xxx_pgd_la);
static int wcd9xxx_dealloc_slim_sh_ch_rx(struct wcd9xxx *wcd9xxx);
static int wcd9xxx_dealloc_slim_sh_ch_tx(struct wcd9xxx *wcd9xxx);

int wcd9xxx_init_slimslave(struct wcd9xxx *wcd9xxx, u8 wcd9xxx_pgd_la)
{
	int ret = 0;

	ret = wcd9xxx_alloc_slim_sh_ch_rx(wcd9xxx, wcd9xxx_pgd_la);
	if (ret) {
		pr_err("%s: Failed to alloc rx slimbus shared channels\n",
								__func__);
		goto rx_err;
	}
	ret = wcd9xxx_alloc_slim_sh_ch_tx(wcd9xxx, wcd9xxx_pgd_la);
	if (ret) {
		pr_err("%s: Failed to alloc tx slimbus shared channels\n",
								__func__);
		goto tx_err;
	}
	return 0;
tx_err:
	wcd9xxx_dealloc_slim_sh_ch_rx(wcd9xxx);
rx_err:
	return ret;
}


int wcd9xxx_deinit_slimslave(struct wcd9xxx *wcd9xxx)
{
	int ret = 0;
	ret = wcd9xxx_dealloc_slim_sh_ch_rx(wcd9xxx);
	if (ret < 0) {
		pr_err("%s: fail to dealloc rx slim ports\n", __func__);
		goto err;
	}
	ret = wcd9xxx_dealloc_slim_sh_ch_tx(wcd9xxx);
	if (ret < 0) {
		pr_err("%s: fail to dealloc tx slim ports\n", __func__);
		goto err;
	}
err:
	return ret;
}

int wcd9xxx_get_channel(struct wcd9xxx *wcd9xxx,
		unsigned int *rx_ch,
		unsigned int *tx_ch)
{
	int ch_idx = 0;
	struct wcd9xxx_slim_sch_rx *rx = sh_ch.rx;
	struct wcd9xxx_slim_sch_tx *tx = sh_ch.tx;

	for (ch_idx = 0; ch_idx < SLIM_MAX_RX_PORTS; ch_idx++)
		rx_ch[ch_idx] = rx[ch_idx].ch_num;
	for (ch_idx = 0; ch_idx < SLIM_MAX_TX_PORTS; ch_idx++)
		tx_ch[ch_idx] = tx[ch_idx].ch_num;
	return 0;
}

static int wcd9xxx_alloc_slim_sh_ch_rx(struct wcd9xxx *wcd9xxx,
			u8 wcd9xxx_pgd_la)
{
	int ret = 0;
	u8 ch_idx ;
	u16 slave_port_id = 0;
	struct wcd9xxx_slim_sch_rx *rx = sh_ch.rx;

	/*
	 * DSP requires channel number to be between 128 and 255.
	 */
	pr_debug("%s: pgd_la[%d]\n", __func__, wcd9xxx_pgd_la);
	for (ch_idx = 0; ch_idx < SLIM_MAX_RX_PORTS; ch_idx++) {
		slave_port_id = (ch_idx + 1 +
				SB_PGD_OFFSET_OF_RX_SLAVE_DEV_PORTS);
		rx[ch_idx].ch_num = slave_port_id + BASE_CH_NUM;
		ret = slim_get_slaveport(wcd9xxx_pgd_la, slave_port_id,
					&rx[ch_idx].sph, SLIM_SINK);
		if (ret < 0) {
			pr_err("%s: slave port failure id[%d] ret[%d]\n",
					__func__, slave_port_id, ret);
			goto err;
		}

		ret = slim_query_ch(wcd9xxx->slim, rx[ch_idx].ch_num,
							&rx[ch_idx].ch_h);
		if (ret < 0) {
			pr_err("%s: slim_query_ch failed ch-num[%d] ret[%d]\n",
					__func__, rx[ch_idx].ch_num, ret);
			goto err;
		}
	}
err:
	return ret;
}

static int wcd9xxx_alloc_slim_sh_ch_tx(struct wcd9xxx *wcd9xxx,
			u8 wcd9xxx_pgd_la)
{
	int ret = 0;
	u8 ch_idx ;
	struct wcd9xxx_slim_sch_tx *tx = sh_ch.tx;
	u16 slave_port_id = 0;

	pr_debug("%s: pgd_la[%d]\n", __func__, wcd9xxx_pgd_la);
	/* DSP requires channel number to be between 128 and 255. For RX port
	 * use channel numbers from 138 to 144, for TX port
	 * use channel numbers from 128 to 137
	 */
	for (ch_idx = 0; ch_idx < SLIM_MAX_TX_PORTS; ch_idx++) {
		slave_port_id = ch_idx;
		tx[ch_idx].ch_num = slave_port_id + BASE_CH_NUM;
		ret = slim_get_slaveport(wcd9xxx_pgd_la, slave_port_id,
					&tx[ch_idx].sph, SLIM_SRC);
		if (ret < 0) {
			pr_err("%s: slave port failure id[%d] ret[%d]\n",
					__func__, slave_port_id, ret);
			goto err;
		}
		ret = slim_query_ch(wcd9xxx->slim, tx[ch_idx].ch_num,
							&tx[ch_idx].ch_h);
		if (ret < 0) {
			pr_err("%s: slim_query_ch failed ch-num[%d] ret[%d]\n",
					__func__, tx[ch_idx].ch_num, ret);
			goto err;
		}
	}
err:
	return ret;
}

static int wcd9xxx_dealloc_slim_sh_ch_rx(struct wcd9xxx *wcd9xxx)
{
	int idx = 0;
	int ret = 0;
	struct wcd9xxx_slim_sch_rx *rx = sh_ch.rx;
	/* slim_dealloc_ch */
	for (idx = 0; idx < SLIM_MAX_RX_PORTS; idx++) {
		ret = slim_dealloc_ch(wcd9xxx->slim, rx[idx].ch_h);
		if (ret < 0) {
			pr_err("%s: slim_dealloc_ch fail ret[%d] ch_h[%d]\n",
				__func__, ret, rx[idx].ch_h);
		}
	}
	memset(sh_ch.rx, 0, sizeof(sh_ch.rx));
	return ret;
}

static int wcd9xxx_dealloc_slim_sh_ch_tx(struct wcd9xxx *wcd9xxx)
{
	int idx = 0;
	int ret = 0;
	struct wcd9xxx_slim_sch_tx *tx = sh_ch.tx;
	/* slim_dealloc_ch */
	for (idx = 0; idx < SLIM_MAX_TX_PORTS; idx++) {
		ret = slim_dealloc_ch(wcd9xxx->slim, tx[idx].ch_h);
		if (ret < 0) {
			pr_err("%s: slim_dealloc_ch fail ret[%d] ch_h[%d]\n",
				__func__, ret, tx[idx].ch_h);
		}
	}
	memset(sh_ch.tx, 0, sizeof(sh_ch.tx));
	return ret;
}

/* Enable slimbus slave device for RX path */
int wcd9xxx_cfg_slim_sch_rx(struct wcd9xxx *wcd9xxx, unsigned int *ch_num,
				unsigned int ch_cnt, unsigned int rate)
{
	u8 i = 0;
	u16 grph;
	u32 sph[SLIM_MAX_RX_PORTS] = {0};
	u16 ch_h[SLIM_MAX_RX_PORTS] = {0};
	u16 slave_port_id;
	u8  payload_rx = 0, wm_payload = 0;
	int ret, idx = 0;
	unsigned short  multi_chan_cfg_reg_addr;
	struct wcd9xxx_slim_sch_rx *rx = sh_ch.rx;
	struct slim_ch prop;

	/* Configure slave interface device */
	pr_debug("%s: ch_cnt[%d] rate=%d\n", __func__, ch_cnt, rate);

	for (i = 0; i < ch_cnt; i++) {
		idx = (ch_num[i] - BASE_CH_NUM -
			SB_PGD_OFFSET_OF_RX_SLAVE_DEV_PORTS - 1);
		ch_h[i] = rx[idx].ch_h;
		sph[i] = rx[idx].sph;
		slave_port_id = idx + 1;
		if ((slave_port_id > SB_PGD_MAX_NUMBER_OF_RX_SLAVE_DEV_PORTS) ||
			(slave_port_id == 0)) {
			pr_err("Slimbus: invalid slave port id: %d",
							slave_port_id);
			ret = -EINVAL;
			goto err;
		}
		slave_port_id += SB_PGD_OFFSET_OF_RX_SLAVE_DEV_PORTS;
		/* look for the valid port range and chose the
		 * payload accordingly
		 */
		if ((slave_port_id >
				SB_PGD_TX_PORT_MULTI_CHANNEL_1_END_PORT_ID) &&
			(slave_port_id <=
			 SB_PGD_RX_PORT_MULTI_CHANNEL_0_END_PORT_ID)) {
				payload_rx = payload_rx  |
				(1 <<
				(slave_port_id -
				SB_PGD_RX_PORT_MULTI_CHANNEL_0_START_PORT_ID));
		} else {
			ret = -EINVAL;
			goto err;
		}
		multi_chan_cfg_reg_addr =
				SB_PGD_RX_PORT_MULTI_CHANNEL_0(slave_port_id);
		/* write to interface device */
		ret = wcd9xxx_interface_reg_write(wcd9xxx,
				multi_chan_cfg_reg_addr,
				payload_rx);
		if (ret < 0) {
			pr_err("%s:Intf-dev fail reg[%d] payload[%d] ret[%d]\n",
							__func__,
							multi_chan_cfg_reg_addr,
							payload_rx, ret);
			goto err;
		}
		/* configure the slave port for water mark and enable*/
		wm_payload = (SLAVE_PORT_WATER_MARK_VALUE <<
				SLAVE_PORT_WATER_MARK_SHIFT) +
				SLAVE_PORT_ENABLE;
		ret = wcd9xxx_interface_reg_write(wcd9xxx,
				SB_PGD_PORT_CFG_BYTE_ADDR(slave_port_id),
				wm_payload);
		if (ret < 0) {
			pr_err("%s:watermark set failure for port[%d] ret[%d]",
						__func__, slave_port_id, ret);
		}
	}

	/* slim_define_ch api */
	prop.prot = SLIM_AUTO_ISO;
	prop.baser = SLIM_RATE_4000HZ;
	prop.dataf = SLIM_CH_DATAF_NOT_DEFINED;
	prop.auxf = SLIM_CH_AUXF_NOT_APPLICABLE;
	prop.ratem = (rate/4000);
	prop.sampleszbits = 16;

	ret = slim_define_ch(wcd9xxx->slim, &prop, ch_h, ch_cnt,
					true, &grph);
	if (ret < 0) {
		pr_err("%s: slim_define_ch failed ret[%d]\n",
					__func__, ret);
		goto err;
	}
	for (i = 0; i < ch_cnt; i++) {
		ret = slim_connect_sink(wcd9xxx->slim, &sph[i],
							1, ch_h[i]);
		if (ret < 0) {
			pr_err("%s: slim_connect_sink failed ret[%d]\n",
						__func__, ret);
			goto err_close_slim_sch;
		}
	}
	/* slim_control_ch */
	ret = slim_control_ch(wcd9xxx->slim, grph, SLIM_CH_ACTIVATE,
					true);
	if (ret < 0) {
		pr_err("%s: slim_control_ch failed ret[%d]\n",
				__func__, ret);
		goto err_close_slim_sch;
	}
	for (i = 0; i < ch_cnt; i++) {
		idx = (ch_num[i] - BASE_CH_NUM -
				SB_PGD_OFFSET_OF_RX_SLAVE_DEV_PORTS - 1);
		rx[idx].grph = grph;
	}
	return 0;

err_close_slim_sch:
	/*  release all acquired handles */
	wcd9xxx_close_slim_sch_rx(wcd9xxx, ch_num, ch_cnt);
err:
	return ret;
}
EXPORT_SYMBOL_GPL(wcd9xxx_cfg_slim_sch_rx);

/* Enable slimbus slave device for RX path */
int wcd9xxx_cfg_slim_sch_tx(struct wcd9xxx *wcd9xxx, unsigned int *ch_num,
				unsigned int ch_cnt, unsigned int rate)
{
	u8 i = 0;
	u8  payload_tx_0 = 0, payload_tx_1 = 0, wm_payload = 0;
	u16 grph;
	u32 sph[SLIM_MAX_TX_PORTS] = {0};
	u16 ch_h[SLIM_MAX_TX_PORTS] = {0};
	u16 idx = 0, slave_port_id;
	int ret = 0;
	unsigned short  multi_chan_cfg_reg_addr;

	struct wcd9xxx_slim_sch_tx *tx = sh_ch.tx;
	struct slim_ch prop;

	pr_debug("%s: ch_cnt[%d] rate[%d]\n", __func__, ch_cnt, rate);
	for (i = 0; i < ch_cnt; i++) {
		idx = (ch_num[i] - BASE_CH_NUM);
		ch_h[i] = tx[idx].ch_h;
		sph[i] = tx[idx].sph;
		slave_port_id = idx ;
		if (slave_port_id > SB_PGD_MAX_NUMBER_OF_TX_SLAVE_DEV_PORTS) {
			pr_err("SLIMbus: invalid slave port id: %d",
							slave_port_id);
			ret = -EINVAL;
			goto err;
		}
		/* look for the valid port range and chose the
		 *  payload accordingly
		 */
		if (slave_port_id <=
			SB_PGD_TX_PORT_MULTI_CHANNEL_0_END_PORT_ID) {
			payload_tx_0 = payload_tx_0 | (1 << slave_port_id);
		} else if (slave_port_id <=
				SB_PGD_TX_PORT_MULTI_CHANNEL_1_END_PORT_ID) {
				payload_tx_1 = payload_tx_1 |
				(1 <<
				(slave_port_id -
				SB_PGD_TX_PORT_MULTI_CHANNEL_1_START_PORT_ID));
		} else {
			ret = -EINVAL;
			goto err;
		}
		multi_chan_cfg_reg_addr =
				SB_PGD_TX_PORT_MULTI_CHANNEL_0(slave_port_id);
		/* write to interface device */
		ret = wcd9xxx_interface_reg_write(wcd9xxx,
				multi_chan_cfg_reg_addr,
				payload_tx_0);
		if (ret < 0) {
			pr_err("%s:Intf-dev fail reg[%d] payload[%d] ret[%d]\n",
								__func__,
						multi_chan_cfg_reg_addr,
						payload_tx_0, ret);
			goto err;
		}
		multi_chan_cfg_reg_addr =
				SB_PGD_TX_PORT_MULTI_CHANNEL_1(slave_port_id);
		/* ports 8,9 */
		ret = wcd9xxx_interface_reg_write(wcd9xxx,
				multi_chan_cfg_reg_addr,
				payload_tx_1);
		if (ret < 0) {
			pr_err("%s:Intf-dev fail reg[%d] payload[%d] ret[%d]\n",
								__func__,
						multi_chan_cfg_reg_addr,
						payload_tx_1, ret);
			goto err;
		}
		/* configure the slave port for water mark and enable*/
		wm_payload = (SLAVE_PORT_WATER_MARK_VALUE <<
				SLAVE_PORT_WATER_MARK_SHIFT) +
				SLAVE_PORT_ENABLE;
		ret = wcd9xxx_interface_reg_write(wcd9xxx,
				SB_PGD_PORT_CFG_BYTE_ADDR(slave_port_id),
				wm_payload);
		if (ret < 0) {
			pr_err("%s:watermark set failure for port[%d] ret[%d]",
						__func__,
						slave_port_id, ret);
		}
	}

	/* slim_define_ch api */
	prop.prot = SLIM_AUTO_ISO;
	prop.baser = SLIM_RATE_4000HZ;
	prop.dataf = SLIM_CH_DATAF_NOT_DEFINED;
	prop.auxf = SLIM_CH_AUXF_NOT_APPLICABLE;
	prop.ratem = (rate/4000);
	prop.sampleszbits = 16;
	ret = slim_define_ch(wcd9xxx->slim, &prop, ch_h, ch_cnt,
					true, &grph);
	if (ret < 0) {
		pr_err("%s: slim_define_ch failed ret[%d]\n",
					__func__, ret);
		goto err;
	}
	for (i = 0; i < ch_cnt; i++) {
		ret = slim_connect_src(wcd9xxx->slim, sph[i],
							ch_h[i]);
		if (ret < 0) {
			pr_err("%s: slim_connect_src failed ret[%d]\n",
						__func__, ret);
			goto err;
		}
	}
	/* slim_control_ch */
	ret = slim_control_ch(wcd9xxx->slim, grph, SLIM_CH_ACTIVATE,
					true);
	if (ret < 0) {
		pr_err("%s: slim_control_ch failed ret[%d]\n",
				__func__, ret);
		goto err;
	}
	for (i = 0; i < ch_cnt; i++) {
		idx = (ch_num[i] - BASE_CH_NUM);
		tx[idx].grph = grph;
	}
	return 0;
err:
	/* release all acquired handles */
	wcd9xxx_close_slim_sch_tx(wcd9xxx, ch_num, ch_cnt);
	return ret;
}
EXPORT_SYMBOL_GPL(wcd9xxx_cfg_slim_sch_tx);

int wcd9xxx_close_slim_sch_rx(struct wcd9xxx *wcd9xxx, unsigned int *ch_num,
				unsigned int ch_cnt)
{
	u16 grph = 0;
	//u32 sph[SLIM_MAX_RX_PORTS] = {0};     //Qualcomm++
	int i = 0 , idx = 0;
	int ret = 0;
	struct wcd9xxx_slim_sch_rx *rx = sh_ch.rx;

	pr_debug("%s: ch_cnt[%d]\n", __func__, ch_cnt);
	for (i = 0; i < ch_cnt; i++) {
		idx = (ch_num[i] - BASE_CH_NUM -
			SB_PGD_OFFSET_OF_RX_SLAVE_DEV_PORTS - 1);
		if (idx < 0) {
			pr_err("%s: Error:-Invalid index found = %d\n",
				__func__, idx);
			ret = -EINVAL;
			goto err;
		}
		//sph[i] = rx[idx].sph;     //Qualcomm++
		grph = rx[idx].grph;
	}

	/* slim_control_ch (REMOVE) */
	ret = slim_control_ch(wcd9xxx->slim, grph, SLIM_CH_REMOVE, true);
	if (ret < 0) {
		pr_err("%s: slim_control_ch failed ret[%d]\n",
				__func__, ret);
		goto err;
	}

//Qualcomm++
#if 0
	/* slim_disconnect_port */
	ret = slim_disconnect_ports(wcd9xxx->slim, sph, ch_cnt);
	if (ret < 0) {
		pr_err("%s: slim_disconnect_ports failed ret[%d]\n",
			 __func__, ret);
	}
#endif
//Qualcomm++

	for (i = 0; i < ch_cnt; i++) {
		idx = (ch_num[i] - BASE_CH_NUM -
				SB_PGD_OFFSET_OF_RX_SLAVE_DEV_PORTS - 1);
		rx[idx].grph = 0;
	}
err:
	return ret;
}
EXPORT_SYMBOL_GPL(wcd9xxx_close_slim_sch_rx);

int wcd9xxx_close_slim_sch_tx(struct wcd9xxx *wcd9xxx, unsigned int *ch_num,
				unsigned int ch_cnt)
{
	u16 grph = 0;
	//u32 sph[SLIM_MAX_TX_PORTS] = {0};     //Qualcomm++
	int ret = 0;
	int i = 0 , idx = 0;
	struct wcd9xxx_slim_sch_tx *tx = sh_ch.tx;

	pr_debug("%s: ch_cnt[%d]\n", __func__, ch_cnt);
	for (i = 0; i < ch_cnt; i++) {
		idx = (ch_num[i] - BASE_CH_NUM);
		if (idx < 0) {
			pr_err("%s: Error:- Invalid index found = %d\n",
				__func__, idx);
			ret = -EINVAL;
			goto err;
		}
		//sph[i] = tx[idx].sph;     //Qualcomm++
		grph = tx[idx].grph;
	}

//Qualcomm++
#if 0
	/* slim_disconnect_port */
	ret = slim_disconnect_ports(wcd9xxx->slim, sph, ch_cnt);
	if (ret < 0) {
		pr_err("%s: slim_disconnect_ports failed ret[%d]\n",
				__func__, ret);
	}
#endif
//Qualcomm++

	/* slim_control_ch (REMOVE) */
	ret = slim_control_ch(wcd9xxx->slim, grph, SLIM_CH_REMOVE, true);
	if (ret < 0) {
		pr_err("%s: slim_control_ch failed ret[%d]\n",
				__func__, ret);
		goto err;
	}
	for (i = 0; i < ch_cnt; i++) {
		idx = (ch_num[i] - BASE_CH_NUM);
		tx[idx].grph = 0;
	}
err:
	return ret;
}
EXPORT_SYMBOL_GPL(wcd9xxx_close_slim_sch_tx);

int wcd9xxx_get_slave_port(unsigned int ch_num)
{
	int ret = 0;

	pr_debug("%s: ch_num[%d]\n", __func__, ch_num);
	ret = (ch_num - BASE_CH_NUM);
	if (ret < 0) {
		pr_err("%s: Error:- Invalid slave port found = %d\n",
			__func__, ret);
		return -EINVAL;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(wcd9xxx_get_slave_port);

int wcd9xxx_disconnect_port(struct wcd9xxx *wcd9xxx, unsigned int *ch_num,
                unsigned int ch_cnt, unsigned int rx_tx)
{
    u32 sph[SLIM_MAX_TX_PORTS] = {0};
    int i = 0 , idx = 0;
    int ret = 0;
    struct wcd9xxx_slim_sch_rx *rx = sh_ch.rx;
    struct wcd9xxx_slim_sch_tx *tx = sh_ch.tx;

    pr_debug("%s: ch_cnt[%d], rx_tx flag = %d\n", __func__, ch_cnt, rx_tx);
    for (i = 0; i < ch_cnt; i++) {
        /* rx_tx will be 1 for rx, 0 for tx */
        if (rx_tx) {
            idx = (ch_num[i] - BASE_CH_NUM -
                SB_PGD_OFFSET_OF_RX_SLAVE_DEV_PORTS - 1);
            if (idx < 0) {
                pr_err("%s: Invalid index found for RX = %d\n",
                    __func__, idx);
                ret = -EINVAL;
                goto err;
            }
            sph[i] = rx[idx].sph;
        } else {
            idx = (ch_num[i] - BASE_CH_NUM);
            if (idx < 0) {
                pr_err("%s:Invalid index found for TX = %d\n",
                    __func__, idx);
                ret = -EINVAL;
                goto err;
            }
            sph[i] = tx[idx].sph;
        }
    }

    /* slim_disconnect_port */
    ret = slim_disconnect_ports(wcd9xxx->slim, sph, ch_cnt);
    if (ret < 0) {
        pr_err("%s: slim_disconnect_ports failed ret[%d]\n",
            __func__, ret);
    }
err:
    return ret;
}
EXPORT_SYMBOL_GPL(wcd9xxx_disconnect_port);
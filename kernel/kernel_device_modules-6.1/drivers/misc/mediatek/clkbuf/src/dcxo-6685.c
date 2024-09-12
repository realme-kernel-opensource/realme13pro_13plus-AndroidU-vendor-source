// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Kuan-Hsin Lee <Kuan-Hsin.Lee@mediatek.com>
 */
#include <linux/mfd/syscon.h>
#include "clkbuf-util.h"
#include "clkbuf-pmic.h"

#define PMRC_CON0			(0x190)
#define PMRC_CON1			(0x191)
#define PMRC_CON0_SET			(0x198)
#define PMRC_CON1_SET			(0x199)
#define PMRC_CON0_CLR			(0x19a)
#define PMRC_CON1_CLR			(0x19b)
#define PMRC_CON3			(0x19c)
#define XO_BUF_CTL0_L			(0x54c)
#define XO_BUF_CTL0_H			(0x54d)
#define XO_BUF_CTL1_L			(0x54e)
#define XO_BUF_CTL1_H			(0x54f)
#define XO_BUF_CTL2_L			(0x550)
#define XO_BUF_CTL2_H			(0x551)
#define XO_BUF_CTL3_L			(0x552)
#define XO_BUF_CTL3_H			(0x553)
#define XO_BUF_CTL4_L			(0x554)
#define XO_BUF_CTL4_H			(0x555)
#define XO_BUF_CTL5_L			(0x556)
#define XO_BUF_CTL5_H			(0x557)
#define XO_BUF_CTL6_L			(0x558)
#define XO_BUF_CTL6_H			(0x559)
#define XO_BUF_CTL7_L			(0x55a)
#define XO_BUF_CTL7_H			(0x55b)
#define XO_BUF_CTL8_L			(0x55c)
#define XO_BUF_CTL8_H			(0x55d)
#define XO_BUF_CTL9_L			(0x55e)
#define XO_BUF_CTL9_H			(0x55f)
#define XO_BUF_CTL10_L			(0x560)
#define XO_BUF_CTL10_H			(0x561)
#define XO_BUF_CTL11_L			(0x562)
#define XO_BUF_CTL11_H			(0x563)
#define XO_BUF_CTL12_L			(0x564)
#define XO_BUF_CTL12_H			(0x565)
#define XO_CONN_BT0			(0x567)
#define DCXO_DIG_MANCTRL_CW1		(0x796)
#define DCXO_BBLPM_CW0			(0x797)
#define DCXO_BBLPM_CW1			(0x798)
#define DCXO_LDO_CW0			(0x799)
#define DCXO_EXTBUF1_CW0			(0x79a)
#define DCXO_EXTBUF2_CW0			(0x79b)
#define DCXO_EXTBUF3_CW0			(0x79c)
#define DCXO_EXTBUF4_CW0			(0x79d)
#define DCXO_EXTBUF5_CW0			(0x79e)
#define DCXO_EXTBUF6_CW0			(0x79f)
#define DCXO_EXTBUF7_CW0			(0x7a0)
#define DCXO_EXTBUF8_CW0			(0x7a1)
#define DCXO_EXTBUF9_CW0			(0x7a2)
#define DCXO_EXTBUF10_CW0		(0x7a3)
#define DCXO_EXTBUF11_CW0		(0x7a4)
#define DCXO_EXTBUF12_CW0		(0x7a5)
#define DCXO_EXTBUF13_CW0		(0x7a6)
#define DCXO_EXTBUF_CW0			(0x7a7)
#define DCXO_RGMON_CW0_L			(0x7b2)
#define DCXO_RGMON_CW0_H			(0x7b3)
#define DCXO_RGMON_CW2_L			(0x7b4)
#define DCXO_RGMON_CW2_H			(0x7b5)
#define DCXO_RGMON_CW1			(0x7c3)
#define DCXO_DIGCLK_ELR			(0x7f4)
#define LDO_VBBCK_CON0			(0x1b87)
#define LDO_VBBCK_OP_EN1			(0x1b8e)
#define LDO_VBBCK_OP_EN1_SET		(0x1b8f)
#define LDO_VBBCK_OP_EN1_CLR		(0x1b90)
#define LDO_VRFCK1_CON0			(0x1b97)
#define LDO_VRFCK1_OP_EN1		(0x1b9e)
#define LDO_VRFCK1_OP_EN1_SET		(0x1b9f)
#define LDO_VRFCK1_OP_EN1_CLR		(0x1ba0)
#define LDO_VRFCK2_CON0			(0x1ba7)
#define LDO_VRFCK2_OP_EN1		(0x1bae)
#define LDO_VRFCK2_OP_EN1_SET		(0x1baf)
#define LDO_VRFCK2_OP_EN1_CLR		(0x1bb0)

/* Register_TOP_REG*/
#define NULL_ADDR			(0x0)
#define PMRC_EN0_ADDR			(PMRC_CON0)
#define PMRC_EN0_MASK			(0xff)
#define PMRC_EN0_SHIFT			(0)
#define PMRC_EN1_ADDR			(PMRC_CON1)
#define PMRC_EN1_MASK			(0xff)
#define PMRC_EN1_SHIFT			(0)
#define PMRC_CON0_SET_ADDR		(PMRC_CON0_SET)
#define PMRC_CON0_SET_MASK		(0xff)
#define PMRC_CON0_SET_SHIFT		(0)
#define PMRC_CON1_SET_ADDR		(PMRC_CON1_SET)
#define PMRC_CON1_SET_MASK		(0xff)
#define PMRC_CON1_SET_SHIFT		(0)
#define PMRC_CON0_CLR_ADDR		(PMRC_CON0_CLR)
#define PMRC_CON0_CLR_MASK		(0xff)
#define PMRC_CON0_CLR_SHIFT		(0)
#define PMRC_CON1_CLR_ADDR		(PMRC_CON1_CLR)
#define PMRC_CON1_CLR_MASK		(0xff)
#define PMRC_CON1_CLR_SHIFT		(0)
#define RG_DCXO_SRCLKEN1_MODE_ADDR	(PMRC_CON3)
#define RG_DCXO_SRCLKEN1_MODE_MASK	(0x3)
#define RG_DCXO_SRCLKEN1_MODE_SHIFT	(2)
/* Register_SCK_REG*/
#define XO_BBCK1_VOTE_L_ADDR		(XO_BUF_CTL0_L)
#define XO_BBCK1_VOTE_L_MASK		(0xff)
#define XO_BBCK1_VOTE_L_SHIFT		(0)
#define XO_BBCK1_VOTE_H_ADDR		(XO_BUF_CTL0_H)
#define XO_BBCK1_VOTE_H_MASK		(0x3f)
#define XO_BBCK1_VOTE_H_SHIFT		(0)
#define XO_BBCK2_VOTE_L_ADDR		(XO_BUF_CTL1_L)
#define XO_BBCK2_VOTE_L_MASK		(0xff)
#define XO_BBCK2_VOTE_L_SHIFT		(0)
#define XO_BBCK2_VOTE_H_ADDR		(XO_BUF_CTL1_H)
#define XO_BBCK2_VOTE_H_MASK		(0x3f)
#define XO_BBCK2_VOTE_H_SHIFT		(0)
#define XO_BBCK3_VOTE_L_ADDR		(XO_BUF_CTL2_L)
#define XO_BBCK3_VOTE_L_MASK		(0xff)
#define XO_BBCK3_VOTE_L_SHIFT		(0)
#define XO_BBCK3_VOTE_H_ADDR		(XO_BUF_CTL2_H)
#define XO_BBCK3_VOTE_H_MASK		(0x3f)
#define XO_BBCK3_VOTE_H_SHIFT		(0)
#define XO_BBCK4_VOTE_L_ADDR		(XO_BUF_CTL3_L)
#define XO_BBCK4_VOTE_L_MASK		(0xff)
#define XO_BBCK4_VOTE_L_SHIFT		(0)
#define XO_BBCK4_VOTE_H_ADDR		(XO_BUF_CTL3_H)
#define XO_BBCK4_VOTE_H_MASK		(0x3f)
#define XO_BBCK4_VOTE_H_SHIFT		(0)
#define XO_BBCK5_VOTE_L_ADDR		(XO_BUF_CTL4_L)
#define XO_BBCK5_VOTE_L_MASK		(0xff)
#define XO_BBCK5_VOTE_L_SHIFT		(0)
#define XO_BBCK5_VOTE_H_ADDR		(XO_BUF_CTL4_H)
#define XO_BBCK5_VOTE_H_MASK		(0x3f)
#define XO_BBCK5_VOTE_H_SHIFT		(0)
#define XO_RFCK1A_VOTE_L_ADDR		(XO_BUF_CTL5_L)
#define XO_RFCK1A_VOTE_L_MASK		(0xff)
#define XO_RFCK1A_VOTE_L_SHIFT		(0)
#define XO_RFCK1A_VOTE_H_ADDR		(XO_BUF_CTL5_H)
#define XO_RFCK1A_VOTE_H_MASK		(0x3f)
#define XO_RFCK1A_VOTE_H_SHIFT		(0)
#define XO_RFCK1B_VOTE_L_ADDR		(XO_BUF_CTL6_L)
#define XO_RFCK1B_VOTE_L_MASK		(0xff)
#define XO_RFCK1B_VOTE_L_SHIFT		(0)
#define XO_RFCK1B_VOTE_H_ADDR		(XO_BUF_CTL6_H)
#define XO_RFCK1B_VOTE_H_MASK		(0x3f)
#define XO_RFCK1B_VOTE_H_SHIFT		(0)
#define XO_RFCK1C_VOTE_L_ADDR		(XO_BUF_CTL7_L)
#define XO_RFCK1C_VOTE_L_MASK		(0xff)
#define XO_RFCK1C_VOTE_L_SHIFT		(0)
#define XO_RFCK1C_VOTE_H_ADDR		(XO_BUF_CTL7_H)
#define XO_RFCK1C_VOTE_H_MASK		(0x3f)
#define XO_RFCK1C_VOTE_H_SHIFT		(0)
#define XO_RFCK2A_VOTE_L_ADDR		(XO_BUF_CTL8_L)
#define XO_RFCK2A_VOTE_L_MASK		(0xff)
#define XO_RFCK2A_VOTE_L_SHIFT		(0)
#define XO_RFCK2A_VOTE_H_ADDR		(XO_BUF_CTL8_H)
#define XO_RFCK2A_VOTE_H_MASK		(0x3f)
#define XO_RFCK2A_VOTE_H_SHIFT		(0)
#define XO_RFCK2B_VOTE_L_ADDR		(XO_BUF_CTL9_L)
#define XO_RFCK2B_VOTE_L_MASK		(0xff)
#define XO_RFCK2B_VOTE_L_SHIFT		(0)
#define XO_RFCK2B_VOTE_H_ADDR		(XO_BUF_CTL9_H)
#define XO_RFCK2B_VOTE_H_MASK		(0x3f)
#define XO_RFCK2B_VOTE_H_SHIFT		(0)
#define XO_RFCK2C_VOTE_L_ADDR		(XO_BUF_CTL10_L)
#define XO_RFCK2C_VOTE_L_MASK		(0xff)
#define XO_RFCK2C_VOTE_L_SHIFT		(0)
#define XO_RFCK2C_VOTE_H_ADDR		(XO_BUF_CTL10_H)
#define XO_RFCK2C_VOTE_H_MASK		(0x3f)
#define XO_RFCK2C_VOTE_H_SHIFT		(0)
#define XO_CONCK1_VOTE_L_ADDR		(XO_BUF_CTL11_L)
#define XO_CONCK1_VOTE_L_MASK		(0xff)
#define XO_CONCK1_VOTE_L_SHIFT		(0)
#define XO_CONCK1_VOTE_H_ADDR		(XO_BUF_CTL11_H)
#define XO_CONCK1_VOTE_H_MASK		(0x3f)
#define XO_CONCK1_VOTE_H_SHIFT		(0)
#define XO_CONCK2_VOTE_L_ADDR		(XO_BUF_CTL12_L)
#define XO_CONCK2_VOTE_L_MASK		(0xff)
#define XO_CONCK2_VOTE_L_SHIFT		(0)
#define XO_CONCK2_VOTE_H_ADDR		(XO_BUF_CTL12_H)
#define XO_CONCK2_VOTE_H_MASK		(0x3f)
#define XO_CONCK2_VOTE_H_SHIFT		(0)
#define XO_MODE_CONN_BT_MASK_ADDR	(XO_CONN_BT0)
#define XO_MODE_CONN_BT_MASK_MASK	(0x1)
#define XO_MODE_CONN_BT_MASK_SHIFT	(0)
#define XO_BUF_CONN_BT_MASK_ADDR		(XO_CONN_BT0)
#define XO_BUF_CONN_BT_MASK_MASK		(0x1)
#define XO_BUF_CONN_BT_MASK_SHIFT	(1)
/* Register_DCXO_REG*/
#define XO_PMIC_TOP_DIG_SW_ADDR		(DCXO_DIG_MANCTRL_CW1)
#define XO_PMIC_TOP_DIG_SW_MASK		(0x1)
#define XO_PMIC_TOP_DIG_SW_SHIFT		(0)
#define XO_ENBB_MAN_ADDR			(DCXO_DIG_MANCTRL_CW1)
#define XO_ENBB_MAN_MASK			(0x1)
#define XO_ENBB_MAN_SHIFT		(1)
#define XO_ENBB_EN_M_ADDR		(DCXO_DIG_MANCTRL_CW1)
#define XO_ENBB_EN_M_MASK		(0x1)
#define XO_ENBB_EN_M_SHIFT		(2)
#define XO_CLKSEL_MAN_ADDR		(DCXO_DIG_MANCTRL_CW1)
#define XO_CLKSEL_MAN_MASK		(0x1)
#define XO_CLKSEL_MAN_SHIFT		(3)
#define XO_CLKSEL_EN_M_ADDR		(DCXO_DIG_MANCTRL_CW1)
#define XO_CLKSEL_EN_M_MASK		(0x1)
#define XO_CLKSEL_EN_M_SHIFT		(4)
#define XO_BB_LPM_EN_M_ADDR		(DCXO_BBLPM_CW0)
#define XO_BB_LPM_EN_M_MASK		(0x1)
#define XO_BB_LPM_EN_M_SHIFT		(0)
#define XO_BB_LPM_EN_SEL_ADDR		(DCXO_BBLPM_CW0)
#define XO_BB_LPM_EN_SEL_MASK		(0x1)
#define XO_BB_LPM_EN_SEL_SHIFT		(1)
#define XO_BBCK1_BBLPM_EN_MASK_ADDR	(DCXO_BBLPM_CW0)
#define XO_BBCK1_BBLPM_EN_MASK_MASK	(0x1)
#define XO_BBCK1_BBLPM_EN_MASK_SHIFT	(2)
#define XO_BBCK2_BBLPM_EN_MASK_ADDR	(DCXO_BBLPM_CW0)
#define XO_BBCK2_BBLPM_EN_MASK_MASK	(0x1)
#define XO_BBCK2_BBLPM_EN_MASK_SHIFT	(3)
#define XO_BBCK3_BBLPM_EN_MASK_ADDR	(DCXO_BBLPM_CW0)
#define XO_BBCK3_BBLPM_EN_MASK_MASK	(0x1)
#define XO_BBCK3_BBLPM_EN_MASK_SHIFT	(4)
#define XO_BBCK4_BBLPM_EN_MASK_ADDR	(DCXO_BBLPM_CW0)
#define XO_BBCK4_BBLPM_EN_MASK_MASK	(0x1)
#define XO_BBCK4_BBLPM_EN_MASK_SHIFT	(5)
#define XO_BBCK5_BBLPM_EN_MASK_ADDR	(DCXO_BBLPM_CW0)
#define XO_BBCK5_BBLPM_EN_MASK_MASK	(0x1)
#define XO_BBCK5_BBLPM_EN_MASK_SHIFT	(6)
#define XO_BBLPM_EN_MAN_ADDR		(DCXO_BBLPM_CW1)
#define XO_BBLPM_EN_MAN_MASK		(0x1)
#define XO_BBLPM_EN_MAN_SHIFT		(0)
#define XO_BBLPM_EN_MAN_M_ADDR		(DCXO_BBLPM_CW1)
#define XO_BBLPM_EN_MAN_M_MASK		(0x1)
#define XO_BBLPM_EN_MAN_M_SHIFT		(1)
#define XO_VBBCK_EN_MAN_ADDR		(DCXO_LDO_CW0)
#define XO_VBBCK_EN_MAN_MASK		(0x1)
#define XO_VBBCK_EN_MAN_SHIFT		(0)
#define XO_VBBCK_EN_M_ADDR		(DCXO_LDO_CW0)
#define XO_VBBCK_EN_M_MASK		(0x1)
#define XO_VBBCK_EN_M_SHIFT		(1)
#define XO_VRFCK1_EN_MAN_ADDR		(DCXO_LDO_CW0)
#define XO_VRFCK1_EN_MAN_MASK		(0x1)
#define XO_VRFCK1_EN_MAN_SHIFT		(2)
#define XO_VRFCK1_EN_M_ADDR		(DCXO_LDO_CW0)
#define XO_VRFCK1_EN_M_MASK		(0x1)
#define XO_VRFCK1_EN_M_SHIFT		(3)
#define XO_VRFCK2_EN_MAN_ADDR		(DCXO_LDO_CW0)
#define XO_VRFCK2_EN_MAN_MASK		(0x1)
#define XO_VRFCK2_EN_MAN_SHIFT		(4)
#define XO_VRFCK2_EN_M_ADDR		(DCXO_LDO_CW0)
#define XO_VRFCK2_EN_M_MASK		(0x1)
#define XO_VRFCK2_EN_M_SHIFT		(5)
#define XO_VCONCK_EN_MAN_ADDR		(DCXO_LDO_CW0)
#define XO_VCONCK_EN_MAN_MASK		(0x1)
#define XO_VCONCK_EN_MAN_SHIFT		(6)
#define XO_VCONCK_EN_M_ADDR		(DCXO_LDO_CW0)
#define XO_VCONCK_EN_M_MASK		(0x1)
#define XO_VCONCK_EN_M_SHIFT		(7)
#define XO_BBCK1_MODE_ADDR		(DCXO_EXTBUF1_CW0)
#define XO_BBCK1_MODE_MASK		(0x3)
#define XO_BBCK1_MODE_SHIFT		(0)
#define XO_BBCK1_EN_M_ADDR		(DCXO_EXTBUF1_CW0)
#define XO_BBCK1_EN_M_MASK		(0x1)
#define XO_BBCK1_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_BBCK1_RSEL_ADDR	(DCXO_EXTBUF1_CW0)
#define RG_XO_EXTBUF_BBCK1_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_BBCK1_RSEL_SHIFT	(3)
#define RG_XO_EXTBUF_BBCK1_HD_ADDR	(DCXO_EXTBUF1_CW0)
#define RG_XO_EXTBUF_BBCK1_HD_MASK	(0x3)
#define RG_XO_EXTBUF_BBCK1_HD_SHIFT	(6)
#define XO_BBCK2_MODE_ADDR		(DCXO_EXTBUF2_CW0)
#define XO_BBCK2_MODE_MASK		(0x3)
#define XO_BBCK2_MODE_SHIFT		(0)
#define XO_BBCK2_EN_M_ADDR		(DCXO_EXTBUF2_CW0)
#define XO_BBCK2_EN_M_MASK		(0x1)
#define XO_BBCK2_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_BBCK2_RSEL_ADDR	(DCXO_EXTBUF2_CW0)
#define RG_XO_EXTBUF_BBCK2_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_BBCK2_RSEL_SHIFT	(3)
#define RG_XO_EXTBUF_BBCK2_HD_ADDR	(DCXO_EXTBUF2_CW0)
#define RG_XO_EXTBUF_BBCK2_HD_MASK	(0x3)
#define RG_XO_EXTBUF_BBCK2_HD_SHIFT	(6)
#define XO_BBCK3_MODE_ADDR		(DCXO_EXTBUF3_CW0)
#define XO_BBCK3_MODE_MASK		(0x3)
#define XO_BBCK3_MODE_SHIFT		(0)
#define XO_BBCK3_EN_M_ADDR		(DCXO_EXTBUF3_CW0)
#define XO_BBCK3_EN_M_MASK		(0x1)
#define XO_BBCK3_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_BBCK3_RSEL_ADDR	(DCXO_EXTBUF3_CW0)
#define RG_XO_EXTBUF_BBCK3_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_BBCK3_RSEL_SHIFT	(3)
#define RG_XO_EXTBUF_BBCK3_HD_ADDR	(DCXO_EXTBUF3_CW0)
#define RG_XO_EXTBUF_BBCK3_HD_MASK	(0x3)
#define RG_XO_EXTBUF_BBCK3_HD_SHIFT	(6)
#define XO_BBCK4_MODE_ADDR		(DCXO_EXTBUF4_CW0)
#define XO_BBCK4_MODE_MASK		(0x3)
#define XO_BBCK4_MODE_SHIFT		(0)
#define XO_BBCK4_EN_M_ADDR		(DCXO_EXTBUF4_CW0)
#define XO_BBCK4_EN_M_MASK		(0x1)
#define XO_BBCK4_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_BBCK4_RSEL_ADDR	(DCXO_EXTBUF4_CW0)
#define RG_XO_EXTBUF_BBCK4_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_BBCK4_RSEL_SHIFT	(3)
#define RG_XO_EXTBUF_BBCK4_HD_ADDR	(DCXO_EXTBUF4_CW0)
#define RG_XO_EXTBUF_BBCK4_HD_MASK	(0x3)
#define RG_XO_EXTBUF_BBCK4_HD_SHIFT	(6)
#define XO_BBCK5_MODE_ADDR		(DCXO_EXTBUF5_CW0)
#define XO_BBCK5_MODE_MASK		(0x3)
#define XO_BBCK5_MODE_SHIFT		(0)
#define XO_BBCK5_EN_M_ADDR		(DCXO_EXTBUF5_CW0)
#define XO_BBCK5_EN_M_MASK		(0x1)
#define XO_BBCK5_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_BBCK5_RSEL_ADDR	(DCXO_EXTBUF5_CW0)
#define RG_XO_EXTBUF_BBCK5_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_BBCK5_RSEL_SHIFT	(3)
#define RG_XO_EXTBUF_BBCK5_HD_ADDR	(DCXO_EXTBUF5_CW0)
#define RG_XO_EXTBUF_BBCK5_HD_MASK	(0x3)
#define RG_XO_EXTBUF_BBCK5_HD_SHIFT	(6)
#define XO_RFCK1A_MODE_ADDR		(DCXO_EXTBUF6_CW0)
#define XO_RFCK1A_MODE_MASK		(0x3)
#define XO_RFCK1A_MODE_SHIFT		(0)
#define XO_RFCK1A_EN_M_ADDR		(DCXO_EXTBUF6_CW0)
#define XO_RFCK1A_EN_M_MASK		(0x1)
#define XO_RFCK1A_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_RFCK1A_RSEL_ADDR	(DCXO_EXTBUF6_CW0)
#define RG_XO_EXTBUF_RFCK1A_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_RFCK1A_RSEL_SHIFT	(3)
#define XO_RFCK1B_MODE_ADDR		(DCXO_EXTBUF7_CW0)
#define XO_RFCK1B_MODE_MASK		(0x3)
#define XO_RFCK1B_MODE_SHIFT		(0)
#define XO_RFCK1B_EN_M_ADDR		(DCXO_EXTBUF7_CW0)
#define XO_RFCK1B_EN_M_MASK		(0x1)
#define XO_RFCK1B_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_RFCK1B_RSEL_ADDR	(DCXO_EXTBUF7_CW0)
#define RG_XO_EXTBUF_RFCK1B_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_RFCK1B_RSEL_SHIFT	(3)
#define XO_RFCK1C_MODE_ADDR		(DCXO_EXTBUF8_CW0)
#define XO_RFCK1C_MODE_MASK		(0x3)
#define XO_RFCK1C_MODE_SHIFT		(0)
#define XO_RFCK1C_EN_M_ADDR		(DCXO_EXTBUF8_CW0)
#define XO_RFCK1C_EN_M_MASK		(0x1)
#define XO_RFCK1C_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_RFCK1C_RSEL_ADDR	(DCXO_EXTBUF8_CW0)
#define RG_XO_EXTBUF_RFCK1C_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_RFCK1C_RSEL_SHIFT	(3)
#define XO_RFCK2A_MODE_ADDR		(DCXO_EXTBUF9_CW0)
#define XO_RFCK2A_MODE_MASK		(0x3)
#define XO_RFCK2A_MODE_SHIFT		(0)
#define XO_RFCK2A_EN_M_ADDR		(DCXO_EXTBUF9_CW0)
#define XO_RFCK2A_EN_M_MASK		(0x1)
#define XO_RFCK2A_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_RFCK2A_RSEL_ADDR	(DCXO_EXTBUF9_CW0)
#define RG_XO_EXTBUF_RFCK2A_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_RFCK2A_RSEL_SHIFT	(3)
#define XO_RFCK2B_MODE_ADDR		(DCXO_EXTBUF10_CW0)
#define XO_RFCK2B_MODE_MASK		(0x3)
#define XO_RFCK2B_MODE_SHIFT		(0)
#define XO_RFCK2B_EN_M_ADDR		(DCXO_EXTBUF10_CW0)
#define XO_RFCK2B_EN_M_MASK		(0x1)
#define XO_RFCK2B_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_RFCK2B_RSEL_ADDR	(DCXO_EXTBUF10_CW0)
#define RG_XO_EXTBUF_RFCK2B_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_RFCK2B_RSEL_SHIFT	(3)
#define XO_RFCK2C_MODE_ADDR		(DCXO_EXTBUF11_CW0)
#define XO_RFCK2C_MODE_MASK		(0x3)
#define XO_RFCK2C_MODE_SHIFT		(0)
#define XO_RFCK2C_EN_M_ADDR		(DCXO_EXTBUF11_CW0)
#define XO_RFCK2C_EN_M_MASK		(0x1)
#define XO_RFCK2C_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_RFCK2C_RSEL_ADDR	(DCXO_EXTBUF11_CW0)
#define RG_XO_EXTBUF_RFCK2C_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_RFCK2C_RSEL_SHIFT	(3)
#define XO_CONCK1_MODE_ADDR		(DCXO_EXTBUF12_CW0)
#define XO_CONCK1_MODE_MASK		(0x3)
#define XO_CONCK1_MODE_SHIFT		(0)
#define XO_CONCK1_EN_M_ADDR		(DCXO_EXTBUF12_CW0)
#define XO_CONCK1_EN_M_MASK		(0x1)
#define XO_CONCK1_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_CONCK1_RSEL_ADDR	(DCXO_EXTBUF12_CW0)
#define RG_XO_EXTBUF_CONCK1_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_CONCK1_RSEL_SHIFT	(3)
#define XO_CONCK2_MODE_ADDR		(DCXO_EXTBUF13_CW0)
#define XO_CONCK2_MODE_MASK		(0x3)
#define XO_CONCK2_MODE_SHIFT		(0)
#define XO_CONCK2_EN_M_ADDR		(DCXO_EXTBUF13_CW0)
#define XO_CONCK2_EN_M_MASK		(0x1)
#define XO_CONCK2_EN_M_SHIFT		(2)
#define RG_XO_EXTBUF_CONCK2_RSEL_ADDR	(DCXO_EXTBUF13_CW0)
#define RG_XO_EXTBUF_CONCK2_RSEL_MASK	(0x7)
#define RG_XO_EXTBUF_CONCK2_RSEL_SHIFT	(3)
#define XO_BBCK_ALL_EN_ADDR		(DCXO_EXTBUF_CW0)
#define XO_BBCK_ALL_EN_MASK		(0x1)
#define XO_BBCK_ALL_EN_SHIFT		(0)
#define XO_RFCK1_ALL_EN_ADDR		(DCXO_EXTBUF_CW0)
#define XO_RFCK1_ALL_EN_MASK		(0x1)
#define XO_RFCK1_ALL_EN_SHIFT		(1)
#define XO_RFCK2_ALL_EN_ADDR		(DCXO_EXTBUF_CW0)
#define XO_RFCK2_ALL_EN_MASK		(0x1)
#define XO_RFCK2_ALL_EN_SHIFT		(2)
#define XO_CONCK_ALL_EN_ADDR		(DCXO_EXTBUF_CW0)
#define XO_CONCK_ALL_EN_MASK		(0x1)
#define XO_CONCK_ALL_EN_SHIFT		(3)
#define XO_AUXOUT_SEL_L_ADDR		(DCXO_RGMON_CW0_L)
#define XO_AUXOUT_SEL_L_MASK		(0xff)
#define XO_AUXOUT_SEL_L_SHIFT		(0)
#define XO_AUXOUT_SEL_H_ADDR		(DCXO_RGMON_CW0_H)
#define XO_AUXOUT_SEL_H_MASK		(0x3)
#define XO_AUXOUT_SEL_H_SHIFT		(0)
#define XO_STATIC_AUXOUT_L_ADDR		(DCXO_RGMON_CW2_L)
#define XO_STATIC_AUXOUT_L_MASK		(0xff)
#define XO_STATIC_AUXOUT_L_SHIFT		(0)
#define XO_STATIC_AUXOUT_H_ADDR		(DCXO_RGMON_CW2_H)
#define XO_STATIC_AUXOUT_H_MASK		(0xff)
#define XO_STATIC_AUXOUT_H_SHIFT		(0)
#define XO_STATIC_AUXOUT_SEL_ADDR	(DCXO_RGMON_CW1)
#define XO_STATIC_AUXOUT_SEL_MASK	(0x7f)
#define XO_STATIC_AUXOUT_SEL_SHIFT	(0)
#define RG_XO_DIG26M_DIV2_ADDR		(DCXO_DIGCLK_ELR)
#define RG_XO_DIG26M_DIV2_MASK		(0x1)
#define RG_XO_DIG26M_DIV2_SHIFT		(0)
/* Register_LDO_REG*/
#define RG_LDO_VBBCK_EN_ADDR		(LDO_VBBCK_CON0)
#define RG_LDO_VBBCK_EN_MASK		(0x1)
#define RG_LDO_VBBCK_EN_SHIFT		(0)
#define RG_LDO_VBBCK_HW14_OP_EN_ADDR	(LDO_VBBCK_OP_EN1)
#define RG_LDO_VBBCK_HW14_OP_EN_MASK	(0x1)
#define RG_LDO_VBBCK_HW14_OP_EN_SHIFT	(6)
#define RG_LDO_VRFCK1_EN_ADDR		(LDO_VRFCK1_CON0)
#define RG_LDO_VRFCK1_EN_MASK		(0x1)
#define RG_LDO_VRFCK1_EN_SHIFT		(0)
#define RG_LDO_VRFCK1_HW14_OP_EN_ADDR	(LDO_VRFCK1_OP_EN1)
#define RG_LDO_VRFCK1_HW14_OP_EN_MASK	(0x1)
#define RG_LDO_VRFCK1_HW14_OP_EN_SHIFT	(6)
#define RG_LDO_VRFCK2_EN_ADDR		(LDO_VRFCK2_CON0)
#define RG_LDO_VRFCK2_EN_MASK		(0x1)
#define RG_LDO_VRFCK2_EN_SHIFT		(0)
#define RG_LDO_VRFCK2_HW14_OP_EN_ADDR	(LDO_VRFCK2_OP_EN1)
#define RG_LDO_VRFCK2_HW14_OP_EN_MASK	(0x1)
#define RG_LDO_VRFCK2_HW14_OP_EN_SHIFT	(6)

struct reg_t mt6685_debug_regs[] = {
	[0] =
	SET_DBG_REG(vrfck1_en, RG_LDO_VRFCK1_EN)
	[1] =
	SET_DBG_REG(vrfck1_op_en14, RG_LDO_VRFCK1_HW14_OP_EN)
	[2] =
	SET_DBG_REG(vrfck2_en, RG_LDO_VRFCK2_EN)
	[3] =
	SET_DBG_REG(vrfck2_op_en14, RG_LDO_VRFCK2_HW14_OP_EN)
	[4] =
	SET_DBG_REG(vbbck_en, RG_LDO_VBBCK_EN)
	[5] =
	SET_DBG_REG(vbbck_op_en14, RG_LDO_VBBCK_HW14_OP_EN)
	[6] =
	SET_DBG_REG(xo_pmic_top_dig_sw, XO_PMIC_TOP_DIG_SW)
	[7] =
	DBG_REG(dcxo_manual_cw1, DCXO_DIG_MANCTRL_CW1, 0xFF, 0)
	[8] =
	DBG_REG(dcxo_bblpm_cw0, DCXO_BBLPM_CW0, 0xFF, 0)
	[9] =
	SET_DBG_REG(dcxo_bblpm_cw1, XO_BBLPM_EN_MAN)
	[10] =
	DBG_REG(dcxo_buf_cw0, DCXO_EXTBUF1_CW0, 0xFF, 0)
	[11] =
	SET_DBG_REG(dcxo_dig26m_div2, RG_XO_DIG26M_DIV2)
	[12] =
	SET_DBG_REG(xo_clksel_man, XO_CLKSEL_MAN)
	[13] =
	SET_DBG_REG(xo_clksel_en_m, XO_CLKSEL_EN_M)
	[14] =
	SET_DBG_REG(swbblpm_en, XO_BB_LPM_EN_M)
	[15] =
	SET_DBG_REG(hwbblpm_sel, XO_BB_LPM_EN_SEL)
	[16] =
	DBG_REG(NULL, NULL_ADDR, 0x0, 0x0)
};

struct common_regs com_regs = {
	.bblpm_auxout_sel = 79,
	.mode_num = 3,
	.spmi_mask = 0x0000ffff,
	SET_REG_BY_NAME(static_aux_sel, XO_STATIC_AUXOUT_SEL)
	SET_REG(bblpm_auxout, XO_STATIC_AUXOUT_H_ADDR, 0x1, 7)
	SET_REG_BY_NAME(swbblpm_en, XO_BB_LPM_EN_M)
	SET_REG_BY_NAME(hwbblpm_sel, XO_BB_LPM_EN_SEL)
	SET_DBG_REG(pmrc_en_l, PMRC_EN0)
	SET_DBG_REG(pmrc_en_h, PMRC_EN1)

};

struct xo_buf_t mt6685_xo_bufs[] = {
	[0] = {
		SET_REG_BY_NAME(xo_mode, XO_BBCK1_MODE)
		SET_REG_BY_NAME(xo_en, XO_BBCK1_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_H_ADDR, 0x1, 7)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_BBCK1_RSEL)
		SET_REG_BY_NAME(de_sense, RG_XO_EXTBUF_BBCK1_HD)
		SET_REG_BY_NAME(rc_voter, XO_BBCK1_VOTE_L)
		SET_REG_BY_NAME(hwbblpm_msk, XO_BBCK1_BBLPM_EN_MASK)
		.xo_en_auxout_sel = 15,
	},
	[1] = {
		SET_REG_BY_NAME(xo_mode, XO_BBCK2_MODE)
		SET_REG_BY_NAME(xo_en, XO_BBCK2_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_H_ADDR, 0x1, 6)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_BBCK2_RSEL)
		SET_REG_BY_NAME(de_sense, RG_XO_EXTBUF_BBCK2_HD)
		SET_REG_BY_NAME(rc_voter, XO_BBCK2_VOTE_L)
		SET_REG_BY_NAME(hwbblpm_msk, XO_BBCK2_BBLPM_EN_MASK)
		.xo_en_auxout_sel = 15,
	},
	[2] = {
		SET_REG_BY_NAME(xo_mode, XO_BBCK3_MODE)
		SET_REG_BY_NAME(xo_en, XO_BBCK3_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_H_ADDR, 0x1, 5)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_BBCK3_RSEL)
		SET_REG_BY_NAME(de_sense, RG_XO_EXTBUF_BBCK3_HD)
		SET_REG_BY_NAME(rc_voter, XO_BBCK3_VOTE_L)
		SET_REG_BY_NAME(hwbblpm_msk, XO_BBCK3_BBLPM_EN_MASK)
		.xo_en_auxout_sel = 15,
	},
	[3] = {
		SET_REG_BY_NAME(xo_mode, XO_BBCK4_MODE)
		SET_REG_BY_NAME(xo_en, XO_BBCK4_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_H_ADDR, 0x1, 4)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_BBCK4_RSEL)
		SET_REG_BY_NAME(de_sense, RG_XO_EXTBUF_BBCK4_HD)
		SET_REG_BY_NAME(rc_voter, XO_BBCK4_VOTE_L)
		SET_REG_BY_NAME(hwbblpm_msk, XO_BBCK4_BBLPM_EN_MASK)
		.xo_en_auxout_sel = 15,
	},
	[4] = {
		SET_REG_BY_NAME(xo_mode, XO_BBCK5_MODE)
		SET_REG_BY_NAME(xo_en, XO_BBCK5_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_H_ADDR, 0x1, 3)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_BBCK5_RSEL)
		SET_REG_BY_NAME(de_sense, RG_XO_EXTBUF_BBCK5_HD)
		SET_REG_BY_NAME(rc_voter, XO_BBCK5_VOTE_L)
		SET_REG_BY_NAME(hwbblpm_msk, XO_BBCK5_BBLPM_EN_MASK)
		.xo_en_auxout_sel = 15,
	},
	[5] = {
		SET_REG_BY_NAME(xo_mode, XO_RFCK1A_MODE)
		SET_REG_BY_NAME(xo_en, XO_RFCK1A_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_H_ADDR, 0x1, 2)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_RFCK1A_RSEL)
		SET_REG_BY_NAME(rc_voter, XO_RFCK1A_VOTE_L)
		.xo_en_auxout_sel = 15,
	},
	[6] = {
		SET_REG_BY_NAME(xo_mode, XO_RFCK1B_MODE)
		SET_REG_BY_NAME(xo_en, XO_RFCK1B_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_H_ADDR, 0x1, 1)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_RFCK1B_RSEL)
		SET_REG_BY_NAME(rc_voter, XO_RFCK1B_VOTE_L)
		.xo_en_auxout_sel = 15,
	},
	[7] = {
		SET_REG_BY_NAME(xo_mode, XO_RFCK1C_MODE)
		SET_REG_BY_NAME(xo_en, XO_RFCK1C_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_H_ADDR, 0x1, 0)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_RFCK1C_RSEL)
		SET_REG_BY_NAME(rc_voter, XO_RFCK1C_VOTE_L)
		.xo_en_auxout_sel = 15,
	},
	[8] = {
		SET_REG_BY_NAME(xo_mode, XO_RFCK2A_MODE)
		SET_REG_BY_NAME(xo_en, XO_RFCK2A_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_L_ADDR, 0x1, 7)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_RFCK2A_RSEL)
		SET_REG_BY_NAME(rc_voter, XO_RFCK2A_VOTE_L)
		.xo_en_auxout_sel = 15,
	},
	[9] = {
		SET_REG_BY_NAME(xo_mode, XO_RFCK2B_MODE)
		SET_REG_BY_NAME(xo_en, XO_RFCK2B_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_L_ADDR, 0x1, 6)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_RFCK2B_RSEL)
		SET_REG_BY_NAME(rc_voter, XO_RFCK2B_VOTE_L)
		.xo_en_auxout_sel = 15,
	},
	[10] = {
		SET_REG_BY_NAME(xo_mode, XO_RFCK2C_MODE)
		SET_REG_BY_NAME(xo_en, XO_RFCK2C_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_L_ADDR, 0x1, 5)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_RFCK2C_RSEL)
		SET_REG_BY_NAME(rc_voter, XO_RFCK2C_VOTE_L)
		.xo_en_auxout_sel = 15,
	},
	[11] = {
		SET_REG_BY_NAME(xo_mode, XO_CONCK1_MODE)
		SET_REG_BY_NAME(xo_en, XO_CONCK1_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_L_ADDR, 0x1, 4)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_CONCK1_RSEL)
		SET_REG_BY_NAME(rc_voter, XO_CONCK1_VOTE_L)
		.xo_en_auxout_sel = 15,
	},
	[12] = {
		SET_REG_BY_NAME(xo_mode, XO_CONCK2_MODE)
		SET_REG_BY_NAME(xo_en, XO_CONCK2_EN_M)
		SET_REG(xo_en_auxout, XO_STATIC_AUXOUT_L_ADDR, 0x1, 3)
		SET_REG_BY_NAME(impedance, RG_XO_EXTBUF_CONCK2_RSEL)
		SET_REG_BY_NAME(rc_voter, XO_CONCK2_VOTE_L)
		.xo_en_auxout_sel = 15,
	},
};

struct plat_xodata mt6685_data = {
	.xo_buf_t = mt6685_xo_bufs,
	.debug_regs = mt6685_debug_regs,
	.common_regs = &com_regs,
};

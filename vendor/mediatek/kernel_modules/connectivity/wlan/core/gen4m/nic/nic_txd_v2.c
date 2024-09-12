/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

/*! \file   nic_tx.c
 *    \brief  Functions that provide TX operation in NIC Layer.
 *
 *    This file provides TX functions which are responsible for both Hardware
 *    and Software Resource Management and keep their Synchronization.
 */


#if (CFG_SUPPORT_CONNAC2X == 1)
/*******************************************************************************
 *                         C O M P I L E R   F L A G S
 *******************************************************************************
 */

/*******************************************************************************
 *                    E X T E R N A L   R E F E R E N C E S
 *******************************************************************************
 */
#include "precomp.h"

/*******************************************************************************
 *                              C O N S T A N T S
 *******************************************************************************
 */

/*******************************************************************************
 *                             D A T A   T Y P E S
 *******************************************************************************
 */

/*******************************************************************************
 *                            P U B L I C   D A T A
 *******************************************************************************
 */


/*******************************************************************************
 *                           P R I V A T E   D A T A
 *******************************************************************************
 */

/*******************************************************************************
 *                                 M A C R O S
 *******************************************************************************
 */

/*******************************************************************************
 *                  F U N C T I O N   D E C L A R A T I O N S
 *******************************************************************************
 */

/*******************************************************************************
 *                              F U N C T I O N S
 *******************************************************************************
 */

uint8_t nic_txd_v2_long_format_op(
	void *prTxDesc,
	uint8_t fgSet)
{
	if (fgSet)
		HAL_MAC_CONNAC2X_TXD_SET_LONG_FORMAT(
			(struct HW_MAC_CONNAC2X_TX_DESC *)prTxDesc);
	return HAL_MAC_CONNAC2X_TXD_IS_LONG_FORMAT(
			(struct HW_MAC_CONNAC2X_TX_DESC *)prTxDesc);
}

uint8_t nic_txd_v2_tid_op(
	void *prTxDesc,
	uint8_t ucTid,
	uint8_t fgSet)
{
	if (fgSet)
		HAL_MAC_CONNAC2X_TXD_SET_TID(
			(struct HW_MAC_CONNAC2X_TX_DESC *)prTxDesc, ucTid);
	return HAL_MAC_CONNAC2X_TXD_GET_TID(
			(struct HW_MAC_CONNAC2X_TX_DESC *)prTxDesc);
}

uint8_t nic_txd_v2_queue_idx_op(
	void *prTxDesc,
	uint8_t ucQueIdx,
	uint8_t fgSet)
{
	if (fgSet)
		HAL_MAC_CONNAC2X_TXD_SET_QUEUE_INDEX(
			(struct HW_MAC_CONNAC2X_TX_DESC *)prTxDesc, ucQueIdx);
	return HAL_MAC_CONNAC2X_TXD_GET_QUEUE_INDEX(
			(struct HW_MAC_CONNAC2X_TX_DESC *)prTxDesc);
}

#if (CFG_TCP_IP_CHKSUM_OFFLOAD == 1)
void nic_txd_v2_chksum_op(void *prTxDesc, uint8_t ucChksumFlag,
			struct MSDU_INFO *prMsduInfo)
{
	if ((ucChksumFlag & TX_CS_IP_GEN))
		HAL_MAC_CONNAC2X_TXD_SET_IP_CHKSUM(
			(struct HW_MAC_CONNAC2X_TX_DESC *)prTxDesc);
	if ((ucChksumFlag & TX_CS_TCP_UDP_GEN))
		HAL_MAC_CONNAC2X_TXD_SET_TCP_UDP_CHKSUM(
			(struct HW_MAC_CONNAC2X_TX_DESC *)prTxDesc);
	/*
	 * If kernel do not expect HW checksum for this frame, set ~AMSDU.
	 * The ICMP frame check were done by checking pfTxDoneHandler
	 * in nic_txd_*_compose().
	 * In that case ICMP do not need HW checksum would cause following
	 * frames need checksum but skipped, but only happened if IcmpTxs
	 * were disabled for special test case.
	 */
	if (!(ucChksumFlag & (TX_CS_IP_GEN | TX_CS_TCP_UDP_GEN)) &&
	    prMsduInfo->ucPktType != ENUM_PKT_ICMP)
		HAL_MAC_CONNAC2X_TXD_UNSET_HW_AMSDU(
			(struct HW_MAC_CONNAC2X_TX_DESC *)prTxDesc);
}
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD == 1 */

void nic_txd_v2_header_format_op(
	void *prTxDesc,
	struct MSDU_INFO *prMsduInfo)
{
	if (!prMsduInfo->fgIs802_11) {
		if (prMsduInfo->fgIs802_3)
			HAL_MAC_CONNAC2X_TXD_UNSET_ETHERNET_II(
				(struct HW_MAC_CONNAC2X_TX_DESC *)prTxDesc);
		if (prMsduInfo->fgIsVlanExists)
			HAL_MAC_CONNAC2X_TXD_SET_VLAN(
				(struct HW_MAC_CONNAC2X_TX_DESC *)prTxDesc);
	}
}

void nic_txd_v2_fill_by_pkt_option(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo,
	void *prTxD)
{
	struct HW_MAC_CONNAC2X_TX_DESC *prTxDesc =
				(struct HW_MAC_CONNAC2X_TX_DESC *)prTxD;
	uint32_t u4PktOption = prMsduInfo->u4Option;
	u_int8_t fgIsLongFormat;
	u_int8_t fgProtected = FALSE;

	/* Skip this function if no options is set */
	if (!u4PktOption)
		return;

	fgIsLongFormat = HAL_MAC_CONNAC2X_TXD_IS_LONG_FORMAT(prTxDesc);

	/* Fields in DW0 and DW1 (Short Format) */
	if (u4PktOption & MSDU_OPT_NO_ACK)
		HAL_MAC_CONNAC2X_TXD_SET_NO_ACK(prTxDesc);

	if (u4PktOption & MSDU_OPT_PROTECTED_FRAME) {
		/* DBGLOG(RSN, INFO, "MSDU_OPT_PROTECTED_FRAME\n"); */
		HAL_MAC_CONNAC2X_TXD_SET_PROTECTION(prTxDesc);
		fgProtected = TRUE;
	}

	switch (HAL_MAC_CONNAC2X_TXD_GET_HEADER_FORMAT(prTxDesc)) {
	case HEADER_FORMAT_802_11_ENHANCE_MODE:
		if (u4PktOption & MSDU_OPT_EOSP)
			HAL_MAC_CONNAC2X_TXD_SET_EOSP(prTxDesc);

		if (u4PktOption & MSDU_OPT_AMSDU)
			HAL_MAC_CONNAC2X_TXD_SET_AMSDU(prTxDesc);
		break;

	case HEADER_FORMAT_NON_802_11:
		if (u4PktOption & MSDU_OPT_EOSP)
			HAL_MAC_CONNAC2X_TXD_SET_EOSP(prTxDesc);

		if (u4PktOption & MSDU_OPT_MORE_DATA)
			HAL_MAC_CONNAC2X_TXD_SET_MORE_DATA(prTxDesc);

		if (u4PktOption & MSDU_OPT_REMOVE_VLAN)
			HAL_MAC_CONNAC2X_TXD_SET_REMOVE_VLAN(prTxDesc);
		break;

	case HEADER_FORMAT_802_11_NORMAL_MODE:
		if (fgProtected && prMsduInfo->prPacket) {
			struct WLAN_MAC_HEADER *prWlanHeader =
			    (struct WLAN_MAC_HEADER *)
			    ((uintptr_t) (prMsduInfo->prPacket)
				+ MAC_TX_RESERVED_FIELD);

			prWlanHeader->u2FrameCtrl |= MASK_FC_PROTECTED_FRAME;
		}
		break;

	default:
		break;
	}

	if (!fgIsLongFormat)
		return;

	/* Fields in DW2~6 (Long Format) */
	if (u4PktOption & MSDU_OPT_NO_AGGREGATE)
		HAL_MAC_CONNAC2X_TXD_SET_BA_DISABLE(prTxDesc);

	if (u4PktOption & MSDU_OPT_TIMING_MEASURE)
		HAL_MAC_CONNAC2X_TXD_SET_TIMING_MEASUREMENT(prTxDesc);

	if (u4PktOption & MSDU_OPT_NDP)
		HAL_MAC_CONNAC2X_TXD_SET_NDP(prTxDesc);

	if (u4PktOption & MSDU_OPT_NDPA)
		HAL_MAC_CONNAC2X_TXD_SET_NDPA(prTxDesc);

	if (u4PktOption & MSDU_OPT_SOUNDING)
		HAL_MAC_CONNAC2X_TXD_SET_SOUNDING_FRAME(prTxDesc);

	if (u4PktOption & MSDU_OPT_FORCE_RTS)
		HAL_MAC_CONNAC2X_TXD_SET_FORCE_RTS_CTS(prTxDesc);

	if (u4PktOption & MSDU_OPT_BIP)
		HAL_MAC_CONNAC2X_TXD_SET_BIP(prTxDesc);

	/* SW field */
	if (u4PktOption & MSDU_OPT_SW_DURATION)
		HAL_MAC_CONNAC2X_TXD_SET_DURATION_CONTROL_BY_SW(prTxDesc);

	if (u4PktOption & MSDU_OPT_SW_PS_BIT)
		HAL_MAC_CONNAC2X_TXD_SET_SW_PM_CONTROL(prTxDesc);

	if (u4PktOption & MSDU_OPT_SW_HTC)
		HAL_MAC_CONNAC2X_TXD_SET_HTC_EXIST(prTxDesc);
#if 0
	if (u4PktOption & MSDU_OPT_SW_BAR_SN)
		HAL_MAC_TX_DESC_SET_SW_BAR_SSN(prTxDesc);
#endif
	if (u4PktOption & MSDU_OPT_MANUAL_SN) {
		HAL_MAC_CONNAC2X_TXD_SET_TXD_SN_VALID(prTxDesc);
		HAL_MAC_CONNAC2X_TXD_SET_SEQUENCE_NUMBER
			(prTxDesc, prMsduInfo->u2SwSN);
	}

}

/*----------------------------------------------------------------------------*/
/*!
* @brief In this function, we'll compose the Tx descriptor of the MSDU.
*
* @param prAdapter              Pointer to the Adapter structure.
* @param prMsduInfo             Pointer to the Msdu info
* @param prTxDesc               Pointer to the Tx descriptor buffer
*
* @retval VOID
*/
/*----------------------------------------------------------------------------*/
void nic_txd_v2_compose(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo,
	u_int32_t u4TxDescLength,
	u_int8_t fgIsTemplate,
	u_int8_t *prTxDescBuffer)
{
	struct HW_MAC_CONNAC2X_TX_DESC *prTxDesc;
	struct STA_RECORD *prStaRec;
	struct BSS_INFO *prBssInfo;
	uint8_t ucEtherTypeOffsetInWord;
	u_int32_t u4TxDescAndPaddingLength;
	uint8_t ucWmmQueSet = 0, ucTarQueue, ucTarPort;
#if ((CFG_SISO_SW_DEVELOP == 1) || (CFG_SUPPORT_SPE_IDX_CONTROL == 1))
	enum ENUM_WF_PATH_FAVOR_T eWfPathFavor;
#endif
	struct WLAN_MAC_HEADER *prWlanHeader = NULL;
#if CFG_SUPPORT_TX_MGMT_USE_DATAQ
	uint8_t *pucBuff = NULL;
	uint32_t u4TxHeadRoomSize;
#endif

	prTxDesc = (struct HW_MAC_CONNAC2X_TX_DESC *) prTxDescBuffer;
	prBssInfo = GET_BSS_INFO_BY_INDEX(prAdapter, prMsduInfo->ucBssIndex);
	prStaRec = cnmGetStaRecByIndex(prAdapter, prMsduInfo->ucStaRecIndex);

	u4TxDescAndPaddingLength = u4TxDescLength + NIC_TX_DESC_PADDING_LENGTH;

	kalMemZero(prTxDesc, u4TxDescAndPaddingLength);

	nicTxForceAmsduForCert(prAdapter, (uint8_t *)prTxDesc);

	/* Ether-type offset */
	if (prMsduInfo->fgIs802_11) {
		ucEtherTypeOffsetInWord =
			(prAdapter->chip_info->pse_header_length
				+ prMsduInfo->ucMacHeaderLength
				+ prMsduInfo->ucLlcLength) >> 1;
	} else {
		ucEtherTypeOffsetInWord =
			((ETHER_HEADER_LEN - ETHER_TYPE_LEN)
				+ prAdapter->chip_info->pse_header_length) >> 1;
	}
	HAL_MAC_CONNAC2X_TXD_SET_ETHER_TYPE_OFFSET(prTxDesc,
		ucEtherTypeOffsetInWord);

	ucTarPort = nicTxGetTxDestPortIdxByTc(prMsduInfo->ucTC);
	if (ucTarPort == PORT_INDEX_MCU) {
		ucTarQueue = nicTxGetTxDestQIdxByTc(prMsduInfo->ucTC);
		if (prMsduInfo->ucControlFlag & MSDU_CONTROL_FLAG_FORCE_TX) {
#if (CFG_SUPPORT_FORCE_ALTX == 1)
			ucTarQueue = MAC_TXQ_ALTX_0_INDEX;
#else
			ucTarQueue |= MAC_TXQ_ALTX_0_INDEX;
#endif
		}
	} else {
		if (prBssInfo) {
			ucWmmQueSet = prBssInfo->ucWmmQueSet;
#if CFG_SUPPORT_DROP_INVALID_MSDUINFO
			if (fgIsTemplate != TRUE &&
				prMsduInfo->ucPacketType == TX_PACKET_TYPE_DATA
				&& ucWmmQueSet != prMsduInfo->ucWmmQueSet) {
				prMsduInfo->fgDrop = TRUE;
				DBGLOG(RSN, ERROR,
					"WmmQueSet mismatch[%u,%u,%u,%u]\n",
					prMsduInfo->ucBssIndex,
					prMsduInfo->ucStaRecIndex,
					ucWmmQueSet,
					prMsduInfo->ucWmmQueSet);
			}
#endif /* CFG_SUPPORT_DROP_INVALID_MSDUINFO */
		} else
			DBGLOG(TX, ERROR, "prBssInfo is NULL\n");

		ucTarQueue = nicTxGetTxDestQIdxByTc(prMsduInfo->ucTC);
#if (CFG_TX_RSRC_WMM_ENHANCE == 1)
/* Note for SDIO resource ctrl
* There are cases for TargetQ update
* 1. ResV1 + TC <= TC4 : WmmSet may greater than 0, go to update
* 2. ResV2 + TC <= TC4 : WmmSet always 0
* 3. ResV2 + TC >	TC4 : TargetQ prepared in nicTxGetTxDestQIdxByTc()
*/
		if ((ucTarPort == PORT_INDEX_LMAC) &&
			(prMsduInfo->ucTC <= TC4_INDEX))
#else
		if (ucTarPort == PORT_INDEX_LMAC)
#endif
		{
			if (prBssInfo) {
				ucTarQueue +=
				  (prBssInfo->ucWmmQueSet * WMM_AC_INDEX_NUM);
			}
		}
	}

#if (CFG_SUPPORT_DMASHDL_SYSDVT)
	if (prMsduInfo->ucPktType == ENUM_PKT_ICMP) {
		/* send packets to specific mapping queue for DMASHDL DVT */
		if (DMASHDL_DVT_QUEUE_MAPPING_TYPE1(prAdapter)) {
			ucTarQueue = DMASHDL_DVT_GET_MAPPING_QID(prAdapter);
			prMsduInfo->ucTarQueue = ucTarQueue;
			DMASHDL_DVT_SET_MAPPING_QID(prAdapter,
				(ucTarQueue + 1) % MAC_TXQ_AC33_INDEX);
		} else if (DMASHDL_DVT_QUEUE_MAPPING_TYPE2(prAdapter)) {
			ucTarQueue = DMASHDL_DVT_GET_MAPPING_QID(prAdapter);
			prMsduInfo->ucTarQueue = ucTarQueue;
			DMASHDL_DVT_SET_MAPPING_QID(prAdapter,
				(ucTarQueue + 1) % MAC_TXQ_AC2_INDEX);
		}
	}
#endif

	HAL_MAC_CONNAC2X_TXD_SET_QUEUE_INDEX(prTxDesc, ucTarQueue);

	/* BMC packet */
	if (prMsduInfo->ucStaRecIndex == STA_REC_INDEX_BMCAST) {
		HAL_MAC_CONNAC2X_TXD_SET_BMC(prTxDesc);

		/* Must set No ACK to mask retry bit in FC */
		HAL_MAC_CONNAC2X_TXD_SET_NO_ACK(prTxDesc);
	}
	/* WLAN index */
	prMsduInfo->ucWlanIndex = nicTxGetWlanIdx(prAdapter,
		prMsduInfo->ucBssIndex, prMsduInfo->ucStaRecIndex);

#if 0				/* DBG */
	DBGLOG(RSN, INFO,
	       "Tx WlanIndex = %d eAuthMode = %d\n", prMsduInfo->ucWlanIndex,
	       prAdapter->rWifiVar.rConnSettings.eAuthMode);
#endif
	HAL_MAC_CONNAC2X_TXD_SET_WLAN_INDEX(prTxDesc,
					prMsduInfo->ucWlanIndex);
	HAL_MAC_CONNAC2X_TXD_SET_VTA(prTxDesc, 1);

	/* Header format */
	if (prMsduInfo->fgIs802_11) {
		HAL_MAC_CONNAC2X_TXD_SET_HEADER_FORMAT(
			prTxDesc, HEADER_FORMAT_802_11_NORMAL_MODE);
		HAL_MAC_CONNAC2X_TXD_SET_802_11_HEADER_LENGTH(
			prTxDesc, (prMsduInfo->ucMacHeaderLength >> 1));
	} else {
		HAL_MAC_CONNAC2X_TXD_SET_HEADER_FORMAT(
			prTxDesc, HEADER_FORMAT_NON_802_11);
		HAL_MAC_CONNAC2X_TXD_SET_ETHERNET_II(prTxDesc);
	}

	/* Header Padding */
	HAL_MAC_CONNAC2X_TXD_SET_HEADER_PADDING(
		prTxDesc, NIC_TX_DESC_HEADER_PADDING_LENGTH);

	/* TID */
	HAL_MAC_CONNAC2X_TXD_SET_TID(prTxDesc, prMsduInfo->ucUserPriority);

	/* Protection */
	if (secIsProtectedFrame(prAdapter, prMsduInfo, prStaRec)) {
		/* Update Packet option, */
		/* PF bit will be set in nicTxFillDescByPktOption() */
		if ((prStaRec
			&& prStaRec->fgTransmitKeyExist) || fgIsTemplate) {
			DBGLOG_LIMITED(RSN, TRACE,
				"Set MSDU_OPT_PROTECTED_FRAME\n");
			nicTxConfigPktOption(
				prMsduInfo, MSDU_OPT_PROTECTED_FRAME, TRUE);

			if (prMsduInfo->fgIs802_1x &&
			    prMsduInfo->fgIs802_1x_NonProtected) {
				nicTxConfigPktOption(
					prMsduInfo,
					MSDU_OPT_PROTECTED_FRAME, FALSE);
				DBGLOG(RSN, LOUD,
					"Pairwise EAPoL not protect!\n");
			}
		} else if (prMsduInfo->ucStaRecIndex
				== STA_REC_INDEX_BMCAST) {/* BMC packet */
			nicTxConfigPktOption(
				prMsduInfo, MSDU_OPT_PROTECTED_FRAME, TRUE);
			DBGLOG(RSN, LOUD, "Protect BMC frame!\n");
		}
	}
#if (UNIFIED_MAC_TX_FORMAT == 1)
	/* Packet Format */
	HAL_MAC_CONNAC2X_TXD_SET_PKT_FORMAT(
		prTxDesc, prMsduInfo->ucPacketFormat);
#endif

	/* Own MAC */
	if (prBssInfo) {
		HAL_MAC_CONNAC2X_TXD_SET_OWN_MAC_INDEX(
			prTxDesc, prBssInfo->ucOwnMacIndex);
	}

	if (u4TxDescLength == NIC_TX_DESC_SHORT_FORMAT_LENGTH) {
		HAL_MAC_CONNAC2X_TXD_SET_SHORT_FORMAT(prTxDesc);

		/* Update Packet option */
		nic_txd_v2_fill_by_pkt_option(prAdapter, prMsduInfo, prTxDesc);

		/* Short format, Skip DW 2~6 */
		return;
	}
		HAL_MAC_CONNAC2X_TXD_SET_LONG_FORMAT(prTxDesc);

	/* Update Packet option */
	nic_txd_v2_fill_by_pkt_option(prAdapter, prMsduInfo, prTxDesc);

	/* Type */
	if (prMsduInfo->fgIs802_11) {
#if CFG_SUPPORT_TX_MGMT_USE_DATAQ
		if (prMsduInfo->ucPktType == ENUM_PKT_802_11_MGMT) {
			u4TxHeadRoomSize = NIC_TX_DESC_AND_PADDING_LENGTH +
			   prAdapter->chip_info->txd_append_size;

			kalGetPacketBuf(prMsduInfo->prPacket, &pucBuff);
			prWlanHeader =
				(struct WLAN_MAC_HEADER *)((uintptr_t)
				(pucBuff + u4TxHeadRoomSize));

			if (prMsduInfo->u4Option & MSDU_OPT_PROTECTED_FRAME)
				prWlanHeader->u2FrameCtrl |=
					MASK_FC_PROTECTED_FRAME;
		} else
#endif
			prWlanHeader =
				(struct WLAN_MAC_HEADER *)
				((uintptr_t)
				(prMsduInfo->prPacket) + MAC_TX_RESERVED_FIELD);

		HAL_MAC_CONNAC2X_TXD_SET_TYPE(
			prTxDesc,
			(prWlanHeader->u2FrameCtrl & MASK_FC_TYPE) >> 2);
		HAL_MAC_CONNAC2X_TXD_SET_SUB_TYPE(
			prTxDesc,
			(prWlanHeader->u2FrameCtrl & MASK_FC_SUBTYPE)
				>> OFFSET_OF_FC_SUBTYPE);

		HAL_MAC_CONNAC2X_TXD7_SET_TYPE(
			prTxDesc,
			(prWlanHeader->u2FrameCtrl & MASK_FC_TYPE) >> 2);
		HAL_MAC_CONNAC2X_TXD7_SET_SUB_TYPE(
			prTxDesc,
			(prWlanHeader->u2FrameCtrl & MASK_FC_SUBTYPE)
				>> OFFSET_OF_FC_SUBTYPE);
	}
	/* PID */
	if (prMsduInfo->pfTxDoneHandler) {
		prMsduInfo->ucPID = nicTxAssignPID(prAdapter,
				prMsduInfo->ucWlanIndex,
				prMsduInfo->ucPacketType); /* 0/1: data/mgmt */
		DBGLOG(TX, INFO, "TX[%s] WIDX[%u] PID[%u]\n",
			TXS_PACKET_TYPE[prMsduInfo->ucPktType],
			prMsduInfo->ucWlanIndex, prMsduInfo->ucPID);
		HAL_MAC_CONNAC2X_TXD_SET_PID(prTxDesc, prMsduInfo->ucPID);
		HAL_MAC_CONNAC2X_TXD_SET_TXS_TO_MCU(prTxDesc);
		/* TXS is MPDU based, AMSDU will cause TX skb leak in driver */
		HAL_MAC_CONNAC2X_TXD_UNSET_HW_AMSDU(prTxDesc);
		/* Save for matching TXS */
		prMsduInfo->ucTxdWlanIdx =
			HAL_MAC_CONNAC2X_TXD_GET_WLAN_INDEX(prTxDesc);

		DBGLOG(TX, TEMP,
			"TXS MSDU: w/w'/p/t/up=%u/%u/%u/%u/%u\n",
			prMsduInfo->ucWlanIndex,
			prMsduInfo->ucTxdWlanIdx,
			prMsduInfo->ucPID,
			prMsduInfo->ucTC,
			prMsduInfo->ucUserPriority);
	} else if (prAdapter->rWifiVar.ucDataTxDone == 2) {
		/* Log mode: only TxS to FW, no event to driver */
		HAL_MAC_CONNAC2X_TXD_SET_PID(
			prTxDesc, NIC_TX_DESC_PID_RESERVED);
		HAL_MAC_CONNAC2X_TXD_SET_TXS_TO_MCU(prTxDesc);
	}

#if CFG_SUPPORT_WIFI_SYSDVT
	if (prMsduInfo->pfTxDoneHandler) {
		DBGLOG(REQ, LOUD, "PacketType=%d\n",
			prMsduInfo->ucPacketType);
		if (is_frame_test(prAdapter, 0) == 1 &&
			prMsduInfo->ucPacketType == 0) { /* Data */
			prMsduInfo->ucPID = prAdapter->auto_dvt->txs.pid;
			HAL_MAC_CONNAC2X_TXD_SET_PID(prTxDesc,
				prAdapter->auto_dvt->txs.pid);
			HAL_MAC_CONNAC2X_TXD_SET_TXS_FORMAT(prTxDesc,
				prAdapter->auto_dvt->txs.format);
			send_add_txs_queue(prAdapter->auto_dvt->txs.pid,
				prMsduInfo->ucWlanIndex);
			DBGLOG(REQ, LOUD,
				"Send_add_txs_queue pid=%d auto_txs_format=%d\n",
				prMsduInfo->ucPID,
				prAdapter->auto_dvt->txs.format);
		} else if (is_frame_test(prAdapter, 0) == 2 &&
			prMsduInfo->ucPacketType == 1) { /* Mgmt */
			struct WLAN_MAC_HEADER *prWlanHeader =
			(struct WLAN_MAC_HEADER *)
			((uintptr_t)(prMsduInfo->prPacket) +
			MAC_TX_RESERVED_FIELD);

			if (((prWlanHeader->u2FrameCtrl &
				MASK_FC_TYPE) >> 2) == 0 &&
				((prWlanHeader->u2FrameCtrl & MASK_FC_SUBTYPE)
				>> OFFSET_OF_FC_SUBTYPE) == 8)
				;/* FC_TYPE_MGMT=0, SUBTYPE_BEACON=8 */
			else if (((prWlanHeader->u2FrameCtrl &
				MASK_FC_TYPE) >> 2) == 0) {
				prMsduInfo->ucPID =
					prAdapter->auto_dvt->txs.pid;
				HAL_MAC_CONNAC2X_TXD_SET_PID(prTxDesc,
					prAdapter->auto_dvt->txs.pid);
				HAL_MAC_CONNAC2X_TXD_SET_TXS_FORMAT(prTxDesc,
					prAdapter->auto_dvt->txs.format);
				send_add_txs_queue(prAdapter->auto_dvt->txs.pid,
					prMsduInfo->ucWlanIndex);
				DBGLOG(REQ, LOUD,
					"Send_add_txs_queue pid=%d auto_txs_format=%d\n",
					prMsduInfo->ucPID,
					prAdapter->auto_dvt->txs.format);
			} else {
				prMsduInfo->ucPID =
					prAdapter->auto_dvt->txs.pid;
				HAL_MAC_CONNAC2X_TXD_SET_PID(prTxDesc,
					prAdapter->auto_dvt->txs.pid);
				HAL_MAC_CONNAC2X_TXD_SET_TXS_FORMAT(prTxDesc,
					prAdapter->auto_dvt->txs.format);
				HAL_MAC_CONNAC2X_TXD_SET_NO_ACK(prTxDesc);
					send_add_txs_queue(
					prAdapter->auto_dvt->txs.pid,
					prMsduInfo->ucWlanIndex);
				DBGLOG(REQ, LOUD,
					"Send_add_txs_queue pid=%d auto_txs_format=%d\n",
					prMsduInfo->ucPID,
					prAdapter->auto_dvt->txs.format);
			}
		}
	}
#endif /* AUTOMATION */

	/* Remaining TX time */
	if (!(prMsduInfo->u4Option & MSDU_OPT_MANUAL_LIFE_TIME))
		prMsduInfo->u4RemainingLifetime =
			nicTxGetRemainingTxTimeByTc(prMsduInfo->ucTC);
	HAL_MAC_CONNAC2X_TXD_SET_REMAINING_LIFE_TIME_IN_MS(
		prTxDesc, prMsduInfo->u4RemainingLifetime);

	/* Tx count limit */
	if (!(prMsduInfo->u4Option & MSDU_OPT_MANUAL_RETRY_LIMIT)) {
		/* Note: BMC packet retry limit is set to unlimited */
		prMsduInfo->ucRetryLimit =
			nicTxGetTxCountLimitByTc(prMsduInfo->ucTC);
	}
	HAL_MAC_CONNAC2X_TXD_SET_REMAINING_TX_COUNT(
		prTxDesc, prMsduInfo->ucRetryLimit);

	/* Power Offset */
	HAL_MAC_CONNAC2X_TXD_SET_POWER_OFFSET(
		prTxDesc, prMsduInfo->cPowerOffset);

	/* Fix rate */
	switch (prMsduInfo->ucRateMode) {
	case MSDU_RATE_MODE_MANUAL_DESC:
		HAL_MAC_TX_DESC_SET_DW(
			prTxDesc, 6, 1, &prMsduInfo->u4FixedRateOption);
#if (CFG_SISO_SW_DEVELOP == 1 || CFG_SUPPORT_SPE_IDX_CONTROL == 1)
		/* Update spatial extension index setting */
		eWfPathFavor = wlanGetAntPathType(prAdapter, ENUM_WF_NON_FAVOR);
		if (prBssInfo) {
			HAL_MAC_CONNAC2X_TXD_SET_SPE_IDX(
				prTxDesc,
				wlanGetSpeIdx(prAdapter, prBssInfo->ucBssIndex,
					eWfPathFavor));
		}
#endif
		HAL_MAC_CONNAC2X_TXD_SET_SPE_IDX_SEL(prTxDesc,
			ENUM_SPE_SEL_BY_TXD);
		HAL_MAC_CONNAC2X_TXD_SET_FIXED_RATE_MODE_TO_DESC(prTxDesc);
		HAL_MAC_CONNAC2X_TXD_SET_FIXED_RATE_ENABLE(prTxDesc);

#if (CFG_SUPPORT_HE_ER == 1)
		if (prBssInfo &&
			(prBssInfo->ucErMode == RA_DCM ||
			prBssInfo->ucErMode == RA_ER_106)) {
			/* 2 HE LTF */
			HAL_MAC_CONNAC2X_TXD_SET_HE_LTF(prTxDesc, 1);
			/* 1.6us GI */
			HAL_MAC_CONNAC2X_TXD_SET_GI_TYPE(prTxDesc, 1);
			/* DBGLOG(TX, WARN, "nic_txd:LTF:2(%x,GI:1.6(%x", */
			/*    HAL_MAC_CONNAC2X_TXD_GET_HE_LTF(prTxDesc),    */
			/*    HAL_MAC_CONNAC2X_TXD_GET_GI_TYPE(prTxDesc));  */
		}
#endif
		break;

	case MSDU_RATE_MODE_MANUAL_CR:
		HAL_MAC_CONNAC2X_TXD_SET_FIXED_RATE_MODE_TO_CR(prTxDesc);
		HAL_MAC_CONNAC2X_TXD_SET_FIXED_RATE_ENABLE(prTxDesc);
		break;

	case MSDU_RATE_MODE_AUTO:
	default:
		break;
	}
}

void nic_txd_v2_set_pkt_fixed_rate_option_full(struct MSDU_INFO
	*prMsduInfo,
	uint16_t u2RateCode,
	uint8_t ucBandwidth,
	u_int8_t fgShortGI,
	u_int8_t fgLDPC,
	u_int8_t fgDynamicBwRts, u_int8_t fgBeamforming,
	uint8_t ucAntennaIndex)
{
	struct HW_MAC_CONNAC2X_TX_DESC rTxDesc;
	struct HW_MAC_CONNAC2X_TX_DESC *prTxDesc = &rTxDesc;

	kalMemZero(prTxDesc, NIC_TX_DESC_LONG_FORMAT_LENGTH);

	/* Follow the format of Tx descriptor DW 6 */
	HAL_MAC_CONNAC2X_TXD_SET_FR_RATE(prTxDesc, u2RateCode);

	if (ucBandwidth)
		HAL_MAC_CONNAC2X_TXD_SET_FR_BW(prTxDesc, ucBandwidth);
#if 0
	if (fgBeamforming)
		HAL_MAC_CONNAC2X_TXD_SET_FR_BF(prTxDesc);
#else
	DBGLOG(TX, ERROR, "%s:: Need BF owner to check this setting!\n",
			__func__);
#endif
	if (fgShortGI)
		HAL_MAC_CONNAC2X_TXD_SET_GI_TYPE(prTxDesc, SHORT_GI);

	if (fgLDPC)
		HAL_MAC_CONNAC2X_TXD_SET_LDPC(prTxDesc);

	if (fgDynamicBwRts)
		HAL_MAC_CONNAC2X_TXD_SET_FR_DYNAMIC_BW_RTS(prTxDesc);

	HAL_MAC_CONNAC2X_TXD_SET_FR_ANTENNA_ID(prTxDesc, ucAntennaIndex);

	/* Write back to RateOption of MSDU_INFO */
	HAL_MAC_TX_DESC_GET_DW(prTxDesc, 6, 1,
			       &prMsduInfo->u4FixedRateOption);

	prMsduInfo->ucRateMode = MSDU_RATE_MODE_MANUAL_DESC;

}

void nic_txd_v2_set_pkt_fixed_rate_option(
	struct ADAPTER *prAdapter,
	struct MSDU_INFO *prMsduInfo,
	uint16_t u2RateCode,
	uint8_t ucBandwidth,
	u_int8_t fgShortGI,
	u_int8_t fgDynamicBwRts)
{
	struct HW_MAC_CONNAC2X_TX_DESC rTxDesc;
	struct HW_MAC_CONNAC2X_TX_DESC *prTxDesc = &rTxDesc;

	kalMemZero(prTxDesc, NIC_TX_DESC_LONG_FORMAT_LENGTH);

	/* Follow the format of Tx descriptor DW 6 */
	HAL_MAC_CONNAC2X_TXD_SET_FR_RATE(prTxDesc, u2RateCode);

	if (ucBandwidth)
		HAL_MAC_CONNAC2X_TXD_SET_FR_BW(prTxDesc, ucBandwidth);

	if (fgShortGI)
		HAL_MAC_CONNAC2X_TXD_SET_GI_TYPE(prTxDesc, SHORT_GI);

	if (fgDynamicBwRts)
		HAL_MAC_CONNAC2X_TXD_SET_FR_DYNAMIC_BW_RTS(prTxDesc);

	/* Write back to RateOption of MSDU_INFO */
	HAL_MAC_TX_DESC_GET_DW(prTxDesc, 6, 1,
			       &prMsduInfo->u4FixedRateOption);

	prMsduInfo->ucRateMode = MSDU_RATE_MODE_MANUAL_DESC;

}

void nic_txd_v2_set_hw_amsdu_template(
	struct ADAPTER *prAdapter,
	struct STA_RECORD *prStaRec,
	u_int8_t ucTid,
	u_int8_t fgSet)
{
	struct HW_MAC_CONNAC2X_TX_DESC *prTxDesc;

	DBGLOG(QM, INFO,
		"Update HW Amsdu field of TXD template for STA[%u] Tid[%u]\n",
		prStaRec->ucIndex, ucTid);

	if (prStaRec->aprTxDescTemplate[ucTid]) {
		prTxDesc =
			(struct HW_MAC_CONNAC2X_TX_DESC *)
			prStaRec->aprTxDescTemplate[ucTid];
		if (fgSet)
			HAL_MAC_CONNAC2X_TXD_SET_HW_AMSDU(prTxDesc);
		else
			HAL_MAC_CONNAC2X_TXD_UNSET_HW_AMSDU(prTxDesc);
	}
}
#endif /* CFG_SUPPORT_CONNAC2X == 1 */
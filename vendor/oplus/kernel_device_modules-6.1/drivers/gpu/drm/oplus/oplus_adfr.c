/***************************************************************
** Copyright (C) 2018-2021 OPLUS. All rights reserved.
** File : oplus_adfr.c
** Description : ADFR kernel module
** Version : 1.0
** Date : 2021/07/09
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Gaoxiaolei      2021/07/09        1.0         Build this moudle
******************************************************************/
#include <linux/gpio/consumer.h>
#include "oplus_display_trace.h"
#include "mtk_drm_drv.h"
#include "mtk_drm_crtc.h"
#include "mtk_log.h"
#include "mtk_drm_mmp.h"
#include "mtk_dsi.h"
#include "oplus_adfr.h"
#include "oplus_drm_disp_panel.h"
#include "oplus_display_apollo_brightness.h"

#define OPLUS_ADFR_CONFIG_GLOBAL (1<<0)
#define OPLUS_ADFR_CONFIG_FAKEFRAME (1<<1)
#define OPLUS_ADFR_CONFIG_VSYNC_SWITCH (1<<2)
#define OPLUS_ADFR_CONFIG_VSYNC_SWITCH_MODE (1<<3)
#define OPLUS_ADFR_CONFIG_IDLE_MODE (1<<4)
#define OPLUS_ADFR_CONFIG_TEMPERATURE_DETECTION (1<<5)
#define OPLUS_ADFR_CONFIG_DECREASING_STEP (1<<9)

#define OPLUS_ADFR_DEBUG_GLOBAL_DISABLE (1<<0)
#define OPLUS_ADFR_DEBUG_FAKEFRAME_DISABLE (1<<1)
#define OPLUS_ADFR_DEBUG_VSYNC_SWITCH_DISABLE (1<<2)
#define OPLUS_ADFR_DEBUG_IDLE_MODE_DISABLE (1<<4)
#define OPLUS_ADFR_DEBUG_TEMPERATURE_DETECTION_DISABLE (1<<5)
#define OPLUS_ADFR_DEBUG_DECREASING_STEP_DISABLE (1<<9)

#define ADFR_GET_GLOBAL_CONFIG(config) ((config) & OPLUS_ADFR_CONFIG_GLOBAL)
#define ADFR_GET_FAKEFRAME_CONFIG(config) ((config) & OPLUS_ADFR_CONFIG_FAKEFRAME)
#define ADFR_GET_VSYNC_SWITCH_CONFIG(config) ((config) & OPLUS_ADFR_CONFIG_VSYNC_SWITCH)
#define ADFR_GET_VSYNC_SWITCH_MODE(config) ((config) & OPLUS_ADFR_CONFIG_VSYNC_SWITCH_MODE)
#define ADFR_GET_IDLE_MODE_CONFIG(config) ((config) & OPLUS_ADFR_CONFIG_IDLE_MODE)
#define ADFR_GET_TEMPERATURE_DETECTION_CONFIG(config) ((config) & OPLUS_ADFR_CONFIG_TEMPERATURE_DETECTION)
#define OPLUS_ADFR_GET_DECREASING_STEP_CONFIG(config) ((config) & OPLUS_ADFR_CONFIG_DECREASING_STEP)

#define MTK_DISP_EVENT_ADFR_MIN_FPS	0x15

/* -------------------- parameters -------------------- */
/* log level config */
unsigned int oplus_adfr_log_level = OPLUS_ADFR_LOG_LEVEL_DEBUG;
EXPORT_SYMBOL(oplus_adfr_log_level);
/* dual display id */
unsigned int oplus_adfr_display_id = OPLUS_ADFR_PRIMARY_DISPLAY;
EXPORT_SYMBOL(oplus_adfr_display_id);

/* global variable */
int g_commit_pid = 0;
EXPORT_SYMBOL(g_commit_pid);
static struct drm_device *drm_dev;
struct mutex multite_mutex;

/*** 0st bit: adfr global on/off
	** 1st bit: fakeframe on/off
	** 2st bit; switch on/off
	** 3rd bit; 0:double-TE switch 1:external TE/TP switch
	*/
u32 oplus_adfr_config = 0;
EXPORT_SYMBOL(oplus_adfr_config);
static u32 oplus_adfr_debug = 0;

struct oplus_adfr_test_te_params test_te;

/* fakeframe */
unsigned long long ff_last_te_time = 0;

/* vsync switch */
static u32 vsync_switch_gpio_level = OPLUS_VSYNC_SWITCH_TP;
static bool force_te_vsync_mode = false;

/* disable multi-TE when backlight updated */
bool oplus_adfr_need_filter_backlight_cmd = false;
bool enable_multite = false;
EXPORT_SYMBOL(enable_multite);
bool oplus_adfr_get_multite_state(void);
void oplus_adfr_set_multite_state(bool state);
int oplus_mtk_dsi_panel_send_multite(struct drm_crtc *crtc, bool enable);

/* samsung auto mode */
bool oplus_adfr_auto_mode_updated = false;
static u32 oplus_adfr_auto_mode = OPLUS_ADFR_AUTO_OFF;
bool oplus_adfr_auto_fakeframe_updated = false;
u32 oplus_adfr_auto_fakeframe = 0;
bool oplus_adfr_auto_min_fps_updated = false;
static u32 oplus_adfr_auto_min_fps = 0;
bool oplus_adfr_need_filter_auto_on_cmd = false;
static u32 oplus_tmp_auto_mode = 0;
static u32 oplus_tmp_auto_fakeframe = 0;
static u32 oplus_tmp_auto_min_fps = 0;
static bool oplus_adfr_skip_min_fps_cmd = false;

/* idle mode */
static u32 oplus_adfr_idle_mode = OPLUS_ADFR_IDLE_OFF;

/* external variable/function declaration */
extern void mtk_crtc_cmdq_timeout_cb(struct cmdq_cb_data data);
extern void lcm_cmd_cmdq_cb(struct cmdq_cb_data data);
extern void lcdinfo_notify(unsigned long val, void *v);

/* osync mode timer */
enum hrtimer_restart oplus_adfr_osync_mode_timer_handler(struct hrtimer *timer);

/* --------------- adfr misc ---------------*/
char *oplus_display_get_panel_name(struct drm_crtc *crtc) {
	char *panel_name = "";
	struct mtk_drm_crtc *mtk_crtc;
	struct mtk_ddp_comp *comp;

	if (!crtc) {
		ADFR_ERR("find crtc fail\n");
		return panel_name;
	}

	mtk_crtc = to_mtk_crtc(crtc);
	comp = mtk_ddp_comp_request_output(mtk_crtc);

	if (!mtk_crtc || !comp || !comp->funcs || !comp->funcs->io_cmd) {
		ADFR_ERR("Invalid params");
		return panel_name;
	}

	mtk_ddp_comp_io_cmd(comp, NULL, GET_PANEL_NAME, &panel_name);

	return panel_name;
}

static struct drm_display_mode *get_mode_by_id(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}

	return NULL;
}

void oplus_adfr_init(struct drm_device *dev, struct mtk_drm_private *priv)
{
	drm_dev = dev;
	if (oplus_adfr_is_support()) {
		if (oplus_adfr_fakeframe_is_enable()) {
			priv->fakeframe_wq = create_singlethread_workqueue("fakeframe");
			hrtimer_init(&priv->fakeframe_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
			priv->fakeframe_timer.function = oplus_fakeframe_timer_handler;
			INIT_WORK(&priv->fakeframe_work, oplus_fakeframe_work_handler);
		}

		if (oplus_adfr_vsync_switch_is_enable()) {
			/* add for mux switch control */
			init_completion(&priv->switch_te_gate);
			priv->vsync_switch_wq = create_singlethread_workqueue("vsync_switch");
			INIT_WORK(&priv->vsync_switch_work, oplus_adfr_vsync_switch_work_handler);
		}

		/* osync mode timer init */
		priv->osync_mode_wq = create_singlethread_workqueue("osync");
		hrtimer_init(&priv->osync_mode_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		priv->osync_mode_timer.function = oplus_adfr_osync_mode_timer_handler;
		INIT_WORK(&priv->osync_mode_work, oplus_adfr_osync_mode_work_handler);
		mutex_init(&multite_mutex);
	}
}

inline bool oplus_adfr_is_support(void)
{
	return	(bool)(ADFR_GET_GLOBAL_CONFIG(oplus_adfr_config) &&
		!(oplus_adfr_debug & OPLUS_ADFR_DEBUG_GLOBAL_DISABLE));
}
EXPORT_SYMBOL(oplus_adfr_is_support);

inline bool oplus_adfr_fakeframe_is_enable(void)
{
	return (bool)(ADFR_GET_FAKEFRAME_CONFIG(oplus_adfr_config) &&
		!(oplus_adfr_debug & OPLUS_ADFR_DEBUG_FAKEFRAME_DISABLE) &&
		oplus_adfr_auto_fakeframe);
}
EXPORT_SYMBOL(oplus_adfr_fakeframe_is_enable);

bool oplus_adfr_vsync_switch_is_enable(void)
{
	return (bool)(ADFR_GET_VSYNC_SWITCH_CONFIG(oplus_adfr_config) &&
		!(oplus_adfr_debug & OPLUS_ADFR_DEBUG_VSYNC_SWITCH_DISABLE));
}

enum oplus_vsync_mode oplus_adfr_get_vsync_mode(void)
{
	if (!oplus_adfr_vsync_switch_is_enable()) {
		return OPLUS_INVALID_VSYNC;
	}

	return (enum oplus_vsync_mode)ADFR_GET_VSYNC_SWITCH_MODE(oplus_adfr_config);
}

inline bool oplus_adfr_idle_mode_is_enable(void)
{
	return (bool)(ADFR_GET_IDLE_MODE_CONFIG(oplus_adfr_config) &&
		!(oplus_adfr_debug & OPLUS_ADFR_DEBUG_IDLE_MODE_DISABLE));
}

inline bool oplus_adfr_temperature_detection_is_enable(void)
{
	return (bool)(ADFR_GET_TEMPERATURE_DETECTION_CONFIG(oplus_adfr_config) &&
		!(oplus_adfr_debug & OPLUS_ADFR_DEBUG_TEMPERATURE_DETECTION_DISABLE));
}

bool oplus_adfr_decreasing_step_is_enabled(void)
{
	return (bool)(OPLUS_ADFR_GET_DECREASING_STEP_CONFIG(oplus_adfr_config) &&
		!(oplus_adfr_debug & OPLUS_ADFR_DEBUG_DECREASING_STEP_DISABLE));
}

ssize_t oplus_adfr_get_debug(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	pr_err("kVRR get adfr config %#X debug %#X \n", oplus_adfr_config, oplus_adfr_debug);

	return scnprintf(buf, PAGE_SIZE, "debug:0x%08X config:0x%08X auto_mode:0x%08X fakeframe:0x%08X auto_minfps:0x%08X\n",
		oplus_adfr_debug, oplus_adfr_config, oplus_adfr_auto_mode, oplus_adfr_auto_fakeframe, oplus_adfr_auto_min_fps);
}
EXPORT_SYMBOL(oplus_adfr_get_debug);

ssize_t oplus_adfr_set_debug(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%u", &oplus_adfr_debug);
	pr_err("kVRR get adfr config %#X debug %#X \n", oplus_adfr_config, oplus_adfr_debug);

	return count;
}
EXPORT_SYMBOL(oplus_adfr_set_debug);

ssize_t oplus_adfr_get_params(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	pr_err("kVRR adfr config:%d,auto mode:%d,fakeframe:%d,autominfps:%d\n",
		oplus_adfr_config, oplus_adfr_auto_mode, oplus_adfr_auto_fakeframe, oplus_adfr_auto_min_fps);

	return scnprintf(buf, PAGE_SIZE, "config:%d,auto_mode:0x%08X fakeframe:0x%08X auto_minfps:0x%08X\n",
		oplus_adfr_config, oplus_adfr_auto_mode, oplus_adfr_auto_fakeframe, oplus_adfr_auto_min_fps);
}
EXPORT_SYMBOL(oplus_adfr_get_params);

ssize_t oplus_adfr_set_params(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	sscanf(buf, "%u %u %u %u", &oplus_adfr_config, &oplus_tmp_auto_mode, &oplus_tmp_auto_fakeframe, &oplus_tmp_auto_min_fps);

	if (oplus_adfr_auto_mode != oplus_tmp_auto_mode) {
		oplus_adfr_auto_mode = oplus_tmp_auto_mode;
		oplus_adfr_auto_mode_updated = true;
	}
	if (oplus_adfr_auto_fakeframe != oplus_tmp_auto_fakeframe) {
		oplus_adfr_auto_fakeframe = oplus_tmp_auto_fakeframe;
		oplus_adfr_auto_fakeframe_updated = true;
	}
	if (oplus_adfr_auto_min_fps != oplus_tmp_auto_min_fps) {
		oplus_adfr_auto_min_fps = oplus_tmp_auto_min_fps;
		oplus_adfr_auto_min_fps_updated = true;
	}

	pr_err("kVRR adfr_set_params config:%u,automode:%u,u:%u,fakeframe:%u,u:%u,minfps:%u,u:%u\n",
		oplus_adfr_config, oplus_adfr_auto_mode, oplus_adfr_auto_mode_updated, oplus_adfr_auto_fakeframe,
			oplus_adfr_auto_fakeframe_updated, oplus_adfr_auto_min_fps, oplus_adfr_auto_min_fps_updated);

	return count;
}
EXPORT_SYMBOL(oplus_adfr_set_params);

int set_adfr_params(int config, int auto_mode, int fakeframe, int min_fps)
{
	oplus_adfr_config = config;
	oplus_adfr_auto_mode = auto_mode;
	oplus_adfr_auto_fakeframe = fakeframe;
	oplus_adfr_auto_min_fps = min_fps;

	if (oplus_adfr_auto_mode != 0)
		oplus_adfr_auto_mode_updated = true;
	if (oplus_adfr_auto_fakeframe != 0)
		oplus_adfr_auto_fakeframe_updated = true;
	if (oplus_adfr_auto_min_fps != 0)
		oplus_adfr_auto_min_fps_updated = true;

	pr_err("kVRR adfr_set_params config:%u,automode:%u,u:%u,fakeframe:%u,u:%u,minfps:%u,u:%u\n",
		oplus_adfr_config, oplus_adfr_auto_mode, oplus_adfr_auto_mode_updated, oplus_adfr_auto_fakeframe,
			oplus_adfr_auto_fakeframe_updated, oplus_adfr_auto_min_fps, oplus_adfr_auto_min_fps_updated);

	return 0;
}
EXPORT_SYMBOL(set_adfr_params);

/* osync mode timer */
enum hrtimer_restart oplus_adfr_osync_mode_timer_handler(struct hrtimer *timer)
{
	struct mtk_drm_private *priv =
			container_of(timer, struct mtk_drm_private, osync_mode_timer);

	if (priv != NULL)
		queue_work(priv->osync_mode_wq, &priv->osync_mode_work);
	return HRTIMER_NORESTART;
}

void oplus_adfr_osync_mode_work_handler(struct work_struct *work_item)
{
	struct mtk_drm_private *priv =
			container_of(work_item, struct mtk_drm_private, osync_mode_work);

	mutex_lock(&multite_mutex);
	priv->osync_mode_recovery = true;
	mutex_unlock(&multite_mutex);
	ADFR_INFO("osyncmode_work_handler, priv->osync_mode_recovery=%d\n", priv->osync_mode_recovery);
}

int oplus_adfr_osync_mode_timer_start(int deferred_ms)
{
	struct mtk_drm_private *private = NULL;

	private = drm_dev->dev_private;
	if (!drm_dev) {
		ADFR_INFO("%s:%d, drm_dev is NULL\n",
			__func__, __LINE__);
		return -1;
	}
	if (!private) {
		ADFR_INFO("%s:%d, drm_dev->dev_private is NULL\n",
			__func__, __LINE__);
		return -1;
	}
	ADFR_INFO("kVRR osync_mode_timer start,defer time=%d\n", deferred_ms);
	hrtimer_start(&private->osync_mode_timer, ms_to_ktime(deferred_ms), HRTIMER_MODE_REL);
	return 0;
}

/* --------------- fake frame --------------- */
int oplus_mtk_send_fakeframe(struct drm_crtc *crtc, bool sync)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct cmdq_pkt *cmdq_handle;
	struct mtk_crtc_state *crtc_state;
	struct mtk_ddp_comp *comp = mtk_ddp_comp_request_output(mtk_crtc);
	struct mtk_cmdq_cb_data *cb_data;
	unsigned long long current_ts;
	uint32_t v_refresh_ns = 0;
	bool is_frame_mode;
	int vrefresh = 0;
	crtc_state = to_mtk_crtc_state(crtc->state);
	if (!mtk_crtc->enabled || crtc_state->prop_val[CRTC_PROP_DOZE_ACTIVE]) {
		DDPINFO("%s:%d, crtc is not reusmed!\n", __func__, __LINE__);
		return -1;
	}

	if (!mtk_crtc->panel_ext || !(mtk_crtc->panel_ext->params->oplus_fakeframe_cfg & 0x00000001)) {
		ADFR_ERR("panel_ext or oplus_fakeframe_cfg is null\n");
		return -1;
	}
	if (!(comp && comp->funcs && comp->funcs->io_cmd)) {
		ADFR_ERR("comp or func or io_cmd is null\n");
		return -1;
	}

	current_ts = sched_clock();
	vrefresh = drm_mode_vrefresh(&crtc->state->mode);
	if (vrefresh == 120)
		v_refresh_ns = VREFRESH_120_NS;
	else if (vrefresh == 90)
		v_refresh_ns = VREFRESH_90_NS;
	else
		v_refresh_ns = VREFRESH_120_NS;

	ADFR_INFO("current_ts=%llu,rdma_start_time+v_refresh_ns=%llu,sync=%d\n", current_ts, last_rdma_start_time+v_refresh_ns, sync);
	if (current_ts < (last_rdma_start_time + v_refresh_ns))
		return -1;

	is_frame_mode = mtk_crtc_is_frame_trigger_mode(&mtk_crtc->base);

	if (is_frame_mode) {
		ff_last_te_time = oplus_last_te_time;
		mtk_drm_idlemgr_kick(__func__, crtc, 0);
		mtk_crtc_pkt_create(&cmdq_handle, &mtk_crtc->base,
						mtk_crtc->gce_obj.client[CLIENT_CFG]);

		if (mtk_crtc_with_sub_path(crtc, mtk_crtc->ddp_mode))
			mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle,
				DDP_SECOND_PATH, 0);
		else
			mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle,
				DDP_FIRST_PATH, 0);

		cmdq_pkt_clear_event(cmdq_handle,
			mtk_crtc->gce_obj.event[EVENT_STREAM_BLOCK]);
		cmdq_pkt_wfe(cmdq_handle,
					 mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);
		ADFR_INFO("send fake frames\n");

		if (comp && comp->funcs && comp->funcs->io_cmd)
			comp->funcs->io_cmd(comp, cmdq_handle, PANEL_FAKE_FRAME, NULL);

		cmdq_pkt_set_event(cmdq_handle,
			mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);
		cmdq_pkt_set_event(cmdq_handle,
			mtk_crtc->gce_obj.event[EVENT_STREAM_BLOCK]);
		if (sync) {
			cmdq_pkt_flush(cmdq_handle);
			cmdq_pkt_destroy(cmdq_handle);
		} else {
			cb_data = kmalloc(sizeof(*cb_data), GFP_KERNEL);
			if (cb_data) {
				cb_data->cmdq_handle = cmdq_handle;
				cb_data->misc = 2;
				cmdq_pkt_flush_threaded(cmdq_handle, lcm_cmd_cmdq_cb, cb_data);
			} else {
				DDPPR_ERR("%s cb data create failed, sync flush instead...\n", __func__);
				cmdq_pkt_flush(cmdq_handle);
				cmdq_pkt_destroy(cmdq_handle);
			}
		}
	}

	return 0;
}

int oplus_mtk_send_ext_fakeframe(struct drm_crtc *crtc, bool need_lock)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct cmdq_pkt *cmdq_handle;
	struct mtk_crtc_state *crtc_state;
	struct mtk_ddp_comp *comp = mtk_ddp_comp_request_output(mtk_crtc);
	struct mtk_cmdq_cb_data *cb_data;
	unsigned long long current_ts;
	uint32_t v_refresh_ns = 0;
	int vrefresh = 0;
	bool is_frame_mode;

	crtc_state = to_mtk_crtc_state(crtc->state);
	if (!mtk_crtc->enabled || crtc_state->prop_val[CRTC_PROP_DOZE_ACTIVE]) {
		ADFR_WARN("%s:%d, crtc is not reusmed!\n", __func__, __LINE__);
		return -1;
	}
	if (!mtk_crtc->panel_ext || !(mtk_crtc->panel_ext->params->oplus_fakeframe_cfg & 0x00000001)) {
		return -1;
	}
	if (!(comp && comp->funcs && comp->funcs->io_cmd))
		return -1;

	current_ts = sched_clock();
	vrefresh = drm_mode_vrefresh(&crtc->state->mode);
	if (vrefresh == 120)
		v_refresh_ns = VREFRESH_120_NS;
	else if (vrefresh == 90)
		v_refresh_ns = VREFRESH_90_NS;
	else
		v_refresh_ns = VREFRESH_120_NS;

	ADFR_INFO("current_ts=%llu,rdma_start_time+v_refresh_ns=%llu\n", current_ts, last_rdma_start_time+v_refresh_ns);
	if (current_ts < (last_rdma_start_time + v_refresh_ns))
		return -1;

	/* record ff_last_te_time to avoid send ff in one TE interval */
	if (oplus_last_te_time == ff_last_te_time)
		return -1;

	if (need_lock)
		DDP_MUTEX_LOCK(&mtk_crtc->lock, __func__, __LINE__);
	is_frame_mode = mtk_crtc_is_frame_trigger_mode(&mtk_crtc->base);
	if (is_frame_mode) {
		mtk_drm_idlemgr_kick(__func__, crtc, 1);
		cmdq_handle = cmdq_pkt_create(mtk_crtc->gce_obj.client[CLIENT_CFG]);

		cmdq_pkt_wfe(cmdq_handle, mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);

		(cmdq_handle)->err_cb.cb = mtk_crtc_cmdq_timeout_cb;
		(cmdq_handle)->err_cb.data = crtc;

		/* send fakeframe */
		ADFR_INFO("send ext fake frames\n");
		if (comp && comp->funcs && comp->funcs->io_cmd)
			comp->funcs->io_cmd(comp, cmdq_handle, PANEL_FAKE_FRAME, NULL);

		cmdq_pkt_set_event(cmdq_handle, mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);
		oplus_disp_trace_c("%d|fftimer_start|%d", g_commit_pid, 1);
		oplus_disp_trace_c("%d|fftimer_start|%d", g_commit_pid, 0);

		cb_data = kmalloc(sizeof(*cb_data), GFP_KERNEL);
		if (cb_data) {
			cb_data->cmdq_handle = cmdq_handle;
			/* indicate timeout handler send ff */
			cb_data->misc = 3;
			cmdq_pkt_flush_threaded(cmdq_handle, lcm_cmd_cmdq_cb, cb_data);
		} else {
			if (need_lock)
				DDP_MUTEX_UNLOCK(&mtk_crtc->lock, __func__, __LINE__);
			return -1;
		}
	}

	if (need_lock)
		DDP_MUTEX_UNLOCK(&mtk_crtc->lock, __func__, __LINE__);

	return 0;
}

void oplus_adfr_send_fake_frame(struct drm_crtc *crtc) {
	int ret = 0;
	int deferred_ms = 0;
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);

	if (crtc == NULL)
		return;
	if (!oplus_adfr_fakeframe_is_enable()) {
		/*ADFR_DEBUG("fakeframe is off\n");*/
		return;
	}
	oplus_disp_trace_begin("atomic_flush:send_fakeframe");
	/* before commit send a fakeframe to triger the panel flush
	 * but if pre-frame is pending, ignore this time
	 * because pre-frame is a real frame, Not Need fakeframe
	 */
	ret = oplus_mtk_send_fakeframe(crtc, false);
	if (ret < 0) {
		ADFR_INFO("send fakeframe fail ret = %d\n", ret);
		oplus_disp_trace_end("atomic_flush:send_fakeframe");
		return;
	}
	deferred_ms = mtk_crtc->panel_ext->params->oplus_fakeframe_deferred_time;
	oplus_adfr_fakeframe_timer_start(deferred_ms);
	oplus_disp_trace_end("atomic_flush:send_fakeframe");
}

void oplus_fakeframe_work_handler(struct work_struct *work_item)
{
	struct mtk_drm_private *priv =
			container_of(work_item, struct mtk_drm_private, fakeframe_work);
	struct drm_crtc *crtc;
	int ret = 0;

	/* this debug cmd only for crtc0 */
	crtc = list_first_entry(&(priv->drm)->mode_config.crtc_list,
				typeof(*crtc), head);
	if (!crtc) {
		printk(KERN_ERR "find crtc fail\n");
		return;
	}
	/* return for cmdq internal err cause KE */
	return;

	ADFR_INFO("fakeframe_work_handler\n");
	ret = oplus_mtk_send_ext_fakeframe(crtc, true);
	if (ret < 0) {
		ADFR_INFO("send fakeframe fail ret = %d\n", ret);
	}
}

enum hrtimer_restart oplus_fakeframe_timer_handler(struct hrtimer *timer)
{
	struct mtk_drm_private *priv =
			container_of(timer, struct mtk_drm_private, fakeframe_timer);

	if (priv != NULL)
		queue_work(priv->fakeframe_wq, &priv->fakeframe_work);
	return HRTIMER_NORESTART;
}

int oplus_adfr_fakeframe_timer_start(int deferred_ms)
{
	struct mtk_drm_private *private = NULL;

	private = drm_dev->dev_private;
	if (!drm_dev) {
		ADFR_INFO("%s:%d, drm_dev is NULL\n",
			__func__, __LINE__);
		return -1;
	}
	if (!private) {
		ADFR_INFO("%s:%d, drm_dev->dev_private is NULL\n",
			__func__, __LINE__);
		return -1;
	}
	ADFR_INFO("defer time=%d,start_hrttimer!\n", deferred_ms);
	hrtimer_start(&private->fakeframe_timer, ms_to_ktime(deferred_ms), HRTIMER_MODE_REL);
	return 0;
}

/* cancel the fakeframe timer */
int oplus_adfr_cancel_fakeframe(void)
{
	struct mtk_drm_private *private = NULL;

	if (!oplus_adfr_fakeframe_is_enable()) {
		/*ADFR_INFO("fakeframe is off\n");*/
		return 0;
	}

	private = drm_dev->dev_private;
	if (!drm_dev) {
		ADFR_ERR("%s:%d, drm_dev is NULL\n",
			__func__, __LINE__);
		return -EINVAL;
	}
	if (!private) {
		ADFR_ERR("%s:%d, drm_dev->dev_private is NULL\n",
			__func__, __LINE__);
		return -EINVAL;
	}

	ADFR_INFO("oplus_adfr_cancel_fakeframe\n");
	hrtimer_cancel(&private->fakeframe_timer);

	return 0;
}

/* --------------- vsync switch ---------------*/
static int oplus_dsi_display_enable_and_waiting_for_next_te_irq(void)
{
	int const switch_te_timeout = msecs_to_jiffies(1100);

	struct mtk_drm_private *private = NULL;

	private = drm_dev->dev_private;
	if (!drm_dev || !private) {
		ADFR_ERR("Invalid params");
		return -EINVAL;
	}

	ADFR_INFO("Waiting for the next TE to switch\n");

	private->vsync_switch_pending = true;
	reinit_completion(&private->switch_te_gate);

	if (!wait_for_completion_timeout(&private->switch_te_gate, switch_te_timeout)) {
		ADFR_ERR("vsync switch TE check failed\n");
		return -EINVAL;
	}

	/* wait for te falling edge */
	udelay(1000);

	return 0;
}

/*GPIO SWITCH: 0-TP Vsync    1-TE Vsync*/
static int oplus_dsi_display_vsync_switch_check_te(struct mtk_ddp_comp *comp, int level)
{
	int rc = 0;

	if (!comp || !comp->funcs || !comp->funcs->io_cmd) {
		ADFR_ERR("Invalid params");
		return -EINVAL;
	}

	if (level == vsync_switch_gpio_level) {
		ADFR_INFO("vsync_switch_gpio is already %d\n", level);
		return 0;
	}

	if (force_te_vsync_mode == true) {
		ADFR_INFO("force te vsync, filter other vsync switch\n");
		return 0;
	}

	oplus_dsi_display_enable_and_waiting_for_next_te_irq();

	comp->funcs->io_cmd(comp, NULL, LCM_VSYNC_SWITCH, &level);
	ADFR_INFO("set vsync_switch_gpio to %d\n", level);
	vsync_switch_gpio_level = level;
	oplus_disp_trace_c("%d|vsync_switch_gpio_level|%d", g_commit_pid, vsync_switch_gpio_level);

	return rc;
}

static int oplus_dsi_display_set_vsync_switch_gpio(struct drm_crtc *crtc, int level)
{
	int rc = 0;
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_ddp_comp *comp = mtk_ddp_comp_request_output(mtk_crtc);

	/* only support in mux switch */
	if (oplus_adfr_get_vsync_mode() != OPLUS_EXTERNAL_TE_TP_VSYNC) {
		ADFR_ERR("%s is not supported\n", __func__);
		return -EINVAL;
	}

	if (crtc == NULL || mtk_crtc == NULL)
		return -EINVAL;

	if (!mtk_crtc->enabled) {
		/* set vsync switch */
		if (comp && comp->funcs && comp->funcs->io_cmd) {
			comp->funcs->io_cmd(comp, NULL, LCM_VSYNC_SWITCH, &level);
		}
		else {
			ADFR_ERR("failed to call LCM_VSYNC_SWITCH io_cmd\n");
			return -EINVAL;
		}
		ADFR_INFO("set vsync_switch_gpio to %d\n", level);
		vsync_switch_gpio_level = level;
		oplus_disp_trace_c("%d|vsync_switch_gpio_level|%d", g_commit_pid, vsync_switch_gpio_level);
	} else {
		oplus_dsi_display_vsync_switch_check_te(comp, level);
	}

	return rc;
}

/*GPIO SWITCH: 0-TP Vsync    1-TE Vsync*/
ssize_t oplus_set_vsync_switch(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	int vsync_switch_gpio = 0;
	struct drm_crtc *crtc;
	struct drm_device *ddev = get_drm_device();

	sscanf(buf, "%du", &vsync_switch_gpio);

	ADFR_INFO("oplus_set_vsync_switch = %d\n", vsync_switch_gpio);

	/* this debug cmd only for crtc0 */
	crtc = list_first_entry(&(ddev)->mode_config.crtc_list,
				typeof(*crtc), head);
	if (!crtc) {
		ADFR_ERR("find crtc fail\n");
		return count;
	}

	ret = oplus_dsi_display_set_vsync_switch_gpio(crtc, vsync_switch_gpio);
	if (ret)
		ADFR_ERR("oplus_dsi_display_set_vsync_switch_gpio(%d) fail\n", vsync_switch_gpio);

	return count;
}
EXPORT_SYMBOL(oplus_set_vsync_switch);

ssize_t oplus_get_vsync_switch(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", vsync_switch_gpio_level);
}
EXPORT_SYMBOL(oplus_get_vsync_switch);

void oplus_adfr_vsync_switch(struct drm_display_mode *m, bool force_te_vsync)
{
	int level = OPLUS_VSYNC_SWITCH_TP;
	int h_skew = SDC_ADFR;
	struct drm_crtc *crtc = NULL;
	struct mtk_drm_crtc *mtk_crtc = NULL;
	struct mtk_ddp_comp *comp = NULL;
	struct drm_device *ddev = get_drm_device();

	if (!oplus_adfr_vsync_switch_is_enable()) {
		return;
	}

	/* this debug cmd only for crtc0 */
	crtc = list_first_entry(&(ddev)->mode_config.crtc_list,
				typeof(*crtc), head);
	if (!crtc) {
		ADFR_ERR("find crtc fail\n");
		return;
	}

	mtk_crtc = to_mtk_crtc(crtc);
	comp = mtk_ddp_comp_request_output(mtk_crtc);
	if (!mtk_crtc || !comp || (m == NULL)) {
		ADFR_ERR("Invalid params");
		return;
	}

	if (force_te_vsync == true) {
		if (oplus_adfr_get_vsync_mode() == OPLUS_EXTERNAL_TE_TP_VSYNC) {
			if (vsync_switch_gpio_level == OPLUS_VSYNC_SWITCH_TP) {
				level = OPLUS_VSYNC_SWITCH_TE;
				oplus_dsi_display_vsync_switch_check_te(comp, level);
			}
		}
	} else {
		/* disable fake frame before vsync switch */
		oplus_adfr_auto_fakeframe = OPLUS_ADFR_FAKEFRAME_OFF;
		ADFR_INFO("fakeframe %d\n", oplus_adfr_auto_fakeframe);
		oplus_disp_trace_c("%d|oplus_adfr_auto_fakeframe|%d", g_commit_pid, oplus_adfr_auto_fakeframe);

		if (oplus_adfr_get_vsync_mode() != OPLUS_EXTERNAL_TE_TP_VSYNC) {
			ADFR_WARN("OPLUS_EXTERNAL_TE_TP_VSYNC is not supported\n");
			return;
		}

		h_skew = m->hskew;

		if (h_skew == OPLUS_ADFR) {
			level = OPLUS_VSYNC_SWITCH_TE;
		} else {
			level = OPLUS_VSYNC_SWITCH_TP;
		}

		oplus_dsi_display_vsync_switch_check_te(comp, level);
	}
}
EXPORT_SYMBOL(oplus_adfr_vsync_switch);

void oplus_adfr_vsync_switch_work_handler(struct work_struct *work_item)
{
	struct mtk_drm_private *priv =
			container_of(work_item, struct mtk_drm_private, vsync_switch_work);
	struct drm_crtc *crtc;
	struct mtk_drm_crtc *mtk_crtc;
	struct mtk_ddp_comp *comp;

	oplus_disp_trace_begin("oplus_adfr_frame_done_vsync_switch");
	if (!priv) {
		ADFR_ERR("Invalid params:priv");
		oplus_disp_trace_end("oplus_adfr_frame_done_vsync_switch");
		return;
	}

	/* this debug cmd only for crtc0 */
	crtc = list_first_entry(&(priv->drm)->mode_config.crtc_list,
				typeof(*crtc), head);
	if (!crtc) {
		ADFR_ERR("find crtc fail\n");
		oplus_disp_trace_end("oplus_adfr_frame_done_vsync_switch");
		return;
	}

	mtk_crtc = to_mtk_crtc(crtc);
	comp = mtk_ddp_comp_request_output(mtk_crtc);

	if (!mtk_crtc || !comp || !comp->funcs || !comp->funcs->io_cmd) {
		ADFR_ERR("Invalid params");
		oplus_disp_trace_end("oplus_adfr_frame_done_vsync_switch");
		return;
	}

	vsync_switch_gpio_level = OPLUS_VSYNC_SWITCH_TP;
	comp->funcs->io_cmd(comp, NULL, LCM_VSYNC_SWITCH, &vsync_switch_gpio_level);
	ADFR_INFO("set vsync_switch_gpio to 0\n");
	oplus_disp_trace_c("%d|vsync_switch_gpio_level|%d", g_commit_pid, vsync_switch_gpio_level);
	oplus_disp_trace_end("oplus_adfr_frame_done_vsync_switch");
}

void oplus_adfr_frame_done_vsync_switch(struct mtk_drm_crtc *mtk_crtc)
{
	struct mtk_drm_private *priv = mtk_crtc->base.dev->dev_private;

	if (oplus_adfr_get_vsync_mode() != OPLUS_EXTERNAL_TE_TP_VSYNC) {
		ADFR_WARN("OPLUS_EXTERNAL_TE_TP_VSYNC is not supported\n");
		return;
	}

	if (!mtk_crtc || !priv) {
		ADFR_ERR("Invalid params");
		return;
	}

	if (priv->need_vsync_switch) {
		oplus_disp_trace_begin("oplus_adfr_frame_done_vsync_switch");
		queue_work(priv->vsync_switch_wq, &priv->vsync_switch_work);
		ADFR_INFO("queue vsync switch work");
		priv->need_vsync_switch = false;
		oplus_disp_trace_end("oplus_adfr_frame_done_vsync_switch");
	}
}

/*
 if use TP when timing switch (resolution switch), tearing happen
 it seems like DDIC does not support MIPI offset writes after resolution switching
 TE is official, so do the TE switch after timing switch because MIPI will be reset after that
 if current use TE, do nothing
*/
void oplus_adfr_resolution_vsync_switch(struct mtk_drm_crtc *mtk_crtc, struct drm_connector *connector,
		unsigned int cur_mode, unsigned int dst_mode)
{
	struct mtk_ddp_comp *comp = mtk_ddp_comp_request_output(mtk_crtc);
	struct mtk_drm_private *priv = mtk_crtc->base.dev->dev_private;
	struct drm_display_mode *m = get_mode_by_id(connector, dst_mode);
	struct drm_display_mode *src_m = get_mode_by_id(connector, cur_mode);

	if (oplus_adfr_get_vsync_mode() != OPLUS_EXTERNAL_TE_TP_VSYNC) {
		ADFR_WARN("OPLUS_EXTERNAL_TE_TP_VSYNC is not supported\n");
		return;
	}

	if (!mtk_crtc || !comp || !comp->funcs || !comp->funcs->io_cmd || !priv || !connector) {
		ADFR_ERR("Invalid params");
		return;
	}

	/* just do switch when use tp vsync and resolution change */
	if ((src_m->hdisplay != m->hdisplay) && (vsync_switch_gpio_level == OPLUS_VSYNC_SWITCH_TP)) {
		vsync_switch_gpio_level = OPLUS_VSYNC_SWITCH_TE;
		comp->funcs->io_cmd(comp, NULL, LCM_VSYNC_SWITCH, &vsync_switch_gpio_level);
		ADFR_INFO("set vsync_switch_gpio to 1\n");

		/* after one frame commit completed, change back to current mode vsync */
		priv->need_vsync_switch = true;
		oplus_disp_trace_c("%d|vsync_switch_gpio_level|%d", g_commit_pid, vsync_switch_gpio_level);
	}
}

/* aod/fod vsync switch entry and exit */
void oplus_adfr_aod_fod_vsync_switch(struct mtk_drm_crtc *mtk_crtc, bool force_te_vsync)
{
	int h_skew = SDC_ADFR;
	struct mtk_ddp_comp *comp = NULL;
	struct mtk_drm_private *priv = NULL;

	if (oplus_adfr_get_vsync_mode() != OPLUS_EXTERNAL_TE_TP_VSYNC) {
		ADFR_WARN("OPLUS_EXTERNAL_TE_TP_VSYNC is not supported\n");
		return;
	}

	if (!mtk_crtc) {
		ADFR_ERR("Invalid params:mtk_crtc");
		return;
	}

	comp = mtk_ddp_comp_request_output(mtk_crtc);
	if (!comp || !comp->funcs || !comp->funcs->io_cmd) {
		ADFR_ERR("Invalid params:comp");
		return;
	}

	/* force switch to te vsync as tp vsync will change in aod mode */
	if (force_te_vsync == true) {
		if (vsync_switch_gpio_level == OPLUS_VSYNC_SWITCH_TP) {
			vsync_switch_gpio_level = OPLUS_VSYNC_SWITCH_TE;
			comp->funcs->io_cmd(comp, NULL, LCM_VSYNC_SWITCH, &vsync_switch_gpio_level);
			ADFR_INFO("set vsync_switch_gpio to 1\n");
			oplus_disp_trace_c("%d|vsync_switch_gpio_level|%d", g_commit_pid, vsync_switch_gpio_level);
		}
		force_te_vsync_mode = true;
	} else {
		/* change back to tp vysnc since aod/fod mode is off */
		if (force_te_vsync_mode == true) {
			if (vsync_switch_gpio_level == OPLUS_VSYNC_SWITCH_TE) {
				priv = mtk_crtc->base.dev->dev_private;
				if (!priv) {
					ADFR_ERR("Invalid params:priv");
					return;
				}

				h_skew = mtk_crtc->base.state->mode.hskew;
				/* maybe change to OA in aod/fod mode */
				if (h_skew == SDC_ADFR || h_skew == SDC_MFR) {
					vsync_switch_gpio_level = OPLUS_VSYNC_SWITCH_TP;
					comp->funcs->io_cmd(comp, NULL, LCM_VSYNC_SWITCH, &vsync_switch_gpio_level);
					ADFR_INFO("set vsync_switch_gpio to 0\n");
					oplus_disp_trace_c("%d|vsync_switch_gpio_level|%d", g_commit_pid, vsync_switch_gpio_level);
				}
			}
			force_te_vsync_mode = false;
		}
	}
}

/* --------------- auto mode --------------- */
bool oplus_adfr_auto_on_cmd_filter_set(bool enable)
{
	oplus_adfr_need_filter_auto_on_cmd = enable;
	return oplus_adfr_need_filter_auto_on_cmd;
}

bool oplus_adfr_auto_on_cmd_filter_get(void)
{
	return oplus_adfr_need_filter_auto_on_cmd;
}

void oplus_adfr_handle_auto_mode(int prop_id, unsigned int propval)
{
	int handled = 1;

	oplus_disp_trace_begin("oplus_adfr_handle_auto_mode");

	switch (prop_id) {
	case CRTC_PROP_AUTO_MODE:
		/* Add for auto on cmd filter */
		if (oplus_adfr_auto_on_cmd_filter_get() && (propval == OPLUS_ADFR_AUTO_ON)) {
			ADFR_WARN("auto off and auto on cmd are sent on the same frame, filter it\n");
			oplus_disp_trace_end("oplus_adfr_handle_auto_mode");
			handled = 1;
			return;
		}
		if (propval != oplus_adfr_auto_mode) {
			oplus_adfr_auto_mode_updated = true;
			/* when auto mode changes, write the corresponding min fps again */
			oplus_adfr_auto_min_fps_updated = true;
			oplus_adfr_auto_mode = propval;
			handled += 2;
			ADFR_WARN("update auto mode %u\n", propval);
		}
		break;

	case CRTC_PROP_AUTO_FAKE_FRAME:
		if (propval != oplus_adfr_auto_fakeframe) {
			oplus_adfr_auto_fakeframe_updated = true;
			oplus_adfr_auto_fakeframe = propval;
			handled += 4;
			ADFR_WARN("update fake frame %u\n", propval);
		}
		break;

	case CRTC_PROP_AUTO_MIN_FPS:
		if (propval != oplus_adfr_auto_min_fps) {
			oplus_adfr_auto_min_fps_updated = true;
			oplus_adfr_auto_min_fps = propval;
			handled += 8;
			ADFR_WARN("update minfps %u\n", propval);
		}
		break;
	default:
		break;
	}

	oplus_disp_trace_c("%d|auto_handled|%d", g_commit_pid, handled);
	oplus_disp_trace_c("%d|oplus_adfr_auto_mode|%d", g_commit_pid, oplus_adfr_auto_mode);
	oplus_disp_trace_c("%d|oplus_adfr_auto_fakeframe|%d", g_commit_pid, oplus_adfr_auto_fakeframe);
	oplus_disp_trace_c("%d|oplus_adfr_auto_min_fps|%d", g_commit_pid, oplus_adfr_auto_min_fps);
	oplus_disp_trace_end("oplus_adfr_handle_auto_mode");

	if (handled == 1) {
		return;
	} else {
		/* latest setting, but if power on/off or timing switch, the mode and min fps are not right */
		ADFR_INFO("auto mode %d[%d], fakeframe %d[%d], min fps %d[%d], handled %d\n",
			oplus_adfr_auto_mode, oplus_adfr_auto_mode_updated,
			oplus_adfr_auto_fakeframe, oplus_adfr_auto_fakeframe_updated,
			oplus_adfr_auto_min_fps, oplus_adfr_auto_min_fps_updated, handled);
	}

	return;
}

int oplus_mtk_dsi_panel_send_auto_mode_dcs(struct drm_crtc *crtc, bool enable)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct cmdq_pkt *cmdq_handle;
	struct mtk_crtc_state *crtc_state;
	struct mtk_ddp_comp *comp = mtk_ddp_comp_request_output(mtk_crtc);
	struct mtk_cmdq_cb_data *cb_data;
	bool is_frame_mode;

	/* SDC's auto, fakeframe and minfps are available only after power on */
	crtc_state = to_mtk_crtc_state(crtc->state);
	if (!mtk_crtc->enabled || crtc_state->prop_val[CRTC_PROP_DOZE_ACTIVE]) {
		ADFR_WARN("kVRR ignore %s when power is off", __func__);
		return -1;
	}
	if (!(comp && comp->funcs && comp->funcs->io_cmd))
		return -1;

	if (enable) {
		ADFR_INFO("kVRR ctrl:%d auto on\n", g_commit_pid);
		oplus_disp_trace_c("%d|oplus_adfr_auto_mode_cmd|%d", g_commit_pid, OPLUS_ADFR_AUTO_ON);
	} else {
		ADFR_INFO("kVRR ctrl:%d auto off\n", g_commit_pid);
		oplus_disp_trace_c("%d|oplus_adfr_auto_mode_cmd|%d", g_commit_pid, OPLUS_ADFR_AUTO_OFF);
	}

	is_frame_mode = mtk_crtc_is_frame_trigger_mode(&mtk_crtc->base);
	if (is_frame_mode) {
		mtk_drm_idlemgr_kick(__func__, crtc, 0);
		mtk_crtc_pkt_create(&cmdq_handle, &mtk_crtc->base,
						mtk_crtc->gce_obj.client[CLIENT_CFG]);

		if (mtk_crtc_with_sub_path(crtc, mtk_crtc->ddp_mode))
			mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle,
				DDP_SECOND_PATH, 0);
		else
			mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle,
				DDP_FIRST_PATH, 0);

		cmdq_pkt_clear_event(cmdq_handle,
				 mtk_crtc->gce_obj.event[EVENT_STREAM_BLOCK]);
		cmdq_pkt_wfe(cmdq_handle,
		 				 mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);

		if (comp && comp->funcs && comp->funcs->io_cmd)
			comp->funcs->io_cmd(comp, cmdq_handle, SET_AUTO_MODE, &enable);

		cmdq_pkt_set_event(cmdq_handle,
					mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);
		cmdq_pkt_set_event(cmdq_handle,
					mtk_crtc->gce_obj.event[EVENT_STREAM_BLOCK]);

		cb_data = kmalloc(sizeof(*cb_data), GFP_KERNEL);
		if (cb_data) {
			cb_data->cmdq_handle = cmdq_handle;
			/* indicate auto on/off cmd */
			cb_data->misc = 4;
			cmdq_pkt_flush_threaded(cmdq_handle, lcm_cmd_cmdq_cb, cb_data);
		} else {
			cmdq_pkt_flush(cmdq_handle);
			cmdq_pkt_destroy(cmdq_handle);
			return -1;
		}
	}

	return 0;
}

static int oplus_adfr_dsi_display_auto_mode_enable(struct drm_crtc *crtc, bool enable)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	int rc = 0;

	if (crtc == NULL)
		return -1;

	if (!mtk_crtc->panel_ext) {
		return -1;
	} else {
		ADFR_INFO("kVRR oplus_autoon_cfg:%d, oplus_autooff_cfg:%d\n",
			mtk_crtc->panel_ext->params->oplus_autoon_cfg, mtk_crtc->panel_ext->params->oplus_autooff_cfg);
		if (oplus_adfr_auto_mode == OPLUS_ADFR_AUTO_OFF) {
			if (!(mtk_crtc->panel_ext->params->oplus_autooff_cfg & 0x00000001))
				return -1;
		} else {
			if (!(mtk_crtc->panel_ext->params->oplus_autoon_cfg & 0x00000001))
				return -1;
		}
	}
	oplus_disp_trace_begin("dsi_display_auto_mode_enable");
	/* send the commands to enable/disable auto mode */
	rc = oplus_mtk_dsi_panel_send_auto_mode_dcs(crtc, enable);
	if (rc) {
		ADFR_ERR("kVRR fail auto ON cmds rc:%d\n", rc);
		goto exit;
	}
	if (!enable) {
		/* after auto off cmd was sent, auto on cmd filter start */
		oplus_adfr_auto_on_cmd_filter_set(true);
	}

exit:
	oplus_disp_trace_end("dsi_display_auto_mode_enable");
	ADFR_INFO("KVRR auto mode=%d, rc=%d\n", enable, rc);
	return rc;
}

int oplus_mtk_dsi_panel_send_minfps_dcs(struct drm_crtc *crtc, int automode, u32 extend_frame)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct cmdq_pkt *cmdq_handle;
	struct mtk_ddp_comp *comp = mtk_ddp_comp_request_output(mtk_crtc);
	struct mtk_cmdq_cb_data *cb_data;
	struct oplus_minfps minfps;
	bool is_frame_mode;

	if (!(comp && comp->funcs && comp->funcs->io_cmd))
		return -1;

	minfps.minfps_flag = automode;
	minfps.extend_frame = extend_frame;

	is_frame_mode = mtk_crtc_is_frame_trigger_mode(&mtk_crtc->base);
	if (is_frame_mode) {
		mtk_drm_idlemgr_kick(__func__, crtc, 0);
		mtk_crtc_pkt_create(&cmdq_handle, &mtk_crtc->base,
						mtk_crtc->gce_obj.client[CLIENT_CFG]);

		if (mtk_crtc_with_sub_path(crtc, mtk_crtc->ddp_mode))
			mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle,
				DDP_SECOND_PATH, 0);
		else
			mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle,
				DDP_FIRST_PATH, 0);

		cmdq_pkt_clear_event(cmdq_handle,
				 	mtk_crtc->gce_obj.event[EVENT_STREAM_BLOCK]);
		cmdq_pkt_wfe(cmdq_handle,
		 			mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);

		if (comp && comp->funcs && comp->funcs->io_cmd)
			comp->funcs->io_cmd(comp, cmdq_handle, SET_MINFPS, &minfps);

		cmdq_pkt_set_event(cmdq_handle,
					mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);
		cmdq_pkt_set_event(cmdq_handle,
			mtk_crtc->gce_obj.event[EVENT_STREAM_BLOCK]);

		cb_data = kmalloc(sizeof(*cb_data), GFP_KERNEL);
		if (cb_data) {
			cb_data->cmdq_handle = cmdq_handle;
			/* indicate minfps cmd */
			cb_data->misc = 5;
			cmdq_pkt_flush_threaded(cmdq_handle, lcm_cmd_cmdq_cb, cb_data);
		} else {
			cmdq_pkt_flush(cmdq_handle);
			cmdq_pkt_destroy(cmdq_handle);
			return -1;
		}

		oplus_adfr_send_min_fps_event(extend_frame);
	}

	return 0;
}

static int dsi_panel_auto_minfps_check(struct drm_crtc *crtc, u32 extend_frame)
{
	int h_skew = crtc->state->mode.hskew;
	int refresh_rate = drm_mode_vrefresh(&crtc->state->mode);

	if (h_skew == SDC_ADFR) {
		if (oplus_adfr_auto_mode == OPLUS_ADFR_AUTO_OFF) {
			if (refresh_rate == 120) {
				if ((extend_frame < OPLUS_ADFR_AUTO_MIN_FPS_MAX) || (extend_frame > OPLUS_ADFR_AUTO_MIN_FPS_1HZ)) {
					/* The highest frame rate is the most stable */
					extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_MAX;
				} else if ((oplus_adfr_idle_mode == OPLUS_ADFR_IDLE_OFF) && (extend_frame > OPLUS_ADFR_AUTO_MIN_FPS_20HZ)
					&& (extend_frame <= OPLUS_ADFR_AUTO_MIN_FPS_1HZ)) {
					if (oplus_adfr_idle_mode_is_enable()) {
						/* force to 20hz if the min fps is less than 20hz when auto mode is off and idle mode is also off */
						extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_20HZ;
					}
				}
			}
		} else {
			if (refresh_rate == 120) {
				if ((extend_frame < OPLUS_ADFR_AUTO_MIN_FPS_MAX) || (extend_frame > OPLUS_ADFR_AUTO_MIN_FPS_1HZ)) {
					extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_MAX;
				}
			}
		}
	} else if (h_skew == SDC_MFR) {
		if ((extend_frame < OPLUS_ADFR_AUTO_MIN_FPS_60HZ) || (extend_frame > OPLUS_ADFR_AUTO_MIN_FPS_1HZ)) {
			extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_60HZ;
		} else if ((oplus_adfr_idle_mode == OPLUS_ADFR_IDLE_OFF) && (extend_frame > OPLUS_ADFR_AUTO_MIN_FPS_20HZ) && (extend_frame <= OPLUS_ADFR_AUTO_MIN_FPS_1HZ)) {
			if (oplus_adfr_idle_mode_is_enable()) {
				extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_20HZ;
			}
		}
	} else if (h_skew == OPLUS_ADFR) {
		if (extend_frame < 1 || extend_frame > 3)
			extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_MAX;
	}

	return extend_frame;
}

static int dsi_panel_send_auto_minfps_dcs(struct drm_crtc *crtc, u32 extend_frame)
{
	int rc = 0;
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct mtk_crtc_state *crtc_state;

	/* SDC's auto, fakeframe and minfps are available only after power on */
	crtc_state = to_mtk_crtc_state(crtc->state);
	if (!mtk_crtc->enabled || crtc_state->prop_val[CRTC_PROP_DOZE_ACTIVE]) {
		ADFR_WARN("kVRR ignore %s:%d %u when power is off", __func__, __LINE__, extend_frame);
		return 0;
	}
	if (!mtk_crtc->panel_ext) {
		return -1;
	} else {
		ADFR_INFO("kVRR oplus_minfps0_cfg:%d, oplus_minfps1_cfg:%d\n",
			mtk_crtc->panel_ext->params->oplus_minfps0_cfg, mtk_crtc->panel_ext->params->oplus_minfps1_cfg);
		if (oplus_adfr_auto_mode == OPLUS_ADFR_AUTO_OFF) {
			if (!(mtk_crtc->panel_ext->params->oplus_minfps0_cfg & 0x00000001)) {
				ADFR_INFO("kVRR ignore oplus_minfps0_cfg %s:%d", __func__, __LINE__);
				return -1;
			}
		} else {
			if (!(mtk_crtc->panel_ext->params->oplus_minfps1_cfg & 0x00000001)) {
				ADFR_INFO("kVRR ignore oplus_minfps1_cfg %s:%d", __func__, __LINE__);
				return -1;
			}
		}
	}
	/*check minfps*/
	extend_frame = dsi_panel_auto_minfps_check(crtc, extend_frame);

	rc = oplus_mtk_dsi_panel_send_minfps_dcs(crtc, oplus_adfr_auto_mode, extend_frame);

	return rc;
}

static int oplus_adfr_dsi_display_auto_mode_min_fps(struct drm_crtc *crtc, u32 extend_frame)
{
	int rc = 0;

	if (crtc == NULL)
			return -1;

	oplus_disp_trace_begin("dsi_display_auto_mode_min_fps");
	/* send the commands to set auto mode min fps */
	rc = dsi_panel_send_auto_minfps_dcs(crtc, extend_frame);
	if (rc) {
		ADFR_ERR("kVRR fail auto Min Fps cmds rc:%d\n", rc);
		goto exit;
	}

exit:
	oplus_disp_trace_end("dsi_display_auto_mode_min_fps");
	ADFR_INFO("extern_frame=%d, rc=%d\n", extend_frame, rc);
	return rc;
}

void oplus_adfr_dsi_display_auto_mode_update(struct drm_device *drm)
{
	struct drm_crtc *crtc;
	struct mtk_drm_crtc *mtk_crtc;
	struct mtk_drm_private *private = NULL;
	int h_skew = SDC_ADFR;
	int rc = 0;
	static int last_osync_mode_recovery, last_enable_multite;

	if (!oplus_adfr_is_support()) {
		return;
	}
	/* this debug cmd only for crtc0 */
	crtc = list_first_entry(&(drm)->mode_config.crtc_list,
				typeof(*crtc), head);
	if (!crtc) {
		printk(KERN_ERR "find crtc fail\n");
		return;
	}
	mtk_crtc = to_mtk_crtc(crtc);

	h_skew = crtc->state->mode.hskew;
	if ((h_skew != SDC_ADFR) && (h_skew != SDC_MFR) && (h_skew != OPLUS_ADFR)) {
		ADFR_ERR("kVRR OPLUS ADFR does not support auto mode setting\n");
		return;
	}

	oplus_disp_trace_begin("dsi_display_auto_mode_update");

	if (h_skew == OPLUS_ADFR) {
		u32 minfps = 0;
		mutex_lock(&multite_mutex);
		private = drm->dev_private;
		if (!drm || !private) {
			ADFR_ERR("Invalid params");
			mutex_unlock(&multite_mutex);
			return;
		}
		if (mtk_crtc->oplus_apollo_br->oplus_backlight_updated == true) {
			/* private->osync_mode_recovery = false; */
			/* oplus_adfr_osync_mode_timer_start(1000); */
			ADFR_WARN("kVRR set osync_mode_recovery=false\n");
		}
		/* disable or enable multi te in OA mode*/
		if ((last_osync_mode_recovery != private->osync_mode_recovery)
				|| (last_enable_multite != enable_multite)) {
			ADFR_INFO("kVRR osync_mode_recovery=%d, multite=%d, minfps=%d\n", private->osync_mode_recovery, enable_multite, oplus_adfr_auto_min_fps);
			last_osync_mode_recovery = private->osync_mode_recovery;
			last_enable_multite = enable_multite;
		}
		if (false == private->osync_mode_recovery) {
			if (true == oplus_adfr_get_multite_state()) {
				rc = oplus_mtk_dsi_panel_send_multite(crtc, false);
				if (0 == rc)
					oplus_adfr_set_multite_state(false);
			}
		} else {
			minfps = dsi_panel_auto_minfps_check(crtc, oplus_adfr_auto_min_fps);
			if (false == oplus_adfr_get_multite_state()) {
				if (minfps > 0) {
					rc = oplus_mtk_dsi_panel_send_multite(crtc, true);
					if (0 == rc)
						oplus_adfr_set_multite_state(true);
				}
			} else {
				if (minfps == 0) {
					rc = oplus_mtk_dsi_panel_send_multite(crtc, false);
					if (0 == rc)
						oplus_adfr_set_multite_state(false);
				}
			}
		}
		mutex_unlock(&multite_mutex);
	}

	if (oplus_adfr_auto_mode_updated) {
		oplus_adfr_dsi_display_auto_mode_enable(crtc, oplus_adfr_auto_mode);
		oplus_adfr_auto_mode_updated = false;
	}

	if (oplus_adfr_auto_min_fps_updated) {
		if (oplus_adfr_skip_min_fps_cmd) {
			ADFR_INFO("filter min fps %u setting\n", oplus_adfr_auto_min_fps);
		} else {
			oplus_adfr_dsi_display_auto_mode_min_fps(crtc, oplus_adfr_auto_min_fps);
		}
		oplus_adfr_auto_min_fps_updated = false;
	}

	if (oplus_adfr_auto_fakeframe_updated) {
		/* fake frame update take effect immediately, so just reset it's updated status here */
		oplus_adfr_auto_fakeframe_updated = false;
	}

	oplus_disp_trace_end("dsi_display_auto_mode_update");
	return;
}

int oplus_adfr_send_min_fps_event(unsigned int min_fps)
{
	ADFR_INFO("start\n");

	lcdinfo_notify(MTK_DISP_EVENT_ADFR_MIN_FPS, &min_fps);
	ADFR_INFO("MTK_DISP_EVENT_ADFR_MIN_FPS:%u\n", min_fps);

	ADFR_INFO("end\n");

	return 0;
}

/* the highest min fps setting is required when the temperature meets certain conditions, otherwise recovery it */
int oplus_adfr_temperature_detection_handle(void *mtk_ddp_comp, void *cmdq_pkt, int ntc_temp, int shell_temp)
{
	static bool last_oplus_adfr_skip_min_fps_cmd = false;
	unsigned int refresh_rate = 120;
	unsigned int h_skew = SDC_ADFR;
	struct mtk_ddp_comp *comp = mtk_ddp_comp;
	struct cmdq_pkt *cmdq_handle = cmdq_pkt;
	struct drm_crtc *crtc = NULL;
	struct drm_display_mode *drm_mode = NULL;
	struct oplus_minfps minfps;

	if (!oplus_adfr_temperature_detection_is_enable()) {
		return 0;
	}

	if (!comp || !cmdq_handle) {
		ADFR_ERR("Invalid intput params\n");
		return -EINVAL;
	}

	crtc = &(comp->mtk_crtc->base);
	if (!crtc) {
		ADFR_ERR("Invalid crtc params\n");
		return -EINVAL;
	}

	drm_mode = &(crtc->state->mode);
	if (!drm_mode) {
		ADFR_ERR("Invalid drm_mode params\n");
		return -EINVAL;
	}

	refresh_rate = drm_mode_vrefresh(drm_mode);
	h_skew = crtc->state->mode.hskew;

	if ((h_skew != OPLUS_ADFR)
			&& ((abs(ntc_temp - shell_temp) >= 5)
				|| (ntc_temp < 0)
				|| (shell_temp < 0)
				|| (((ntc_temp > 45) || (shell_temp > 45)) && (refresh_rate == 120))
				|| (((ntc_temp > 40) || (shell_temp > 40)) && (refresh_rate == 90))
				|| (((ntc_temp > 40) || (shell_temp > 40)) && (refresh_rate == 60)))) {
		oplus_adfr_skip_min_fps_cmd = true;

		if (!last_oplus_adfr_skip_min_fps_cmd && oplus_adfr_skip_min_fps_cmd) {
			if (((oplus_adfr_auto_min_fps == 0) && (refresh_rate == 120))
					|| ((oplus_adfr_auto_min_fps == 0) && (refresh_rate == 90))
					|| ((oplus_adfr_auto_min_fps == 1) && (refresh_rate == 60))) {
				ADFR_INFO("ntc_temp:%d,shell_temp:%d,refresh_rate:%u, already in min fps %u\n", ntc_temp, shell_temp, refresh_rate, oplus_adfr_auto_min_fps);
			} else {
				minfps.minfps_flag = oplus_adfr_auto_mode;
				if (refresh_rate == 60) {
					minfps.extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_60HZ;
				} else {
					minfps.extend_frame = OPLUS_ADFR_AUTO_MIN_FPS_MAX;
				}
				ADFR_INFO("ntc_temp:%d,shell_temp:%d,refresh_rate:%u, need to set min fps to %u\n", ntc_temp, shell_temp, refresh_rate, minfps.extend_frame);
				mtk_ddp_comp_io_cmd(comp, cmdq_handle, SET_MINFPS, &minfps);
			}
		}
	} else {
		oplus_adfr_skip_min_fps_cmd = false;

		if (last_oplus_adfr_skip_min_fps_cmd && !oplus_adfr_skip_min_fps_cmd) {
			if (((oplus_adfr_auto_min_fps == 0) && (refresh_rate == 120))
					|| ((oplus_adfr_auto_min_fps == 0) && (refresh_rate == 90))
					|| ((oplus_adfr_auto_min_fps == 1) && (refresh_rate == 60))) {
				oplus_adfr_auto_min_fps_updated = false;
				ADFR_INFO("ntc_temp:%d,shell_temp:%d,refresh_rate:%u, no need to update min fps %u\n", ntc_temp, shell_temp, refresh_rate, oplus_adfr_auto_min_fps);
			} else {
				oplus_adfr_auto_min_fps_updated = true;
				ADFR_INFO("ntc_temp:%d,shell_temp:%d,refresh_rate:%u, need to recovery min fps to %u\n", ntc_temp, shell_temp, refresh_rate, oplus_adfr_auto_min_fps);
			}
		}
	}

	last_oplus_adfr_skip_min_fps_cmd = oplus_adfr_skip_min_fps_cmd;

	return 0;
}

/* Add for adfr status reset */
/* reset auto mode status as panel power on and timing switch to SM */
void oplus_adfr_status_reset(struct drm_display_mode *src_m, struct drm_display_mode *dst_m)
{
	u32 refresh_rate = 120;
	u32 h_skew = SDC_ADFR;
	u32 oplus_adfr_auto_min_fps_cmd = OPLUS_ADFR_AUTO_MIN_FPS_MAX;
	struct mtk_drm_private *private = NULL;
	struct drm_device *ddev = get_drm_device();

	if (dst_m == NULL) {
		ADFR_ERR("kVRR Invalid params");
		return;
	}

	h_skew = dst_m->hskew;
	refresh_rate = drm_mode_vrefresh(dst_m);

	if ((h_skew == SDC_ADFR) || (h_skew == SDC_MFR)) {
		/* after auto off cmd was sent, auto on cmd filter start */
		oplus_adfr_auto_on_cmd_filter_set(true);

		oplus_adfr_auto_mode = OPLUS_ADFR_AUTO_OFF;
		if (refresh_rate == 60) {
			oplus_adfr_auto_min_fps = OPLUS_ADFR_AUTO_MIN_FPS_60HZ;
		} else {
			/* 90hz min fps in auto mode off should be 0x08 which will be corrected before cmd sent */
			oplus_adfr_auto_min_fps = OPLUS_ADFR_AUTO_MIN_FPS_MAX;

			if (src_m == NULL) {
				/* 120/90hz should open fakeframe when power on */
				oplus_adfr_auto_fakeframe = OPLUS_ADFR_FAKEFRAME_ON;
			} else if (dst_m->hdisplay == src_m->hdisplay) {
				/* if fakeframe is sent after resolution switch, local garbage issue will happen in low probability */
				/* 120/90hz should open fakeframe when timing switch */
				oplus_adfr_auto_fakeframe = OPLUS_ADFR_FAKEFRAME_ON;
			}
		}

		if (refresh_rate == 90) {
			/* should +8 in auto off mode */
			oplus_adfr_auto_min_fps_cmd = oplus_adfr_auto_min_fps + 8;
		} else {
			oplus_adfr_auto_min_fps_cmd = oplus_adfr_auto_min_fps;
		}

		/* update auto mode and qsync para when timing switch or panel enable for debug */
		oplus_disp_trace_c("%d|oplus_adfr_auto_mode|%d", g_commit_pid, oplus_adfr_auto_mode);
		oplus_disp_trace_c("%d|oplus_adfr_auto_fakeframe|%d", g_commit_pid, oplus_adfr_auto_fakeframe);
		oplus_disp_trace_c("%d|oplus_adfr_auto_min_fps|%d", g_commit_pid, oplus_adfr_auto_min_fps);
		oplus_disp_trace_c("%d|oplus_adfr_auto_mode_cmd|%d", g_commit_pid, oplus_adfr_auto_mode);
		oplus_disp_trace_c("%d|oplus_adfr_auto_min_fps_cmd|%d", g_commit_pid, oplus_adfr_auto_min_fps_cmd);
		ADFR_WARN("auto mode reset: auto mode %d, fakeframe %d, min fps %d\n", oplus_adfr_auto_mode,
			oplus_adfr_auto_fakeframe, oplus_adfr_auto_min_fps);
	} else {
		private = ddev->dev_private;
		if (private != NULL)
			private->osync_mode_recovery = true;
		ADFR_WARN("private->osync_mode_recovery=%d\n", private->osync_mode_recovery);
		oplus_adfr_auto_min_fps = OPLUS_ADFR_AUTO_MIN_FPS_MAX;
		oplus_disp_trace_c("%d|oplus_adfr_auto_mode_cmd|%d", g_commit_pid, 0);
		oplus_disp_trace_c("%d|oplus_adfr_auto_min_fps_cmd|%d", g_commit_pid, 0);
	}
	oplus_disp_trace_c("%d|h_skew|%d", g_commit_pid, h_skew);

	oplus_adfr_send_min_fps_event(oplus_adfr_auto_min_fps);

	return;
}
EXPORT_SYMBOL(oplus_adfr_status_reset);

int oplus_mtk_dsi_panel_send_multite(struct drm_crtc *crtc, bool enable)
{
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	struct cmdq_pkt *cmdq_handle;
	struct mtk_crtc_state *crtc_state;
	struct mtk_ddp_comp *comp = mtk_ddp_comp_request_output(mtk_crtc);
	struct mtk_cmdq_cb_data *cb_data;
	bool is_frame_mode;

	/* SDC's auto, fakeframe and minfps are available only after power on */
	crtc_state = to_mtk_crtc_state(crtc->state);
	if (!mtk_crtc->enabled || crtc_state->prop_val[CRTC_PROP_DOZE_ACTIVE]) {
		ADFR_WARN("kVRR ignore %s when power is off", __func__);
		return -1;
	}
	if (!(comp && comp->funcs && comp->funcs->io_cmd))
		return -1;

	if (enable) {
		ADFR_WARN("kVRR ctrl:%d multite on\n", g_commit_pid);
		oplus_disp_trace_c("%d|oplus_adfr_multite_cmd|%d", g_commit_pid, 1);
	} else {
		ADFR_WARN("kVRR ctrl:%d multite off\n", g_commit_pid);
		oplus_disp_trace_c("%d|oplus_adfr_multite_cmd|%d", g_commit_pid, 0);
	}

	is_frame_mode = mtk_crtc_is_frame_trigger_mode(&mtk_crtc->base);
	if (is_frame_mode) {
		mtk_drm_idlemgr_kick(__func__, crtc, 0);
		mtk_crtc_pkt_create(&cmdq_handle, &mtk_crtc->base,
						mtk_crtc->gce_obj.client[CLIENT_CFG]);

		if (mtk_crtc_with_sub_path(crtc, mtk_crtc->ddp_mode))
			mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle,
				DDP_SECOND_PATH, 0);
		else
			mtk_crtc_wait_frame_done(mtk_crtc, cmdq_handle,
				DDP_FIRST_PATH, 0);

		cmdq_pkt_clear_event(cmdq_handle,
				 mtk_crtc->gce_obj.event[EVENT_STREAM_BLOCK]);
		cmdq_pkt_wfe(cmdq_handle,
		 				 mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);

		if (comp && comp->funcs && comp->funcs->io_cmd)
			comp->funcs->io_cmd(comp, cmdq_handle, SET_MULTITE, &enable);

		cmdq_pkt_set_event(cmdq_handle,
					mtk_crtc->gce_obj.event[EVENT_CABC_EOF]);
		cmdq_pkt_set_event(cmdq_handle,
					mtk_crtc->gce_obj.event[EVENT_STREAM_BLOCK]);

		cb_data = kmalloc(sizeof(*cb_data), GFP_KERNEL);
		if (cb_data) {
			cb_data->cmdq_handle = cmdq_handle;
			/* indicate multite enable/disable cmd */
			cb_data->misc = 6;
			cmdq_pkt_flush_threaded(cmdq_handle, lcm_cmd_cmdq_cb, cb_data);
		} else {
			cmdq_pkt_flush(cmdq_handle);
			cmdq_pkt_destroy(cmdq_handle);
		}
	}

	return 0;
}

bool oplus_adfr_get_multite_state(void)
{
	return enable_multite;
}

void oplus_adfr_set_multite_state(bool state)
{
	enable_multite = state;
}
EXPORT_SYMBOL(oplus_adfr_set_multite_state);

/* --------------- idle mode ---------------*/
/* if idle mode is on, the min fps will be reduced when entering MIPI idle and increased when leaving MIPI idle, thus saving power more accurately */
void oplus_adfr_handle_idle_mode(void *drm_crtc, int enter_idle)
{
	struct drm_crtc *crtc = drm_crtc;
	struct cmdq_pkt *handle;
	struct mtk_drm_crtc *mtk_crtc = to_mtk_crtc(crtc);
	u32 h_skew = SDC_ADFR;
	u32 refresh_rate = 120;

	if (!oplus_adfr_idle_mode_is_enable()) {
		return;
	}

	if (!crtc || !mtk_crtc) {
		ADFR_ERR("Invalid params\n");
		return;
	}

	oplus_disp_trace_begin("oplus_adfr_handle_idle_mode");

	h_skew = crtc->state->mode.hskew;
	refresh_rate = drm_mode_vrefresh(&crtc->state->mode);

	if (enter_idle) {
		if (h_skew == SDC_ADFR || h_skew == SDC_MFR) {
			if (refresh_rate == 120 || refresh_rate == 60) {
				/* enter idle mode if auto mode is off and min fps is less than 20hz */
				if ((oplus_adfr_auto_mode == OPLUS_ADFR_AUTO_OFF) && (oplus_adfr_auto_min_fps > OPLUS_ADFR_AUTO_MIN_FPS_20HZ)
					&& (oplus_adfr_auto_min_fps <= OPLUS_ADFR_AUTO_MIN_FPS_1HZ)) {
					oplus_adfr_idle_mode = OPLUS_ADFR_IDLE_ON;
					ADFR_INFO("idle mode on");

					/* send min fps before enter idle */
					ADFR_INFO("enter idle, min fps %d", oplus_adfr_auto_min_fps);
					oplus_adfr_dsi_display_auto_mode_min_fps(crtc, oplus_adfr_auto_min_fps);

					/* wait for the min fps cmds transmission to complete */
					mtk_crtc_pkt_create(&handle, &mtk_crtc->base, mtk_crtc->gce_obj.client[CLIENT_CFG]);
					cmdq_pkt_flush(handle);
					cmdq_pkt_destroy(handle);
				}
			}
		}
	} else {
		/* exit idle mode */
		if (oplus_adfr_idle_mode == OPLUS_ADFR_IDLE_ON) {
			/* send min fps after exit idle */
			ADFR_INFO("exit idle, min fps %d", OPLUS_ADFR_AUTO_MIN_FPS_20HZ);
			oplus_adfr_dsi_display_auto_mode_min_fps(crtc, OPLUS_ADFR_AUTO_MIN_FPS_20HZ);

			oplus_adfr_idle_mode = OPLUS_ADFR_IDLE_OFF;
			ADFR_INFO("idle mode off");
		}
	}

	oplus_disp_trace_c("%d|oplus_adfr_idle_mode|%d", g_commit_pid, oplus_adfr_idle_mode);
	oplus_disp_trace_end();

	return;
}

/* --------------- test te ---------------*/
void oplus_adfr_get_test_te_gpio(void *dsi_dev) {
	struct device *dev = dsi_dev;

	if (!dev) {
		ADFR_ERR("dsi_dev is NULL");
		return;
	}

	/* oplus,adfr-test-te-gpio */
	test_te.gpio = devm_gpiod_get(dev, "oplus-adfr-test-te", GPIOD_IN);
	if (IS_ERR(test_te.gpio)) {
		ADFR_INFO("oplus,adfr-test-te-gpio is not set\n");
	} else {
		ADFR_INFO("oplus,adfr-test-te-gpio is already set\n");

		/* test te timer init */
		hrtimer_init(&test_te.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		test_te.timer.function = oplus_adfr_test_te_timer_handler;
		test_te.config = OPLUS_ADFR_TEST_TE_DISABLE;
	}

	return;
}
EXPORT_SYMBOL(oplus_adfr_get_test_te_gpio);

/* test te timer */
enum hrtimer_restart oplus_adfr_test_te_timer_handler(struct hrtimer *timer)
{
	struct oplus_adfr_test_te_params *p_oplus_adfr_test_te_params = from_timer(p_oplus_adfr_test_te_params, timer, timer);

	if (!p_oplus_adfr_test_te_params) {
		ADFR_ERR("Invalid p_oplus_adfr_test_te_params param\n");
		goto end;
	}

	if (IS_ERR(test_te.gpio)) {
		ADFR_ERR("test te gpio is Invalid, no need to handle test te irq\n");
		goto end;
	}

	ADFR_DEBUG("start\n");

	/* speed up refrsh rate updates if enter idle mode */
	test_te.refresh_rate = oplus_adfr_auto_min_fps;
	if (test_te.config == OPLUS_ADFR_TEST_TE_ENABLE_WITCH_LOG) {
		ADFR_INFO("enter idle mode, update refresh_rate to %u\n", test_te.refresh_rate);
	}

end:
	ADFR_DEBUG("end\n");

	return HRTIMER_NORESTART;
}

int oplus_adfr_test_te_high_gear(void *drm_crtc) {
	struct drm_crtc *crtc = drm_crtc;
	int refresh_rate = 120;
	int high_gear = 55;

	if (!crtc) {
		ADFR_ERR("Invalid crtc params\n");
		return high_gear;
	}

	refresh_rate = drm_mode_vrefresh(&crtc->state->mode);

	switch (refresh_rate) {
	case 144:
		high_gear = 90;
		break;
	case 120:
	case 60:
	case 90:
		if (!oplus_adfr_decreasing_step_is_enabled()) {
			high_gear = 55;
		} else {
			high_gear = 52;
		}
		break;
	default:
		high_gear = 55;
		break;
	}

	return high_gear;
}

int oplus_adfr_test_te_low_gear(void *drm_crtc) {
	struct drm_crtc *crtc = drm_crtc;
	int refresh_rate = 120;
	int low_gear = 16;

	if (!crtc) {
		ADFR_ERR("Invalid crtc params\n");
		return low_gear;
	}

	refresh_rate = drm_mode_vrefresh(&crtc->state->mode);

	switch (refresh_rate) {
	case 144:
		if (!oplus_adfr_decreasing_step_is_enabled()) {
			low_gear = 0;
		} else {
			low_gear = 12;
		}
		break;
	case 120:
	case 60:
	case 90:
		if (!oplus_adfr_decreasing_step_is_enabled()) {
			low_gear = 16;
		} else {
			low_gear = 10;
		}
		break;
	default:
		low_gear = 16;
		break;
	}

	return low_gear;
}

int oplus_adfr_test_te_middle_value(void *drm_crtc) {
	struct drm_crtc *crtc = drm_crtc;
	int refresh_rate = 120;
	int middle_value = 30;

	if (!crtc) {
		ADFR_ERR("Invalid crtc params\n");
		return middle_value;
	}

	refresh_rate = drm_mode_vrefresh(&crtc->state->mode);

	switch (refresh_rate) {
	case 144:
		if (!oplus_adfr_decreasing_step_is_enabled()) {
			middle_value = 72;
		} else {
			middle_value = 36;
		}
		break;
	case 120:
	case 60:
	case 90:
		middle_value = 30;
		break;
	default:
		middle_value = 30;
		break;
	}

	return middle_value;
}

/* test te detectiton */
static irqreturn_t oplus_adfr_test_te_irq_handler(int irq, void *data)
{
	unsigned int temp_refresh_rate = 0;
	int h_skew = SDC_ADFR;
	int refresh_rate = 120;
	u64 current_timestamp = 0;
	struct mtk_ddp_comp *comp = (struct mtk_ddp_comp *)data;
	struct mtk_drm_crtc *mtk_crtc = NULL;
	struct drm_crtc *crtc = NULL;

	if (!comp) {
		ADFR_ERR("Invalid mtk_ddp_comp param\n");
		return IRQ_HANDLED;
	}

	mtk_crtc = comp->mtk_crtc;
	if (!mtk_crtc) {
		ADFR_ERR("Invalid mtk_drm_crtc param\n");
		return IRQ_HANDLED;
	}

	crtc = &(mtk_crtc->base);
	if (!crtc) {
		ADFR_ERR("Invalid crtc params\n");
		return IRQ_HANDLED;
	}

	if (!crtc->state || !&(crtc->state->mode)) {
		ADFR_ERR("Invalid crtc params\n");
		return IRQ_HANDLED;
	}

	h_skew = crtc->state->mode.hskew;
	refresh_rate = drm_mode_vrefresh(&crtc->state->mode);

	if (IS_ERR(test_te.gpio)) {
		ADFR_ERR("test te gpio is Invalid, no need to handle test te irq\n");
		return IRQ_HANDLED;
	}

	ADFR_DEBUG("start\n");

	if (test_te.config != OPLUS_ADFR_TEST_TE_DISABLE) {
		/* check the test te interval to calculate refresh rate of ddic */
		current_timestamp = (u64)ktime_to_ms(ktime_get());
		temp_refresh_rate = 1000 / (current_timestamp - test_te.last_timestamp);

		/* filtering algorithm */
		if ((h_skew == SDC_ADFR) || (h_skew == SDC_MFR)) {
			if (!oplus_adfr_decreasing_step_is_enabled()) {
				/* range:
				 * 144hz: >90 is 144
				 * 120hz: >55 is 120, sw_fps==60 is 60
				 * 90hz: >55 is 90
				 * 60hz: >55 is 60 */
				if (temp_refresh_rate > oplus_adfr_test_te_high_gear(crtc)) {
					test_te.high_refresh_rate_count++;
					test_te.middle_refresh_rate_count = 0;
					/* update refresh rate if four continous temp_refresh_rate are greater than high gear */
					if (test_te.high_refresh_rate_count == 4) {
						test_te.refresh_rate = refresh_rate;
						test_te.high_refresh_rate_count--;
					}
				/* range:
				 * 144hz: 0~90 is 72
				 * 120hz: 17~55 is 30
				 * 90hz: 17~55 is 30
				 * 60hz: 17~55 is 30 */
				} else if (temp_refresh_rate > oplus_adfr_test_te_low_gear(crtc) && temp_refresh_rate <= oplus_adfr_test_te_high_gear(crtc)) {
					test_te.high_refresh_rate_count = 0;
					/* update refresh rate if one continous temp_refresh_rate are greater than low gear and less than or equal to high gear */
					test_te.refresh_rate = oplus_adfr_test_te_middle_value(crtc);
				/* range:
				 * 144hz: X
				 * 120hz: <=16 is temp_refresh_rate
				 * 90hz: <=16 is temp_refresh_rate
				 * 60hz: <=16 is temp_refresh_rate */
				} else {
						test_te.high_refresh_rate_count = 0;
						/* if current refresh rate of ddic is less than or equal to low gear, use it directly */
						test_te.refresh_rate = temp_refresh_rate;
				}
			} else {
				/* range:
				 * 144hz: >90 is 144
				 * 120hz: >52 is 120, sw_fps==60 is 60
				 * 90hz: >52 is 90
				 * 60hz: >52 is 60 */
				if (temp_refresh_rate > oplus_adfr_test_te_high_gear(crtc)) {
					test_te.high_refresh_rate_count++;
					test_te.middle_refresh_rate_count = 0;
					/* update refresh rate if four continous temp_refresh_rate are greater than high gear */
					if (test_te.high_refresh_rate_count == 4) {
						test_te.refresh_rate = refresh_rate;
						test_te.high_refresh_rate_count--;
					}
				/* range:
				 * 144hz: 13~90 is 36
				 * 120hz: 11~52 is 30
				 * 90hz: 11~52 is 30
				 * 60hz: 11~52 is 30 */
				} else if (temp_refresh_rate > oplus_adfr_test_te_low_gear(crtc) && temp_refresh_rate <= oplus_adfr_test_te_high_gear(crtc)) {
					test_te.middle_refresh_rate_count++;
					if (test_te.high_refresh_rate_count > 0) {
						test_te.high_refresh_rate_count = 0;
						/* update refresh rate if one continous temp_refresh_rate are greater than low gear and less than or equal to high gear
						 * and old temp_refresh_rate are greater than high gear*/
						test_te.refresh_rate = oplus_adfr_test_te_middle_value(crtc);
					} else if (test_te.middle_refresh_rate_count == 2) {
						/* update refresh rate if two continous temp_refresh_rate are greater than low gear and less than or equal to high gear
						 * and old temp_refresh_rate are less than or equal to high gear*/
						test_te.refresh_rate = oplus_adfr_test_te_middle_value(crtc);
						test_te.middle_refresh_rate_count--;
					}
				/* range:
				 * 144hz: <=12 is min fps
				 * 120hz: <=10 is min fps
				 * 90hz: <=10 is min fps
				 * 60hz: <=10 is min fps */
				} else {
					test_te.high_refresh_rate_count = 0;
					test_te.middle_refresh_rate_count = 0;
					/* if current refresh rate of ddic is less than or equal to low gear, set as min fps */
					test_te.refresh_rate = 120/(1+oplus_adfr_auto_min_fps);
				}
			}
			if (oplus_adfr_idle_mode == OPLUS_ADFR_IDLE_ON) {
				/* use sa min fps directly when enter idle mode */
				test_te.refresh_rate = 120/(1+oplus_adfr_auto_min_fps);
			}
		} else {
			test_te.high_refresh_rate_count = 0;
			/* fix refresh rate */
			test_te.refresh_rate = refresh_rate;
		}

		if (test_te.refresh_rate > refresh_rate) {
			test_te.refresh_rate = refresh_rate;
		}

		ADFR_DEBUG("oplus_adfr_test_te_high_refresh_rate_count:%u\n", test_te.high_refresh_rate_count);

		if (test_te.config == OPLUS_ADFR_TEST_TE_ENABLE_WITCH_LOG) {
			/* print key information on every test te irq handler */
			ADFR_INFO("last_timestamp:%llu,current_timestamp:%llu,temp_refresh_rate:%u,refresh_rate:%u\n",
						test_te.last_timestamp, current_timestamp,
						temp_refresh_rate, test_te.refresh_rate);

			if ((h_skew == SDC_ADFR) || (h_skew == SDC_MFR)) {
				ADFR_INFO("fps:%u,h_skew:%u,sa_min_fps:%u,idle_mode:%u\n",
							refresh_rate,
							h_skew,
							oplus_adfr_auto_min_fps,
							oplus_adfr_idle_mode);
			} else {
				ADFR_INFO("fps:%u,h_skew:%u,osync_min_fps:%u\n",
							refresh_rate,
							h_skew,
							oplus_adfr_auto_min_fps);
			}
		}

		test_te.last_timestamp = current_timestamp;
	}

	ADFR_DEBUG("end\n");

	return IRQ_HANDLED;
}

int oplus_adfr_register_test_te_irq(void *mtk_ddp_comp, void *platform_device)
{
	int rc = 0;
	unsigned int test_te_irq = 0;
	struct mtk_ddp_comp *comp = mtk_ddp_comp;
	struct platform_device *pdev = platform_device;
	struct device *dev = NULL;

	if (!comp) {
		ADFR_ERR("Invalid mtk_ddp_comp param\n");
		return -EINVAL;
	}

	if (!pdev) {
		ADFR_ERR("Invalid pdev param\n");
		return -EINVAL;
	}

	dev = &pdev->dev;
	if (!dev) {
		ADFR_ERR("Invalid dev param\n");
		return -EINVAL;
	}

	if (IS_ERR(test_te.gpio)) {
		ADFR_ERR("test te gpio is Invalid, no need to handle test te irq\n");
		return -EINVAL;
	}

	test_te_irq = gpiod_to_irq(test_te.gpio);
	if (test_te_irq < 0) {
		ADFR_ERR("failed to get test_te gpio irq\n");
		return -EINVAL;
	}

	ADFR_DEBUG("start\n");

	/* avoid deferred spurious irqs with disable_irq() */
	/* irq_set_status_flags(test_te_irq, IRQ_DISABLE_UNLAZY); */

	/* detect test te rising edge */
	if (comp->id == DDP_COMPONENT_DSI0) {
		rc = devm_request_irq(dev, test_te_irq, oplus_adfr_test_te_irq_handler,
								IRQF_TRIGGER_RISING | IRQF_ONESHOT, "TEST_TE_GPIO_0", comp);
	} else {
		rc = devm_request_irq(dev, test_te_irq, oplus_adfr_test_te_irq_handler,
								IRQF_TRIGGER_RISING | IRQF_ONESHOT, "TEST_TE_GPIO_1", comp);
	}

	if (rc) {
		ADFR_ERR("test te request_irq failed rc:%d\n", rc);
		/* irq_clear_status_flags(test_te_irq, IRQ_DISABLE_UNLAZY); */
	} else {
		ADFR_INFO("register test te irq successfully\n");
	}

	ADFR_DEBUG("end\n");

	return rc;
}

struct drm_crtc *current_crtc = NULL;

void oplus_adfr_set_current_crtc(void *drm_crtc)
{
	current_crtc = (struct drm_crtc *)drm_crtc;
}

struct drm_crtc *oplus_adfr_get_current_crtc(void)
{
	return current_crtc;
}

int oplus_adfr_set_test_te(void *buf)
{
	unsigned int *test_te_config = buf;
	unsigned int test_te_irq = 0;
	struct drm_crtc *crtc = oplus_adfr_get_current_crtc();

	ADFR_DEBUG("start\n");

	if (!buf || !crtc) {
		ADFR_ERR("invalid buf or crtc params\n");
		return -EINVAL;
	}

	if (IS_ERR(test_te.gpio)) {
		ADFR_ERR("test te gpio is Invalid, no need to handle test te irq\n");
		return -EINVAL;
	}

	test_te.config = *test_te_config;
	ADFR_INFO("oplus_adfr_test_te_config:%u\n", test_te.config);

	test_te_irq = gpiod_to_irq(test_te.gpio);
	if (test_te.config != OPLUS_ADFR_TEST_TE_DISABLE) {
		enable_irq(test_te_irq);
		ADFR_INFO("enable test te irq\n");
	} else {
		disable_irq(test_te_irq);
		ADFR_INFO("disable test te irq\n");
	}

	ADFR_DEBUG("end\n");

	return 0;
}
EXPORT_SYMBOL(oplus_adfr_set_test_te);

int oplus_adfr_get_test_te(void *buf)
{
	unsigned int *refresh_rate = buf;
	struct drm_crtc *crtc = oplus_adfr_get_current_crtc();

	ADFR_DEBUG("start\n");

	if (!buf || !crtc) {
		ADFR_ERR("invalid buf or crtc params\n");
		return -EINVAL;
	}

	if (!crtc->state) {
		ADFR_ERR("invalid crtc state param\n");
		return -EINVAL;
	}

	if (IS_ERR(test_te.gpio)) {
		*refresh_rate = drm_mode_vrefresh(&crtc->state->mode);
		ADFR_INFO("test te gpio is invalid, use current timing refresh rate\n");
	} else {
		*refresh_rate = test_te.refresh_rate;
	}

	ADFR_DEBUG("oplus_adfr_test_te_refresh_rate:%u\n", *refresh_rate);

	ADFR_DEBUG("end\n");

	return 0;
}
EXPORT_SYMBOL(oplus_adfr_get_test_te);

ssize_t oplus_adfr_set_test_te_attr(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int test_te_config = OPLUS_ADFR_TEST_TE_DISABLE;
	unsigned int test_te_irq = 0;
	struct drm_crtc *crtc = oplus_adfr_get_current_crtc();

	ADFR_DEBUG("start\n");

	if (!buf || !crtc) {
		ADFR_ERR("invalid buf or crtc params\n");
		return -EINVAL;
	}

	if (IS_ERR(test_te.gpio)) {
		ADFR_ERR("test te gpio is invalid, should not set test te\n");
		return count;
	}

	sscanf(buf, "%u", &test_te_config);

	test_te.config = test_te_config;
	ADFR_INFO("oplus_adfr_test_te_config:%u\n", test_te.config);

	test_te_irq = gpiod_to_irq(test_te.gpio);
	if (test_te.config != OPLUS_ADFR_TEST_TE_DISABLE) {
		enable_irq(test_te_irq);
		ADFR_INFO("enable test te irq\n");
	} else {
		disable_irq(test_te_irq);
		ADFR_INFO("disable test te irq\n");
	}

	ADFR_DEBUG("end\n");

	return count;
}

ssize_t oplus_adfr_get_test_te_attr(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	unsigned int refresh_rate = 0;
	struct drm_crtc *crtc = oplus_adfr_get_current_crtc();

	ADFR_DEBUG("start\n");

	if (!buf || !crtc) {
		ADFR_ERR("invalid buf or crtc params\n");
		return -EINVAL;
	}

	if (!crtc->state) {
		ADFR_ERR("invalid crtc state param\n");
		return -EINVAL;
	}


	if (IS_ERR(test_te.gpio)) {
		refresh_rate = drm_mode_vrefresh(&crtc->state->mode);
		ADFR_INFO("test te gpio is invalid, use current timing refresh rate\n");
	} else {
		refresh_rate = test_te.refresh_rate;
	}

	ADFR_INFO("oplus_adfr_test_te_refresh_rate:%u\n", refresh_rate);

	ADFR_DEBUG("end\n");

	return sprintf(buf, "%u\n", refresh_rate);
}

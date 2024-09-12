// SPDX-License-Identifier: GPL-2.0
/*
 * CPUFreq governor based on scheduler-provided CPU utilization data.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */
/*
 *
 * Copyright (c) 2019 MediaTek Inc.
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched/cputime.h>
#include <sched/sched.h>
#include <linux/kprobes.h>
#include "cpufreq.h"
#if IS_ENABLED(CONFIG_MTK_GEARLESS_SUPPORT)
#include "mtk_energy_model/v2/energy_model.h"
#else
#include "mtk_energy_model/v1/energy_model.h"
#endif
#include "common.h"
#include <linux/tick.h>
#include <linux/sched/cpufreq.h>
#include <linux/sched/clock.h>
#include <trace/events/power.h>
#include <trace/hooks/sched.h>
#include <linux/sched/topology.h>
#include <trace/hooks/topology.h>
#include <trace/hooks/cpufreq.h>
#include <linux/sched/cpufreq.h>
#include <linux/kthread.h>
#include <uapi/linux/sched/types.h>
#include <thermal_interface.h>
#include <mt-plat/mtk_irq_mon.h>
#include "sched_version_ctrl.h"
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
#include <../kernel/oplus_cpu/sched/sched_assist/sa_common.h>
#endif
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
#include <../kernel/oplus_cpu/sched/frame_boost/frame_group.h>
#endif

#if IS_ENABLED(CONFIG_OPLUS_SCHED_TUNE)
#include <../kernel/oplus_cpu/sched/sched_tune/tune.h>
#endif
#if IS_ENABLED(CONFIG_OPLUS_CPUFREQ_IOWAIT_PROTECT)
#include <../kernel/oplus_cpu/sched/eas_opt/oplus_iowait.h>
#endif
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_VT_CAP)
#include <../kernel/oplus_cpu/sched/eas_opt/oplus_cap.h>
#endif

#define CREATE_TRACE_POINTS
#include "sugov_trace.h"

#define IOWAIT_BOOST_MIN	(SCHED_CAPACITY_SCALE / 8)

#ifdef CONFIG_OPLUS_SUGOV_USE_TL
/* Target load.  Lower values result in higher CPU speeds. */
#define DEFAULT_TARGET_LOAD 80
static unsigned int default_target_loads[] = {DEFAULT_TARGET_LOAD};
#endif

struct sugov_cpu {
	struct update_util_data	update_util;
	struct sugov_policy	*sg_policy;
	unsigned int		cpu;

	bool			iowait_boost_pending;
	unsigned int		iowait_boost;
	u64			last_update;
	unsigned long		util;
	unsigned long		bw_dl;
	unsigned long		max;

	/* The field below is for single-CPU policies only: */
#if IS_ENABLED(CONFIG_NO_HZ_COMMON)
	unsigned long		saved_idle_calls;
#endif
};

static DEFINE_PER_CPU(struct sugov_cpu, sugov_cpu);

/*
 * dynamic control util_est
 * 0:disable 1:enable
 */
#if IS_ENABLED(CONFIG_MTK_SCHEDULER)
bool sysctl_util_est = true;
EXPORT_SYMBOL(sysctl_util_est);
#endif

#if IS_ENABLED(CONFIG_OPLUS_SCHED_TUNE)
typedef unsigned long (*stune_util_t)(int cpu, unsigned long other_util,
		unsigned long util);
stune_util_t _stune_util;
#endif

void (*fpsgo_notify_fbt_is_boost_fp)(int fpsgo_is_boost);
EXPORT_SYMBOL(fpsgo_notify_fbt_is_boost_fp);

/************************ Governor internals ***********************/

static bool sugov_should_update_freq(struct sugov_policy *sg_policy, u64 time)
{
	s64 delta_ns;

	/*
	 * Since cpufreq_update_util() is called with rq->lock held for
	 * the @target_cpu, our per-CPU data is fully serialized.
	 *
	 * However, drivers cannot in general deal with cross-CPU
	 * requests, so while get_next_freq() will work, our
	 * sugov_update_commit() call may not for the fast switching platforms.
	 *
	 * Hence stop here for remote requests if they aren't supported
	 * by the hardware, as calculating the frequency is pointless if
	 * we cannot in fact act on it.
	 *
	 * This is needed on the slow switching platforms too to prevent CPUs
	 * going offline from leaving stale IRQ work items behind.
	 */
	if (!cpufreq_this_cpu_can_update(sg_policy->policy))
		return false;

	if (unlikely(sg_policy->limits_changed)) {
		sg_policy->limits_changed = false;
		sg_policy->need_freq_update = true;
		return true;
	}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
	if (sg_policy->flags & SCHED_CPUFREQ_DEF_FRAMEBOOST)
		return true;
#endif
	/* No need to recalculate next freq for min_rate_limit_us
	 * at least. However we might still decide to further rate
	 * limit once frequency change direction is decided, according
	 * to the separate rate limits.
	 */

	delta_ns = time - sg_policy->last_freq_update_time;
	return delta_ns >= READ_ONCE(sg_policy->min_rate_limit_ns);
}

static bool sugov_up_down_rate_limit(struct sugov_policy *sg_policy, u64 time,
				     unsigned int next_freq)
{
	s64 delta_ns;

	delta_ns = time - sg_policy->last_freq_update_time;

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
	if (sg_policy->flags & SCHED_CPUFREQ_DEF_FRAMEBOOST)
		return false;
#endif

	if (next_freq > sg_policy->next_freq &&
	    delta_ns < sg_policy->up_rate_delay_ns)
		return true;

	if (next_freq < sg_policy->next_freq &&
	    delta_ns < sg_policy->down_rate_delay_ns)
		return true;

	return false;
}

static int wl_cnt_cached;
static bool sugov_update_next_freq(struct sugov_policy *sg_policy, u64 time,
				   unsigned int next_freq)
{
	if (sugov_up_down_rate_limit(sg_policy, time, next_freq))
		return false;

	if (sg_policy->need_freq_update || wl_cnt_cached != wl_type_delay_ch_cnt
			|| enq_force_update_freq(sg_policy)) {
		sg_policy->need_freq_update = false;
	} else if (sg_policy->next_freq == next_freq)
		return false;

	wl_cnt_cached = wl_type_delay_ch_cnt;
	sg_policy->next_freq = next_freq;
	sg_policy->last_freq_update_time = time;

	return true;
}

static void sugov_deferred_update(struct sugov_policy *sg_policy)
{
	if (!sg_policy->work_in_progress) {
		sg_policy->work_in_progress = true;
		irq_work_queue(&sg_policy->irq_work);
	}
}

#ifdef CONFIG_OPLUS_SUGOV_USE_TL
#ifdef CONFIG_NONLINEAR_FREQ_CTL
static inline unsigned int get_opp_capacity(struct cpufreq_policy *policy,
						int row)
{
	return (int)pd_get_opp_capacity(policy->cpu, row);
}
#else
static inline unsigned int get_opp_capacity(struct cpufreq_policy *policy,
						int row)
{
	unsigned int cap, orig_cap;
	unsigned long freq, max_freq;

	max_freq = policy->cpuinfo.max_freq;
	orig_cap = capacity_orig_of(policy->cpu);

	freq = policy->freq_table[row].frequency;
	cap = orig_cap * freq / max_freq;

	return cap;
}
#endif

static unsigned int util_to_targetload(
	struct sugov_tunables *tunables, unsigned int util)
{
	int i;
	unsigned int ret;
	unsigned long flags;

	spin_lock_irqsave(&tunables->target_loads_lock, flags);

	for (i = 0; i < tunables->ntarget_loads - 1 &&
		     util >= tunables->util_loads[i+1]; i += 2)
		;

	ret = tunables->util_loads[i];
	spin_unlock_irqrestore(&tunables->target_loads_lock, flags);
	return ret;
}

unsigned int find_util_l(struct sugov_policy *sg_policy, unsigned int util)
{
	unsigned int capacity;
	int idx;

	for (idx = sg_policy->len-1; idx >= 0; idx--) {
		capacity = get_opp_capacity(sg_policy->policy, idx);
		if (capacity >= util){
			return capacity;
		}
	}
	return get_opp_capacity(sg_policy->policy, 0);
}

unsigned int find_util_h(struct sugov_policy *sg_policy, unsigned int util)
{
	unsigned int capacity;
	int idx;
	int target_idx = -1;

	for (idx = sg_policy->len-1; idx >= 0; idx--) {
		capacity = get_opp_capacity(sg_policy->policy, idx);
		if (capacity ==  util) {
			return util;
		}
		if (capacity < util) {
			target_idx = idx;
			continue;
		}
        if (target_idx == -1)
			return capacity;
		return get_opp_capacity(sg_policy->policy, target_idx);
	}
	return get_opp_capacity(sg_policy->policy, target_idx);
}

unsigned int find_closest_util(struct sugov_policy *sg_policy, unsigned int util
		, unsigned int policy)
{
	switch (policy) {
	case CPUFREQ_RELATION_L:
		return find_util_l(sg_policy, util);
	case CPUFREQ_RELATION_H:
		return find_util_h(sg_policy, util);
	default:
		return util;
	}
}

unsigned int choose_util(struct sugov_policy *sg_policy,
		unsigned int util)
{
	unsigned int prevutil, utilmin, utilmax;
	unsigned int tl;
	unsigned long orig_util = util;

	if (!sg_policy) {
		pr_err("sg_policy is null\n");
		return -EINVAL;
	}

	utilmin = 0;
	utilmax = UINT_MAX;

	do {
		prevutil = util;
		tl = util_to_targetload(sg_policy->tunables, util);
		/*
		 * Find the lowest frequency where the computed load is less
		 * than or equal to the target load.
		 */

		util = find_closest_util(sg_policy, (orig_util * 100 / tl), CPUFREQ_RELATION_L);
		trace_choose_util(util, prevutil, utilmax, utilmin, tl);
		if (util > prevutil) {
			/* The previous frequency is too low. */
			utilmin = prevutil;

			if (util >= utilmax) {
				/*
				 * Find the highest frequency that is less
				 * than freqmax.
				 */
				util = find_closest_util(sg_policy, utilmax - 1,CPUFREQ_RELATION_H);
				if (util == utilmin) {
					/*
					 * The first frequency below freqmax
					 * has already been found to be too
					 * low.  freqmax is the lowest speed
					 * we found that is fast enough.
					 */
					util = utilmax;
					break;
				}
			}
		} else if (util < prevutil) {
			/* The previous frequency is high enough. */
			utilmax = prevutil;

			if (util <= utilmin) {
				/*
				 * Find the lowest frequency that is higher
				 * than freqmin.
				 */
				util = find_closest_util(sg_policy, utilmin + 1, CPUFREQ_RELATION_L);
				/*
				 * If freqmax is the first frequency above
				 * freqmin then we have already found that
				 * this speed is fast enough.
				 */
				if (util == utilmax)
					break;
			}
		}

		/* If same frequency chosen as previous then done. */
	} while (util != prevutil);

	return util;
}
EXPORT_SYMBOL_GPL(choose_util);
#endif
/**
 * get_next_freq - Compute a new frequency for a given cpufreq policy.
 * @sg_policy: schedutil policy object to compute the new frequency for.
 * @util: Current CPU utilization.
 * @max: CPU capacity.
 *
 * If the utilization is frequency-invariant, choose the new frequency to be
 * proportional to it, that is
 *
 * next_freq = C * max_freq * util / max
 *
 * Otherwise, approximate the would-be frequency-invariant utilization by
 * util_raw * (curr_freq / max_freq) which leads to
 *
 * next_freq = C * curr_freq * util_raw / max
 *
 * Take C = 1.25 for the frequency tipping point at (util / max) = 0.8.
 *
 * The lowest driver-supported frequency which is equal or greater than the raw
 * next_freq (as calculated above) is returned, subject to policy min/max and
 * cpufreq driver limitations.
 */
static unsigned int get_next_freq(struct sugov_policy *sg_policy,
				  unsigned long util, unsigned long max)
{
	struct cpufreq_policy *policy = sg_policy->policy;
	unsigned int freq = policy->cpuinfo.max_freq;
	unsigned long next_freq = 0;

	mtk_map_util_freq((void *)sg_policy, util, freq, policy->related_cpus, &next_freq,
		get_em_wl());
	if (next_freq) {
		freq = next_freq;
	} else {
		freq = map_util_freq(util, freq, max);
		if (freq == sg_policy->cached_raw_freq && !sg_policy->need_freq_update)
			return sg_policy->next_freq;
		sg_policy->cached_raw_freq = freq;
		freq = cpufreq_driver_resolve_freq(policy, freq);
	}

	return freq;
}

inline int curr_clamp(struct rq *rq, unsigned long *util)
{
	struct task_struct *curr_task;
	int u_min = 0, u_max = 1024;
	int cpu = rq->cpu;
	unsigned long util_ori = *util;
	struct curr_uclamp_hint *cu_ht;

	rcu_read_lock();
	curr_task = rcu_dereference(rq->curr);
	if (!curr_task) {
		rcu_read_unlock();
		return -1;
	}

	if (curr_task->exit_state) {
		rcu_read_unlock();
		return -1;
	}

	cu_ht = &((struct mtk_task *) curr_task->android_vendor_data1)->cu_hint;
	if (!cu_ht->hint) {
		rcu_read_unlock();
		return -1;
	}
	u_min = curr_task->uclamp_req[UCLAMP_MIN].value;
	u_max = curr_task->uclamp_req[UCLAMP_MAX].value;
	rcu_read_unlock();

	*util = clamp_val(*util, u_min, u_max);
	if (trace_sugov_ext_curr_uclamp_enabled())
		trace_sugov_ext_curr_uclamp(cpu, curr_task->pid,
		util_ori, *util, u_min, u_max);
	return 0;
}

/*
 * This function computes an effective utilization for the given CPU, to be
 * used for frequency selection given the linear relation: f = u * f_max.
 *
 * The scheduler tracks the following metrics:
 *
 *   cpu_util_{cfs,rt,dl,irq}()
 *   cpu_bw_dl()
 *
 * Where the cfs,rt and dl util numbers are tracked with the same metric and
 * synchronized windows and are thus directly comparable.
 *
 * The cfs,rt,dl utilization are the running times measured with rq->clock_task
 * which excludes things like IRQ and steal-time. These latter are then accrued
 * in the irq utilization.
 *
 * The DL bandwidth number otoh is not a measured metric but a value computed
 * based on the task model parameters and gives the minimal utilization
 * required to meet deadlines.
 */
unsigned long mtk_cpu_util(unsigned int cpu, unsigned long util_cfs,
				 enum cpu_util_type type,
				 struct task_struct *p,
				 unsigned long min_cap, unsigned long max_cap)
{
	unsigned long dl_util, util, irq, max;
	unsigned long util_ori;
	struct rq *rq = cpu_rq(cpu);

	max = arch_scale_cpu_capacity(cpu);

	if (!uclamp_is_used() &&
	    type == FREQUENCY_UTIL && rt_rq_is_runnable(&rq->rt)) {
		return max;
	}

	/*
	 * Early check to see if IRQ/steal time saturates the CPU, can be
	 * because of inaccuracies in how we track these -- see
	 * update_irq_load_avg().
	 */
	irq = cpu_util_irq(rq);
	if (unlikely(irq >= max))
		return max;

	/*
	 * Because the time spend on RT/DL tasks is visible as 'lost' time to
	 * CFS tasks and we use the same metric to track the effective
	 * utilization (PELT windows are synchronized) we can directly add them
	 * to obtain the CPU's actual utilization.
	 *
	 * CFS and RT utilization can be boosted or capped, depending on
	 * utilization clamp constraints requested by currently RUNNABLE
	 * tasks.
	 * When there are no CFS RUNNABLE tasks, clamps are released and
	 * frequency will be gracefully reduced with the utilization decay.
	 */
	util = util_cfs + cpu_util_rt(rq);
	util_ori = util;

	if (type == FREQUENCY_UTIL) {
		bool sbb_trigger = false;
		struct sbb_cpu_data *sbb_data = per_cpu(sbb, cpu);

		if (sbb_data->active &&
				p == (struct task_struct *)UINTPTR_MAX) {

			sbb_trigger = is_sbb_trigger(rq);

			if (sbb_trigger)
				util = util * sbb_data->boost_factor;
		}

		if (p == (struct task_struct *)UINTPTR_MAX) {
			unsigned long umin, umax;
			struct sugov_rq_data *sugov_data_ptr;

			umin = READ_ONCE(rq->uclamp[UCLAMP_MIN].value);
			umax = READ_ONCE(rq->uclamp[UCLAMP_MAX].value);
			sugov_data_ptr = &((struct mtk_rq *) rq->android_vendor_data1)->sugov_data;
			WRITE_ONCE(sugov_data_ptr->uclamp[UCLAMP_MIN], umin);
			WRITE_ONCE(sugov_data_ptr->uclamp[UCLAMP_MAX], umax);
			p = NULL;
			if (cu_ctrl && curr_clamp(rq, &util) == 0)
				goto skip_rq_uclamp;
			else if (gu_ctrl) {
				unsigned long umax_with_gear;

				umax_with_gear = min_t(unsigned long,
					umax, get_cpu_gear_uclamp_max(cpu));
				util = clamp_val(util, umin, umax_with_gear);
				if (trace_sugov_ext_gear_uclamp_enabled())
					trace_sugov_ext_gear_uclamp(cpu, util_ori,
						umin, umax, util, get_cpu_gear_uclamp_max(cpu));
				goto skip_rq_uclamp;
			}
		}
		util = mtk_uclamp_rq_util_with(rq, util, p, min_cap, max_cap);
skip_rq_uclamp:
		if (sbb_trigger && trace_sugov_ext_sbb_enabled()) {
			int pid = -1;
			struct task_struct *curr;

			rcu_read_lock();
			curr = rcu_dereference(rq->curr);
			if (curr)
				pid = curr->pid;
			rcu_read_unlock();

			trace_sugov_ext_sbb(cpu, pid,
				sbb_data->boost_factor, util_ori, util,
				sbb_data->cpu_utilize,
				get_sbb_active_ratio_gear(topology_cluster_id(cpu)));
		}
	}


	dl_util = cpu_util_dl(rq);

	/*
	 * For frequency selection we do not make cpu_util_dl() a permanent part
	 * of this sum because we want to use cpu_bw_dl() later on, but we need
	 * to check if the CFS+RT+DL sum is saturated (ie. no idle time) such
	 * that we select f_max when there is no idle time.
	 *
	 * NOTE: numerical errors or stop class might cause us to not quite hit
	 * saturation when we should -- something for later.
	 */
	if (util + dl_util >= max)
		return max;

	/*
	 * OTOH, for energy computation we need the estimated running time, so
	 * include util_dl and ignore dl_bw.
	 */
	if (type == ENERGY_UTIL)
		util += dl_util;

	/*
	 * There is still idle time; further improve the number by using the
	 * irq metric. Because IRQ/steal time is hidden from the task clock we
	 * need to scale the task numbers:
	 *
	 *              max - irq
	 *   U' = irq + --------- * U
	 *                 max
	 */
	util = scale_irq_capacity(util, irq, max);
	util += irq;

	/*
	 * Bandwidth required by DEADLINE must always be granted while, for
	 * FAIR and RT, we use blocked utilization of IDLE CPUs as a mechanism
	 * to gracefully reduce the frequency when no tasks show up for longer
	 * periods of time.
	 *
	 * Ideally we would like to set bw_dl as min/guaranteed freq and util +
	 * bw_dl as requested freq. However, cpufreq is not yet ready for such
	 * an interface. So, we only do the latter for now.
	 */
	if (type == FREQUENCY_UTIL)
		util += cpu_bw_dl(rq);

	return min(max, util);
}
EXPORT_SYMBOL(mtk_cpu_util);

#if IS_ENABLED(CONFIG_MTK_SCHED_GROUP_AWARE)
void (*sugov_grp_awr_update_cpu_tar_util_hook)(int cpu);
EXPORT_SYMBOL(sugov_grp_awr_update_cpu_tar_util_hook);
#endif

static void sugov_get_util(struct sugov_cpu *sg_cpu)
{
	struct rq *rq = cpu_rq(sg_cpu->cpu);

	sg_cpu->max = arch_scale_cpu_capacity(sg_cpu->cpu);
	sg_cpu->bw_dl = cpu_bw_dl(rq);

	sg_cpu->util = mtk_cpu_util(sg_cpu->cpu, cpu_util_cfs(sg_cpu->cpu), FREQUENCY_UTIL,
							(struct task_struct *)UINTPTR_MAX,
							0, SCHED_CAPACITY_SCALE);
#if IS_ENABLED(CONFIG_OPLUS_SCHED_TUNE)
	if (_stune_util)
		sg_cpu->util = (*_stune_util)(sg_cpu->cpu, 0, sg_cpu->util);
#endif

#if IS_ENABLED(CONFIG_MTK_SCHED_GROUP_AWARE)
	if (sugov_grp_awr_update_cpu_tar_util_hook && grp_dvfs_ctrl_mode)
		sugov_grp_awr_update_cpu_tar_util_hook(sg_cpu->cpu);
#endif
}

/**
 * sugov_iowait_reset() - Reset the IO boost status of a CPU.
 * @sg_cpu: the sugov data for the CPU to boost
 * @time: the update time from the caller
 * @set_iowait_boost: true if an IO boost has been requested
 *
 * The IO wait boost of a task is disabled after a tick since the last update
 * of a CPU. If a new IO wait boost is requested after more then a tick, then
 * we enable the boost starting from IOWAIT_BOOST_MIN, which improves energy
 * efficiency by ignoring sporadic wakeups from IO.
 */
static bool sugov_iowait_reset(struct sugov_cpu *sg_cpu, u64 time,
			       bool set_iowait_boost)
{
	s64 delta_ns = time - sg_cpu->last_update;
#if IS_ENABLED(CONFIG_OPLUS_CPUFREQ_IOWAIT_PROTECT)
	unsigned int ticks = TICK_NSEC;

	if (sysctl_iowait_reset_ticks)
		ticks = sysctl_iowait_reset_ticks * TICK_NSEC;
	if (delta_ns <= ticks)
#else
	/* Reset boost only if a tick has elapsed since last request */
	if (delta_ns <= TICK_NSEC)
#endif
		return false;

	sg_cpu->iowait_boost = set_iowait_boost ? IOWAIT_BOOST_MIN : 0;
	sg_cpu->iowait_boost_pending = set_iowait_boost;

	return true;
}

/**
 * sugov_iowait_boost() - Updates the IO boost status of a CPU.
 * @sg_cpu: the sugov data for the CPU to boost
 * @time: the update time from the caller
 * @flags: SCHED_CPUFREQ_IOWAIT if the task is waking up after an IO wait
 *
 * Each time a task wakes up after an IO operation, the CPU utilization can be
 * boosted to a certain utilization which doubles at each "frequent and
 * successive" wakeup from IO, ranging from IOWAIT_BOOST_MIN to the utilization
 * of the maximum OPP.
 *
 * To keep doubling, an IO boost has to be requested at least once per tick,
 * otherwise we restart from the utilization of the minimum OPP.
 */
static void sugov_iowait_boost(struct sugov_cpu *sg_cpu, u64 time,
			       unsigned int flags)
{
	bool set_iowait_boost = flags & SCHED_CPUFREQ_IOWAIT;

	/* Reset boost if the CPU appears to have been idle enough */
	if (sg_cpu->iowait_boost &&
	    sugov_iowait_reset(sg_cpu, time, set_iowait_boost))
		return;

	/* Boost only tasks waking up after IO */
	if (!set_iowait_boost)
		return;

	/* Ensure boost doubles only one time at each request */
	if (sg_cpu->iowait_boost_pending)
		return;
	sg_cpu->iowait_boost_pending = true;

	/* Double the boost at each request */
	if (sg_cpu->iowait_boost) {
		sg_cpu->iowait_boost =
			min_t(unsigned int, sg_cpu->iowait_boost << 1, SCHED_CAPACITY_SCALE);
		return;
	}

	/* First wakeup after IO: start with minimum boost */
	sg_cpu->iowait_boost = IOWAIT_BOOST_MIN;
}

/**
 * sugov_iowait_apply() - Apply the IO boost to a CPU.
 * @sg_cpu: the sugov data for the cpu to boost
 * @time: the update time from the caller
 *
 * A CPU running a task which woken up after an IO operation can have its
 * utilization boosted to speed up the completion of those IO operations.
 * The IO boost value is increased each time a task wakes up from IO, in
 * sugov_iowait_apply(), and it's instead decreased by this function,
 * each time an increase has not been requested (!iowait_boost_pending).
 *
 * A CPU which also appears to have been idle for at least one tick has also
 * its IO boost utilization reset.
 *
 * This mechanism is designed to boost high frequently IO waiting tasks, while
 * being more conservative on tasks which does sporadic IO operations.
 */
static void sugov_iowait_apply(struct sugov_cpu *sg_cpu, u64 time)
{
#if IS_ENABLED(CONFIG_OPLUS_CPUFREQ_IOWAIT_PROTECT)
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
#endif
	unsigned long boost;

	/* No boost currently required */
	if (!sg_cpu->iowait_boost)
		return;

	/* Reset boost if the CPU appears to have been idle enough */
	if (sugov_iowait_reset(sg_cpu, time, false))
		return;
#if IS_ENABLED(CONFIG_OPLUS_CPUFREQ_IOWAIT_PROTECT)
	if (!sg_cpu->iowait_boost_pending &&
			(!sysctl_iowait_apply_ticks ||
			 (time - sg_policy->last_update >
			  (sysctl_iowait_apply_ticks * TICK_NSEC)))) {
#else
	if (!sg_cpu->iowait_boost_pending) {
#endif
		/*
		 * No boost pending; reduce the boost value.
		 */
		sg_cpu->iowait_boost >>= 1;
		if (sg_cpu->iowait_boost < IOWAIT_BOOST_MIN) {
			sg_cpu->iowait_boost = 0;
			return;
		}
	}

	sg_cpu->iowait_boost_pending = false;

	/*
	 * sg_cpu->util is already in capacity scale; convert iowait_boost
	 * into the same scale so we can compare.
	 */
	boost = (sg_cpu->iowait_boost * sg_cpu->max) >> SCHED_CAPACITY_SHIFT;
	boost = uclamp_rq_util_with(cpu_rq(sg_cpu->cpu), boost, NULL);
	if (sg_cpu->util < boost)
		sg_cpu->util = boost;
}

/*
 * Make sugov_should_update_freq() ignore the rate limit when DL
 * has increased the utilization.
 */
static inline void ignore_dl_rate_limit(struct sugov_cpu *sg_cpu)
{
	if (cpu_bw_dl(cpu_rq(sg_cpu->cpu)) > sg_cpu->bw_dl)
		sg_cpu->sg_policy->limits_changed = true;
}

#if IS_ENABLED(CONFIG_MTK_OPP_MIN)
void mtk_set_cpu_min_opp(unsigned int cpu, unsigned long min_util)
{
	int gear_id, min_opp;

	gear_id = topology_cluster_id(cpu);
	min_util = map_util_perf(min_util);
	min_opp = pd_X2Y(cpu, min_util, CAP, OPP, true);
	set_cpu_min_opp(gear_id, min_opp);
}

void mtk_set_cpu_min_opp_single(struct sugov_cpu *sg_cpu)
{
	int cpu = sg_cpu->cpu;
	struct rq *rq = cpu_rq(cpu);
	unsigned long min_util;

	min_util = READ_ONCE(rq->uclamp[UCLAMP_MIN].value);
	mtk_set_cpu_min_opp(cpu, min_util);
}

void mtk_set_cpu_min_opp_shared(struct sugov_cpu *sg_cpu)
{
	int cpu, i;
	unsigned long min_util = 0, util;
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	struct cpufreq_policy *policy = sg_policy->policy;

	for_each_cpu(i, policy->cpus) {
		struct rq *rq = cpu_rq(i);

		util = READ_ONCE(rq->uclamp[UCLAMP_MIN].value);
		min_util = max(min_util, util);
	}


	cpu = cpumask_first(policy->cpus);
	mtk_set_cpu_min_opp(cpu, min_util);
}
#else

void mtk_set_cpu_min_opp_single(struct sugov_cpu *sg_cpu)
{
}

void mtk_set_cpu_min_opp_shared(struct sugov_cpu *sg_cpu)
{
}

#endif

static void sugov_update_single(struct update_util_data *hook, u64 time,
				unsigned int flags)
{
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	struct rq *rq;
	unsigned long umin, umax;
	unsigned int next_f;
	int this_cpu = smp_processor_id();
	struct rq *this_rq = cpu_rq(this_cpu);
	struct sugov_rq_data *sugov_data_ptr;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_VT_CAP)
	unsigned long util_thresh = 0;
	unsigned int avg_nr_running = 1;
	int cluster_id = topology_cluster_id(sg_cpu->cpu);
	unsigned long util_orig;
#endif
#if IS_ENABLED(CONFIG_OPLUS_CPUFREQ_IOWAIT_PROTECT)
	unsigned long util_bak;
#endif
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
	unsigned long irq_flags;

	raw_spin_lock_irqsave(&sg_policy->update_lock, irq_flags);
#else
	raw_spin_lock(&sg_policy->update_lock);
#endif

	sugov_data_ptr = &((struct mtk_rq *) this_rq->android_vendor_data1)->sugov_data;
	WRITE_ONCE(sugov_data_ptr->enq_dvfs, false);

	sugov_iowait_boost(sg_cpu, time, flags);
	sg_cpu->last_update = time;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
	sg_policy->flags = flags;
#endif

	ignore_dl_rate_limit(sg_cpu);

	if (!sugov_should_update_freq(sg_policy, time)) {
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
		raw_spin_unlock_irqrestore(&sg_policy->update_lock, irq_flags);
#else
		raw_spin_unlock(&sg_policy->update_lock);
#endif
		return;
	}

	/* Critical Task aware thermal throttling, notify thermal */
	mtk_set_cpu_min_opp_single(sg_cpu);
	sugov_get_util(sg_cpu);
#if IS_ENABLED(CONFIG_OPLUS_CPUFREQ_IOWAIT_PROTECT)
	util_bak = sg_cpu->util;
	sugov_iowait_apply(sg_cpu, time);
	if (unlikely(eas_opt_debug_enable))
		trace_printk("[eas_opt]: enable_iowait_boost=%d, cpu:%u, max:%lu, cpu->util:%lu,iowait_util:%lu\n",
				sysctl_oplus_iowait_boost_enabled, sg_cpu->cpu, sg_cpu->max, util_bak, sg_cpu->util);
#else
	sugov_iowait_apply(sg_cpu, time);
#endif
	if (trace_sugov_ext_util_enabled()) {
		rq = cpu_rq(sg_cpu->cpu);

		umin = rq->uclamp[UCLAMP_MIN].value;
		umax = rq->uclamp[UCLAMP_MAX].value;
		if (gu_ctrl)
			umax = min_t(unsigned long,
				umax, get_cpu_gear_uclamp_max(sg_cpu->cpu));
		trace_sugov_ext_util(sg_cpu->cpu, sg_cpu->util, umin, umax);
	}
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
	fbg_freq_policy_util(sg_cpu->sg_policy->flags, sg_policy->policy->cpus, &sg_cpu->util);
#endif
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_VT_CAP)
	if (eas_opt_enable && (util_thresh_percent[cluster_id] != 100)) {
		rq = cpu_rq(sg_cpu->cpu);
		util_thresh = (sg_cpu->max) * util_thresh_cvt[cluster_id] >> SCHED_CAPACITY_SHIFT;
		avg_nr_running = rq->nr_running;
		util_orig = sg_cpu->util;
		sg_cpu->util = (util_thresh < (sg_cpu->util)) ?
			(util_thresh + ((avg_nr_running * (sg_cpu->util - util_thresh) *nr_oplus_cap_multiple[cluster_id]) >> SCHED_CAPACITY_SHIFT)) : sg_cpu->util;
		if (unlikely(eas_opt_debug_enable))
			trace_printk("[eas_opt]: cluster_id: %d, capacity: %lu, util_thresh: %lu, util_orig: %lu, "
					"util: %lu, avg_nr_running: %d, oplus_cap_multiple: %d,nr_oplus_cap_multiple: %d, util_thresh: %d\n",
					cluster_id, sg_cpu->max, util_thresh, util_orig, sg_cpu->util, avg_nr_running,
					oplus_cap_multiple[cluster_id],nr_oplus_cap_multiple[cluster_id], util_thresh_percent[cluster_id]);
	}
#endif
	next_f = get_next_freq(sg_policy, sg_cpu->util, sg_cpu->max);

	if (!sugov_update_next_freq(sg_policy, time, next_f)) {
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
        	raw_spin_unlock_irqrestore(&sg_policy->update_lock, irq_flags);
#else
		raw_spin_unlock(&sg_policy->update_lock);
#endif
		return;
	}

	/*
	 * This code runs under rq->lock for the target CPU, so it won't run
	 * concurrently on two different CPUs for the same target and it is not
	 * necessary to acquire the lock in the fast switch case.
	 */
	if (sg_policy->policy->fast_switch_enabled) {
		irq_log_store();
		cpufreq_driver_fast_switch(sg_policy->policy, next_f);
		irq_log_store();
	} else
		sugov_deferred_update(sg_policy);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
	raw_spin_unlock_irqrestore(&sg_policy->update_lock, irq_flags);
#else
	raw_spin_unlock(&sg_policy->update_lock);
#endif
}

static unsigned int sugov_next_freq_shared(struct sugov_cpu *sg_cpu, u64 time)
{
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	struct cpufreq_policy *policy = sg_policy->policy;
	struct rq *rq;
	struct sugov_rq_data *sugov_data_ptr;
	unsigned long umin, umax;
	unsigned long util = 0, max = 1;
	unsigned int j, max_cpu = 0;
	int idle = 0;
	bool _ignore_idle_ctrl = ignore_idle_ctrl;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_VT_CAP)
	unsigned long util_thresh = 0;
	unsigned long util_orig = 0;
	unsigned int avg_nr_running = 1;
	unsigned int count_cpu = 0;
	int cluster_id = topology_cluster_id(sg_cpu->cpu);
#endif
#if IS_ENABLED(CONFIG_OPLUS_CPUFREQ_IOWAIT_PROTECT)
	unsigned long util_bak;
#endif

	for_each_cpu(j, policy->cpus) {
		struct sugov_cpu *j_sg_cpu = &per_cpu(sugov_cpu, j);
		unsigned long j_util, j_max;

		sugov_get_util(j_sg_cpu);
#if IS_ENABLED(CONFIG_OPLUS_CPUFREQ_IOWAIT_PROTECT)
		util_bak = j_sg_cpu->util;
#endif
		sugov_iowait_apply(j_sg_cpu, time);
#if IS_ENABLED(CONFIG_OPLUS_CPUFREQ_IOWAIT_PROTECT)
		if (unlikely(eas_opt_debug_enable))
			trace_printk("[eas_opt]: enable_iowait_boost=%d, cpu:%d, max:%lu, cpu->util:%lu, iowait_util:%lu\n",
					sysctl_oplus_iowait_boost_enabled, j_sg_cpu->cpu, j_sg_cpu->max, util_bak, j_sg_cpu->util);
#endif
		j_util = j_sg_cpu->util;
		j_max = j_sg_cpu->max;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_VT_CAP)
		rq = cpu_rq(j);
		avg_nr_running += rq->nr_running;
		count_cpu ++;
#endif
		if (_ignore_idle_ctrl) {
			rq = cpu_rq(j);
			sugov_data_ptr =
				&((struct mtk_rq *) rq->android_vendor_data1)->sugov_data;
			idle = (available_idle_cpu(j)
				&& ((READ_ONCE(sugov_data_ptr->enq_ing) == 0) ? 1 : 0));
		}
		if (trace_sugov_ext_util_enabled()) {
			rq = cpu_rq(j);

			umin = rq->uclamp[UCLAMP_MIN].value;
			umax = rq->uclamp[UCLAMP_MAX].value;
			if (gu_ctrl)
				umax = min_t(unsigned long,
					umax, get_cpu_gear_uclamp_max(j));
			trace_sugov_ext_util(j, idle ? 0 : j_util, umin, umax);
		}
		if (idle)
			continue;
		if (j_util * max >= j_max * util) {
			util = j_util;
			max = j_max;
			max_cpu = j;
		}
	}
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
	fbg_freq_policy_util(sg_policy->flags, policy->cpus, &util);
#endif
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_VT_CAP)
	if (eas_opt_enable && (util_thresh_percent[cluster_id] != 100) && count_cpu) {
		util_thresh = max * util_thresh_cvt[cluster_id] >> SCHED_CAPACITY_SHIFT;
		avg_nr_running = mult_frac(avg_nr_running, 1, count_cpu);
		util_orig = util;
		util = (util_thresh < util) ?
			(util_thresh + ((avg_nr_running * (util - util_thresh) * nr_oplus_cap_multiple[cluster_id]) >> SCHED_CAPACITY_SHIFT)) : util;
		if (unlikely(eas_opt_debug_enable))
			trace_printk("[eas_opt]: cluster_id: %d, capacity: %lu, util_thresh: %lu, util_orig: %lu, util: %lu, avg_nr_running: %d, "
					"oplus_cap_multiple: %d,nr_oplus_cap_multiple: %d, util_thresh: %d\n",
					cluster_id, max, util_thresh, util_orig, util, avg_nr_running,
					oplus_cap_multiple[cluster_id],nr_oplus_cap_multiple[cluster_id], util_thresh_percent[cluster_id]);
	}
#endif
	return get_next_freq(sg_policy, util, max);
}

static void
sugov_update_shared(struct update_util_data *hook, u64 time, unsigned int flags)
{
	struct sugov_cpu *sg_cpu = container_of(hook, struct sugov_cpu, update_util);
	struct sugov_policy *sg_policy = sg_cpu->sg_policy;
	unsigned int next_f;
	int this_cpu = smp_processor_id();
	struct rq *this_rq = cpu_rq(this_cpu);
	struct sugov_rq_data *sugov_data_ptr;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
	unsigned long irq_flags;

	raw_spin_lock_irqsave(&sg_policy->update_lock, irq_flags);
#else
	raw_spin_lock(&sg_policy->update_lock);
#endif

	sugov_data_ptr = &((struct mtk_rq *) this_rq->android_vendor_data1)->sugov_data;
	WRITE_ONCE(sugov_data_ptr->enq_dvfs, false);

	sugov_iowait_boost(sg_cpu, time, flags);
	sg_cpu->last_update = time;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
	sg_policy->flags = flags;
#endif

	ignore_dl_rate_limit(sg_cpu);

	if (sugov_should_update_freq(sg_policy, time)) {
		next_f = sugov_next_freq_shared(sg_cpu, time);

		if (!sugov_update_next_freq(sg_policy, time, next_f))
			goto unlock;
#if IS_ENABLED(CONFIG_OPLUS_CPUFREQ_IOWAIT_PROTECT)
		sg_policy->last_update = time;
#endif

		if (sg_policy->policy->fast_switch_enabled) {
			irq_log_store();
			cpufreq_driver_fast_switch(sg_policy->policy, next_f);
			irq_log_store();
		} else
			sugov_deferred_update(sg_policy);
	}
unlock:
	/* Critical Task aware thermal throttling, notify thermal */
	mtk_set_cpu_min_opp_shared(sg_cpu);

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
	raw_spin_unlock_irqrestore(&sg_policy->update_lock, irq_flags);
#else
	raw_spin_unlock(&sg_policy->update_lock);
#endif
}

static void sugov_work(struct kthread_work *work)
{
	struct sugov_policy *sg_policy = container_of(work, struct sugov_policy, work);
	unsigned int freq;
	unsigned long flags;

	/*
	 * Hold sg_policy->update_lock shortly to handle the case where:
	 * in case sg_policy->next_freq is read here, and then updated by
	 * sugov_deferred_update() just before work_in_progress is set to false
	 * here, we may miss queueing the new update.
	 *
	 * Note: If a work was queued after the update_lock is released,
	 * sugov_work() will just be called again by kthread_work code; and the
	 * request will be proceed before the sugov thread sleeps.
	 */
	raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
	freq = sg_policy->next_freq;
	sg_policy->work_in_progress = false;
	raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);

	mutex_lock(&sg_policy->work_lock);
	__cpufreq_driver_target(sg_policy->policy, freq, CPUFREQ_RELATION_L);
	mutex_unlock(&sg_policy->work_lock);
}

static void sugov_irq_work(struct irq_work *irq_work)
{
	struct sugov_policy *sg_policy;

	sg_policy = container_of(irq_work, struct sugov_policy, irq_work);

	kthread_queue_work(&sg_policy->worker, &sg_policy->work);
}

/************************** sysfs interface ************************/

static struct sugov_tunables *global_tunables;
static DEFINE_MUTEX(global_tunables_lock);

static inline struct sugov_tunables *to_sugov_tunables(struct gov_attr_set *attr_set)
{
	return container_of(attr_set, struct sugov_tunables, attr_set);
}

static DEFINE_MUTEX(min_rate_lock);

static void update_min_rate_limit_ns(struct sugov_policy *sg_policy)
{
	mutex_lock(&min_rate_lock);
	WRITE_ONCE(sg_policy->min_rate_limit_ns, min(sg_policy->up_rate_delay_ns,
					   sg_policy->down_rate_delay_ns));
	mutex_unlock(&min_rate_lock);
}

static ssize_t up_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->up_rate_limit_us);
}

static ssize_t down_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);

	return sprintf(buf, "%u\n", tunables->down_rate_limit_us);
}

static ssize_t
up_rate_limit_us_store(struct gov_attr_set *attr_set, const char *buf, size_t count)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	struct sugov_policy *sg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->up_rate_limit_us = rate_limit_us;

	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->up_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_ns(sg_policy);
	}

	return count;
}

static ssize_t
down_rate_limit_us_store(struct gov_attr_set *attr_set, const char *buf, size_t count)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	struct sugov_policy *sg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->down_rate_limit_us = rate_limit_us;

	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->down_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_ns(sg_policy);
	}

	return count;
}

#ifdef CONFIG_OPLUS_SUGOV_USE_TL
static ssize_t target_loads_show(struct gov_attr_set *attr_set, char *buf)
{
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	int i;
	ssize_t ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&tunables->target_loads_lock, flags);
	for (i = 0; i < tunables->ntarget_loads; i++)
		ret += snprintf(buf + ret, PAGE_SIZE - ret - 1, "%u%s", tunables->target_loads[i],
			i & 0x1 ? ":" : " ");

	snprintf(buf + ret - 1, PAGE_SIZE - ret - 1, "\n");
	spin_unlock_irqrestore(&tunables->target_loads_lock, flags);
	return ret;
}

static unsigned int *get_tokenized_data(const char *buf, int *num_tokens)
{
	const char *cp;
	int i;
	int ntokens = 1;
	unsigned int *tokenized_data;
	int err = -EINVAL;

	cp = buf;
	while ((cp = strpbrk(cp + 1, " :")))
		ntokens++;

	if (!(ntokens & 0x1))
		goto err;

	tokenized_data = kmalloc(ntokens * sizeof(unsigned int), GFP_KERNEL);
	if (!tokenized_data) {
		err = -ENOMEM;
		goto err;
	}

	cp = buf;
	i = 0;
	while (i < ntokens) {
		if (sscanf(cp, "%u", &tokenized_data[i++]) != 1)
			goto err_kfree;

		cp = strpbrk(cp, " :");
		if (!cp)
			break;
		cp++;
	}

	if (i != ntokens)
		goto err_kfree;

	*num_tokens = ntokens;

	return tokenized_data;
err_kfree:
	kfree(tokenized_data);
err:
	return ERR_PTR(err);
}

static unsigned int freq2util(struct sugov_policy *sg_policy, unsigned int freq)
{
	int idx;
	unsigned int capacity, opp_freq;
#ifdef CONFIG_MTK_TINYSYS_SSPM_SUPPORT
	int cpu = sg_policy->policy->cpu;
	int cid = arch_get_cluster_id(cpu);

	freq = mt_cpufreq_find_close_freq(cid, freq);
#endif
	for (idx = sg_policy->len-1; idx >= 0; idx--) {
		capacity = get_opp_capacity(sg_policy->policy, idx);
		opp_freq = cpufreq_get_cpu_freq(sg_policy->policy->cpu, idx);
		if (freq <= opp_freq)
			return capacity;
	}
	return get_opp_capacity(sg_policy->policy, 0);
}

static ssize_t target_loads_store(struct gov_attr_set *attr_set, const char *buf,
					size_t count)
{
	int ntokens, i;
	unsigned int *new_target_loads = NULL;
	unsigned long flags;
	struct sugov_tunables *tunables = to_sugov_tunables(attr_set);
	struct sugov_policy *sg_policy;
	unsigned int *new_util_loads = NULL;
	unsigned int *temp_target_loads = NULL;
	unsigned int *temp_util_loads = NULL;


	//get the first policy if this tunnables have mutil policies
	sg_policy = list_first_entry(&attr_set->policy_list, struct sugov_policy, tunables_hook);
	if (!sg_policy) {
		pr_err("sg_policy is null\n");
		return count;
	}

	new_target_loads = get_tokenized_data(buf, &ntokens);
	if (IS_ERR(new_target_loads))
		return PTR_ERR(new_target_loads);

	new_util_loads = kzalloc(ntokens * sizeof(unsigned int), GFP_KERNEL);
	if (!new_util_loads)
		return -ENOMEM;

	memcpy(new_util_loads, new_target_loads, sizeof(unsigned int) * ntokens);
	for (i = 0; i < ntokens - 1; i += 2) {
			new_util_loads[i+1] = freq2util(sg_policy, new_target_loads[i+1]);
	}

	spin_lock_irqsave(&tunables->target_loads_lock, flags);
	temp_target_loads = tunables->target_loads;
        temp_util_loads = tunables->util_loads;

	tunables->target_loads = new_target_loads;
	tunables->ntarget_loads = ntokens;
	tunables->util_loads = new_util_loads;
	spin_unlock_irqrestore(&tunables->target_loads_lock, flags);

	if (temp_target_loads != default_target_loads)
                kfree(temp_target_loads);
        if (temp_util_loads != default_target_loads)
                kfree(temp_util_loads);

	return count;
}

ssize_t set_sugov_tl(unsigned int cpu, char *buf)
{
	struct cpufreq_policy *policy;
	struct sugov_policy *sg_policy;
	struct sugov_tunables *tunables;
	struct gov_attr_set *attr_set;
	size_t count;

	if (!buf)
		return -EFAULT;

	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return -ENODEV;

	sg_policy = policy->governor_data;
	if (!sg_policy)
		return -EINVAL;

	tunables = sg_policy->tunables;
	if (!tunables)
		return -ENOMEM;

	attr_set = &tunables->attr_set;
	count = strlen(buf);

	return target_loads_store(attr_set, buf, count);
}
EXPORT_SYMBOL_GPL(set_sugov_tl);
#endif

static struct governor_attr up_rate_limit_us = __ATTR_RW(up_rate_limit_us);
static struct governor_attr down_rate_limit_us = __ATTR_RW(down_rate_limit_us);

#ifdef CONFIG_OPLUS_SUGOV_USE_TL
static struct governor_attr target_loads =
	__ATTR(target_loads, 0664, target_loads_show, target_loads_store);
#endif

static struct attribute *sugov_attrs[] = {
	&up_rate_limit_us.attr,
	&down_rate_limit_us.attr,
#ifdef CONFIG_OPLUS_SUGOV_USE_TL
	&target_loads.attr,
#endif
	NULL
};
ATTRIBUTE_GROUPS(sugov);

static void sugov_tunables_free(struct kobject *kobj)
{
	struct gov_attr_set *attr_set = to_gov_attr_set(kobj);

	kfree(to_sugov_tunables(attr_set));
}

static struct kobj_type sugov_tunables_ktype = {
	.default_groups = sugov_groups,
	.sysfs_ops = &governor_sysfs_ops,
	.release = &sugov_tunables_free,
};

/********************** cpufreq governor interface *********************/

struct cpufreq_governor mtk_gov;

static struct sugov_policy *sugov_policy_alloc(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy;

	sg_policy = kzalloc(sizeof(*sg_policy), GFP_KERNEL);
	if (!sg_policy)
		return NULL;

	sg_policy->policy = policy;
	raw_spin_lock_init(&sg_policy->update_lock);
	return sg_policy;
}

static void sugov_policy_free(struct sugov_policy *sg_policy)
{
	kfree(sg_policy);
}

static int sugov_kthread_create(struct sugov_policy *sg_policy)
{
	struct task_struct *thread;
	struct sched_attr attr = {
		.size		= sizeof(struct sched_attr),
		.sched_policy	= SCHED_DEADLINE,
		.sched_flags	= SCHED_FLAG_SUGOV,
		.sched_nice	= 0,
		.sched_priority	= 0,
		/*
		 * Fake (unused) bandwidth; workaround to "fix"
		 * priority inheritance.
		 */
		.sched_runtime	=  1000000,
		.sched_deadline = 10000000,
		.sched_period	= 10000000,
	};
	struct cpufreq_policy *policy = sg_policy->policy;
	int ret;

	/* kthread only required for slow path */
	if (policy->fast_switch_enabled)
		return 0;

	kthread_init_work(&sg_policy->work, sugov_work);
	kthread_init_worker(&sg_policy->worker);
	thread = kthread_create(kthread_worker_fn, &sg_policy->worker,
				"sugov:%d",
				cpumask_first(policy->related_cpus));
	if (IS_ERR(thread)) {
		pr_info("failed to create sugov thread: %ld\n", PTR_ERR(thread));
		return PTR_ERR(thread);
	}

	ret = sched_setattr_nocheck(thread, &attr);
	if (ret) {
		kthread_stop(thread);
		pr_info("%s: failed to set SCHED_DEADLINE\n", __func__);
		return ret;
	}

	sg_policy->thread = thread;
	kthread_bind_mask(thread, policy->related_cpus);
	init_irq_work(&sg_policy->irq_work, sugov_irq_work);
	mutex_init(&sg_policy->work_lock);

	wake_up_process(thread);

	return 0;
}

static void sugov_kthread_stop(struct sugov_policy *sg_policy)
{
	/* kthread only required for slow path */
	if (sg_policy->policy->fast_switch_enabled)
		return;

	kthread_flush_worker(&sg_policy->worker);
	kthread_stop(sg_policy->thread);
	mutex_destroy(&sg_policy->work_lock);
}

static struct sugov_tunables *sugov_tunables_alloc(struct sugov_policy *sg_policy)
{
	struct sugov_tunables *tunables;

	tunables = kzalloc(sizeof(*tunables), GFP_KERNEL);
	if (tunables) {
		gov_attr_set_init(&tunables->attr_set, &sg_policy->tunables_hook);
		if (!have_governor_per_policy())
			global_tunables = tunables;
	}
	return tunables;
}

static void sugov_clear_global_tunables(void)
{
	if (!have_governor_per_policy())
		global_tunables = NULL;
}

static int sugov_init(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy;
	struct sugov_tunables *tunables;
	int ret = 0;
#ifdef CONFIG_OPLUS_SUGOV_USE_TL
	int cluster_id, first_cpu;
#endif

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	cpufreq_enable_fast_switch(policy);

	sg_policy = sugov_policy_alloc(policy);
	if (!sg_policy) {
		ret = -ENOMEM;
		goto disable_fast_switch;
	}

#ifdef CONFIG_OPLUS_SUGOV_USE_TL
	first_cpu = cpumask_first(policy->related_cpus);
	cluster_id = topology_cluster_id(first_cpu);
	sg_policy->len = get_nr_caps(cluster_id);
#endif

	ret = sugov_kthread_create(sg_policy);
	if (ret)
		goto free_sg_policy;

	mutex_lock(&global_tunables_lock);

	if (global_tunables) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto stop_kthread;
		}
		policy->governor_data = sg_policy;
		sg_policy->tunables = global_tunables;

		gov_attr_set_get(&global_tunables->attr_set, &sg_policy->tunables_hook);
		goto out;
	}

	tunables = sugov_tunables_alloc(sg_policy);
	if (!tunables) {
		ret = -ENOMEM;
		goto stop_kthread;
	}

	tunables->up_rate_limit_us = cpufreq_policy_transition_delay_us(policy);
	tunables->down_rate_limit_us = cpufreq_policy_transition_delay_us(policy);

#ifdef CONFIG_OPLUS_SUGOV_USE_TL
	tunables->target_loads = default_target_loads;
	tunables->ntarget_loads = ARRAY_SIZE(default_target_loads);
	//same with target_loads by default
	tunables->util_loads = default_target_loads;
	spin_lock_init(&tunables->target_loads_lock);
#endif

	policy->governor_data = sg_policy;
	sg_policy->tunables = tunables;

	ret = kobject_init_and_add(&tunables->attr_set.kobj, &sugov_tunables_ktype,
				   get_governor_parent_kobj(policy), "%s",
				   mtk_gov.name);
	if (ret)
		goto fail;

	policy->dvfs_possible_from_any_cpu = 1;

out:
	mutex_unlock(&global_tunables_lock);
	return 0;

fail:
	kobject_put(&tunables->attr_set.kobj);
	policy->governor_data = NULL;
	sugov_clear_global_tunables();

stop_kthread:
	sugov_kthread_stop(sg_policy);
	mutex_unlock(&global_tunables_lock);

free_sg_policy:
	sugov_policy_free(sg_policy);

disable_fast_switch:
	cpufreq_disable_fast_switch(policy);

	pr_info("initialization failed (error %d)\n", ret);
	return ret;
}

static void sugov_exit(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	struct sugov_tunables *tunables = sg_policy->tunables;
	unsigned int count;

	mutex_lock(&global_tunables_lock);

	count = gov_attr_set_put(&tunables->attr_set, &sg_policy->tunables_hook);
	policy->governor_data = NULL;
	if (!count)
		sugov_clear_global_tunables();

	mutex_unlock(&global_tunables_lock);

	sugov_kthread_stop(sg_policy);
	sugov_policy_free(sg_policy);
	cpufreq_disable_fast_switch(policy);
}

static int sugov_start(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	sg_policy->up_rate_delay_ns =
		sg_policy->tunables->up_rate_limit_us * NSEC_PER_USEC;
	sg_policy->down_rate_delay_ns =
		sg_policy->tunables->down_rate_limit_us * NSEC_PER_USEC;
	update_min_rate_limit_ns(sg_policy);
	sg_policy->last_freq_update_time	= 0;
	sg_policy->next_freq			= 0;
	sg_policy->work_in_progress		= false;
	sg_policy->limits_changed		= false;
	sg_policy->need_freq_update		= false;
	sg_policy->cached_raw_freq		= 0;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_FRAME_BOOST)
	sg_policy->flags = 0;
#endif

	for_each_cpu(cpu, policy->cpus) {
		struct sugov_cpu *sg_cpu = &per_cpu(sugov_cpu, cpu);

		memset(sg_cpu, 0, sizeof(*sg_cpu));
		sg_cpu->cpu			= cpu;
		sg_cpu->sg_policy		= sg_policy;
	}

	for_each_cpu(cpu, policy->cpus) {
		struct sugov_cpu *sg_cpu = &per_cpu(sugov_cpu, cpu);

		cpufreq_add_update_util_hook(cpu, &sg_cpu->update_util,
					     policy_is_shared(policy) ?
							sugov_update_shared :
							sugov_update_single);
	}
	return 0;
}

static void sugov_stop(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	for_each_cpu(cpu, policy->cpus)
		cpufreq_remove_update_util_hook(cpu);

	synchronize_rcu();

	if (!policy->fast_switch_enabled) {
		irq_work_sync(&sg_policy->irq_work);
		kthread_cancel_work_sync(&sg_policy->work);
	}
}

static void sugov_limits(struct cpufreq_policy *policy)
{
	struct sugov_policy *sg_policy = policy->governor_data;

	if (!policy->fast_switch_enabled) {
		mutex_lock(&sg_policy->work_lock);
		cpufreq_policy_apply_limits(policy);
		mutex_unlock(&sg_policy->work_lock);
	}

	sg_policy->limits_changed = true;
}

struct cpufreq_governor mtk_gov = {
	.name			= "sugov_ext",
	.owner			= THIS_MODULE,
	.init			= sugov_init,
	.exit			= sugov_exit,
	.start			= sugov_start,
	.stop			= sugov_stop,
	.limits			= sugov_limits,
};

#if IS_ENABLED(CONFIG_OPLUS_SCHED_TUNE)
static int __nocfi DetectSymbol(void)
{
	int ret;
	static struct kprobe kp = {
	    .symbol_name = "stune_util"
	};

	ret = register_kprobe(&kp);
	if (ret < 0) {
		pr_warn("Bypass  failed\n");
		return ret;
	}
	_stune_util = (stune_util_t)kp.addr;
	pr_info("_stune_util:%ps\n", _stune_util);
	unregister_kprobe(&kp);
	return 0;
}
#endif

static int __init cpufreq_mtk_init(void)
{
	int ret = 0;
	struct proc_dir_entry *dir;

	ret = mtk_static_power_init();
	if (ret) {
#if IS_ENABLED(CONFIG_MTK_GEARLESS_SUPPORT)
		pr_info("%s: failed to init MTK EM, ret: %d, %p, %p\n",
			__func__, ret, mtk_em_pd_ptr_private, mtk_em_pd_ptr_public);
#else
		pr_info("%s: failed to init MTK EM, ret: %d\n",
			__func__, ret);
#endif
		return ret;
	}

	dir = proc_mkdir("mtk_scheduler", NULL);
	if (!dir)
		return -ENOMEM;

	ret = init_sched_ctrl();
	if(ret)
		pr_info("register init_sched_ctrl failed\n");

	ret = init_opp_cap_info(dir);
	if (ret)
		return ret;
#if IS_ENABLED(CONFIG_NONLINEAR_FREQ_CTL)
	ret = register_trace_android_vh_cpufreq_fast_switch(mtk_cpufreq_fast_switch, NULL);
	if (ret)
		pr_info("register android_vh_cpufreq_fast_switch failed\n");

	ret = register_trace_android_vh_arch_set_freq_scale(
			mtk_arch_set_freq_scale, NULL);
	if (ret)
		pr_info("register android_vh_arch_set_freq_scale failed\n");
	else
		topology_clear_scale_freq_source(SCALE_FREQ_SOURCE_ARCH, cpu_possible_mask);
#endif

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
	update_ux_sched_cputopo();
#endif

#if IS_ENABLED(CONFIG_OPLUS_SCHED_TUNE)
	DetectSymbol();
#endif
	return cpufreq_register_governor(&mtk_gov);
}

static void __exit cpufreq_mtk_exit(void)
{
	clear_opp_cap_info();
	cpufreq_unregister_governor(&mtk_gov);
}

module_init(cpufreq_mtk_init);
module_exit(cpufreq_mtk_exit);

MODULE_LICENSE("GPL");
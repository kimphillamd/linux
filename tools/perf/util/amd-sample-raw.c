// SPDX-License-Identifier: GPL-2.0
/*
 * AMD specific. Provide textual annotation for IBS raw sample data.
 */

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <sys/stat.h>
#include <linux/compiler.h>
#include <asm/byteorder.h>

#include "debug.h"
#include "session.h"
#include "evlist.h"
#include "color.h"
#include "sample-raw.h"
#include "pmu-events/pmu-events.h"
#include "amd-ibs.h"

static void pr_ibs_fetch_ctl(__u64 msr_val)
{
	union ibs_fetch_ctl reg;
	reg.val = msr_val;

        printf("ibs_fetch_ctl:\t%016llx MaxCnt %7d Cnt %7d Lat %5d En %d Val %d Comp %d "
		"IcMiss %d PhyAddrValid %d%s L1TlbMiss %d L2TlbMiss %d RandEn %d%s\n",
		msr_val, reg.fetch_maxcnt << 4, reg.fetch_cnt << 4, reg.fetch_lat, reg.fetch_en, reg.fetch_val, reg.fetch_comp,
		reg.ic_miss, reg.phy_addr_valid,
		reg.phy_addr_valid ? (reg.l1tlb_pgsz == 0 ? " L1TlbPgSz 4KB" :
				      (reg.l1tlb_pgsz == 1 ? " L1TlbPgSz 2MB" :
				       (reg.l1tlb_pgsz == 2 ? " L1TlbPgSz 1GB" :
					" L1TlbPgSz RESERVED"))) : "",
		reg.l1tlb_miss, reg.l2tlb_miss, reg.rand_en,
		reg.fetch_comp ? (reg.fetch_l2_miss ? " L2Miss 1" : " L2Miss 0") : "");
}

static void pr_ic_ibs_extd_ctl(__u64 msr_val)
{
	union ic_ibs_extd_ctl reg;
	reg.val = msr_val;

	printf("ic_ibs_ext_ctl:\t%016llx IbsItlbRefillLat %3d\n", msr_val, reg.itlb_refill_lat);
}

static void pr_ibs_op_ctl(__u64 msr_val)
{
	union ibs_op_ctl reg;
	reg.val = msr_val;

        printf("ibs_op_ctl:\t%016llx MaxCnt %9d En %d Val %d CntCtl %d=%s CurCnt %9d\n",
		msr_val, ((reg.opmaxcnt_ext << 16) | reg.opmaxcnt) << 4, reg.op_en, reg.op_val,
		reg.cnt_ctl, reg.cnt_ctl ? "uOps" : "cycles", reg.opcurcnt);
}	

static void pr_ibs_op_data(__u64 msr_val)
{
	union ibs_op_data reg;
	reg.val = msr_val;

        printf("ibs_op_data:\t%016llx CompToRetCtr %5d TagToRetCtr %5d%s%s%s BrnRet %d "
		" RipInvalid %d BrnFuse %d Microcode %d\n",
		msr_val, reg.comp_to_ret_ctr, reg.tag_to_ret_ctr,
		reg.op_brn_ret ? (reg.op_return ? " OpReturn 1" : " OpReturn 0") : "",
		reg.op_brn_ret ? (reg.op_brn_taken ? " OpBrnTaken 1" : " OpBrnTaken 0") : "",
		reg.op_brn_ret ? (reg.op_brn_misp ? " OpBrnMisp 1" : " OpBrnMisp 0") : "",
		reg.op_brn_ret, reg.op_rip_invalid, reg.op_brn_fuse, reg.op_microcode);
}

static void pr_ibs_op_data2(__u64 msr_val)
{
	union ibs_op_data2 op_data2;
	static const char *data_src_str[] = {
		"",
		" DataSrc 1=(reserved)",
		" DataSrc 2=Local node cache",
		" DataSrc 3=DRAM",
		" DataSrc 4=Remote node cache",
		" DataSrc 5=(reserved)",
		" DataSrc 6=(reserved)",
		" DataSrc 7=Other"
	};

	op_data2.val = msr_val;
        printf("ibs_op_data2:\t%016llx %sRmtNode %d%s\n",
	       msr_val,
	       op_data2.data_src == 2 ? (op_data2.cache_hit_st ? "CacheHitSt 1=O-State "
							       : "CacheHitSt 0=M-state" ) : "",
	       op_data2.rmt_node, data_src_str[op_data2.data_src]);
}

static void pr_ibs_op_data3(__u64 msr_val)
{
	union ibs_op_data3 reg;
	char op_mem_width_str[sizeof("OpMemWidth __ bytes ")] = "";

	reg.val = msr_val;

	if (reg.op_mem_width)
		snprintf(op_mem_width_str, sizeof("OpMemWidth __ bytes "),
			 "OpMemWidth %2d bytes ", 1 << (reg.op_mem_width - 1));

        printf("ibs_op_data3:\t%016llx LdOp %d StOp %d DcL1TlbMiss %d DcL2TlbMiss %d "
		"DcL1TlbHit2M %d DcL1TlbHit1G %d DcL2TlbHit2M %d DcMiss %d DcMisAcc %d "
		"DcWcMemAcc %d DcUcMemAcc %d DcLockedOp %d DcMissNoMabAlloc %d DcLinAddrValid %d "
		"DcPhyAddrValid %d DcL2TlbHit1G %d L2Miss %d SwPf %d %s"
		"OpDcMissOpenMemReqs %2d DcMissLat %5d TlbRefillLat %5d\n",
		msr_val, reg.ld_op, reg.st_op, reg.dc_l1tlb_miss, reg.dc_l2tlb_miss,
		reg.dc_l1tlb_hit_2m, reg.dc_l1tlb_hit_1g, reg.dc_l2tlb_hit_2m, reg.dc_miss, reg.dc_mis_acc,
		reg.dc_wc_mem_acc, reg.dc_uc_mem_acc, reg.dc_locked_op, reg.dc_miss_no_mab_alloc, reg.dc_lin_addr_valid,
		reg.dc_phy_addr_valid, reg.dc_l2_tlb_hit_1g, reg.l2_miss, reg.sw_pf, op_mem_width_str,
		reg.op_dc_miss_open_mem_reqs, reg.dc_miss_lat, reg.tlb_refill_lat);
}

/*
 * IBS extra MSR sample data can be either from the Fetch side, or Op/Execution side
 * Test for respective valid bits in control MSRs, depending on the size
 * of the raw data set:
 * - Fetch sample: 4 msrs -> 4 bytes prepadding + 4*8 bytes = 36 byte size
 * - Op sample:    8 msrs -> 4 bytes prepadding + 8*8 bytes = 68 byte size
 */
static bool is_valid_ibs_sample(struct perf_sample *sample)
{
	size_t len = sample->raw_size;
	union ibs_control_reg ctl_reg;

	ctl_reg = *(union ibs_control_reg *)(sample->raw_data + 4);
	if ((len == 68 && ctl_reg.op_ctl.op_en && ctl_reg.op_ctl.op_val) || 
	    (len == 36 && ctl_reg.fetch_ctl.fetch_en && ctl_reg.fetch_ctl.fetch_val))
		return true;
		
	return false;
}

static void amd_ibs_dump(struct perf_sample *sample)
{
	unsigned long long *msr_val;
	union ibs_fetch_ctl fetch_ctl;
	union ibs_op_data op_data;
	union ibs_op_data3 op_data3;

	msr_val = (unsigned long long *)((char *)sample->raw_data + 4);

	if (sample->raw_size == 36) {
		fetch_ctl = (union ibs_fetch_ctl)*msr_val;
		pr_ibs_fetch_ctl(*msr_val++);
		printf("IbsFetchLinAd:\t%016llx\n", *msr_val++);
		if (fetch_ctl.phy_addr_valid)
			printf("IbsFetchPhysAd:\t%016llx\n", *msr_val);
		msr_val++;
		pr_ic_ibs_extd_ctl(*msr_val);
	} else if (sample->raw_size == 68) {
		pr_ibs_op_ctl(*msr_val++);
		op_data = (union ibs_op_data)*(msr_val + 1);
		if (!op_data.op_rip_invalid)
			printf("IbsOpRip:\t%016llx\n", *msr_val);
		msr_val++;
		pr_ibs_op_data(*msr_val++);
		pr_ibs_op_data2(*msr_val++);
		op_data3 = (union ibs_op_data3)*msr_val;
		pr_ibs_op_data3(*msr_val++);
		if (op_data3.dc_lin_addr_valid)
			printf("IbsDCLinAd:\t%016llx\n", *msr_val);
		msr_val++;
		if (op_data3.dc_phy_addr_valid)
			printf("IbsDCPhysAd:\t%016llx\n", *msr_val);
		msr_val++;
		if (op_data.op_brn_ret && *msr_val)
			printf("IbsBrTarget:\t%016llx\n", *msr_val);
	} 
}

/* AMD vendor specific raw sample function. Check for PERF_RECORD_SAMPLE events
 * and if the event was triggered by IBS display its raw data with decoded text.
 * The function is only invoked when the dump flag -D is set.
 */
void evlist__amd_sample_raw(struct evlist *evlist, union perf_event *event, struct perf_sample *sample)
{
	struct evsel *evsel;

	if (event->header.type != PERF_RECORD_SAMPLE || !sample->raw_size)
		return;

	evsel = evlist__event2evsel(evlist, event);
	if (evsel == NULL)
		return;

	/*
	 * IBS events are either (cycles or uOps) events with precision,
	 * or begin with "ibs_"{op,fetch}
	    && event != cycles or uops
	 */
	if (evsel->name) {
		if (((evsel->core.attr.precise_ip == 1 || evsel->core.attr.precise_ip == 2)) &&
		     strncmp(evsel->name, "ibs_", strlen("ibs_"))) {
			/* IBS picks up requests for cycles/0x76 or uOps/0xc1 */
			if (strncasecmp(evsel->name, "0x76", strlen("0x76")) &&
			    strncmp(evsel->name, "cycles", strlen("cycles")) &&
			    strncmp(evsel->name, "ls_not_halted_cyc", strlen("ls_not_halted_cyc")) &&
			    strncmp(evsel->name, "cpu/cpu-cycles/", strlen("cpu/cpu-cycles/")) &&
			    strncmp(evsel->name, "cpu-cycles", strlen("cpu-cycles")) &&
			    strncasecmp(evsel->name, "0xc1", strlen("0xc1")) &&
			    strncmp(evsel->name, "uops_retired", strlen("uops_retired")) &&
			    strncmp(evsel->name, "macro_ops_retired", strlen("macro_ops_retired")) &&
			    strncmp(evsel->name, "ex_ret_cops", strlen("ex_ret_cops")))
				return;
		}

		/* Lastly, -e ibs_{fetch,op}// should match, regardless of precision */
		if (strncmp(evsel->name, "ibs_", strlen("ibs_")))
			return;
	}

	if (!is_valid_ibs_sample(sample)) {
		pr_err("Invalid raw IBS MSR data encountered\n");
		return;
	}
	amd_ibs_dump(sample);
}

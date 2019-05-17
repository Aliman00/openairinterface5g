/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file lte-enb.c
 * \brief Top-level threads for gNodeB
 * \author R. Knopp, F. Kaltenberger, Navid Nikaein
 * \date 2012
 * \version 0.1
 * \company Eurecom
 * \email: knopp@eurecom.fr,florian.kaltenberger@eurecom.fr, navid.nikaein@eurecom.fr
 * \note
 * \warning
 */

#define _GNU_SOURCE
#include <pthread.h>

#include "time_utils.h"

#undef MALLOC //there are two conflicting definitions, so we better make sure we don't use it at all

#include "rt_wrapper.h"

#include "assertions.h"


#include "PHY/types.h"

#include "PHY/INIT/phy_init.h"

#include "PHY/defs_gNB.h"
#include "SCHED/sched_eNB.h"
#include "SCHED_NR/sched_nr.h"
#include "SCHED_NR/fapi_nr_l1.h"
#include "PHY/LTE_TRANSPORT/transport_proto.h"

#undef MALLOC //there are two conflicting definitions, so we better make sure we don't use it at all
//#undef FRAME_LENGTH_COMPLEX_SAMPLES //there are two conflicting definitions, so we better make sure we don't use it at all

#include "../../ARCH/COMMON/common_lib.h"

//#undef FRAME_LENGTH_COMPLEX_SAMPLES //there are two conflicting definitions, so we better make sure we don't use it at all

#include "PHY/LTE_TRANSPORT/if4_tools.h"
#include "PHY/LTE_TRANSPORT/if5_tools.h"

#include "PHY/phy_extern.h"


#include "LAYER2/MAC/mac.h"
#include "LAYER2/MAC/mac_extern.h"
#include "LAYER2/MAC/mac_proto.h"
#include "RRC/LTE/rrc_extern.h"
#include "PHY_INTERFACE/phy_interface.h"
#include "common/utils/LOG/log_extern.h"
#include "UTIL/OTG/otg_tx.h"
#include "UTIL/OTG/otg_externs.h"
#include "UTIL/MATH/oml.h"
#include "common/utils/LOG/vcd_signal_dumper.h"
#include "UTIL/OPT/opt.h"
#include "enb_config.h"


#ifndef OPENAIR2
#include "UTIL/OTG/otg_extern.h"
#endif

#if defined(ENABLE_ITTI)
# if defined(ENABLE_USE_MME)
#   include "s1ap_eNB.h"
#ifdef PDCP_USE_NETLINK
#   include "SIMULATION/ETH_TRANSPORT/proto.h"
#endif
# endif
#endif

#include "T.h"

//#define DEBUG_THREADS 1

//#define USRP_DEBUG 1
struct timing_info_t {
  //unsigned int frame, hw_slot, last_slot, next_slot;
  RTIME time_min, time_max, time_avg, time_last, time_now;
  //unsigned int mbox0, mbox1, mbox2, mbox_target;
  unsigned int n_samples;
} timing_info;

// Fix per CC openair rf/if device update
// extern openair0_device openair0;


#if defined(ENABLE_ITTI)
extern volatile int             start_gNB;
extern volatile int             start_UE;
#endif
extern volatile int                    oai_exit;

extern openair0_config_t openair0_cfg[MAX_CARDS];

extern int transmission_mode;

uint16_t sl_ahead=4;
uint16_t sf_ahead=4;
//pthread_t                       main_gNB_thread;

time_stats_t softmodem_stats_mt; // main thread
time_stats_t softmodem_stats_hw; //  hw acquisition
time_stats_t softmodem_stats_rxtx_sf; // total tx time
time_stats_t nfapi_meas; // total tx time
time_stats_t softmodem_stats_rx_sf; // total rx time

/* mutex, cond and variable to serialize phy proc TX calls
 * (this mechanism may be relaxed in the future for better
 * performances)
 */
static struct {
  pthread_mutex_t  mutex_phy_proc_tx;
  pthread_cond_t   cond_phy_proc_tx;
  volatile uint8_t phy_proc_CC_id;
} sync_phy_proc;

extern double cpuf;

void init_gNB(int,int);
void stop_gNB(int nb_inst);

int wakeup_txfh(gNB_L1_rxtx_proc_t *proc,PHY_VARS_gNB *gNB);
int wakeup_tx(PHY_VARS_gNB *gNB);
extern PARALLEL_CONF_t get_thread_parallel_conf(void);
extern WORKER_CONF_t   get_thread_worker_conf(void);


void wakeup_prach_gNB(PHY_VARS_gNB *gNB,RU_t *ru,int frame,int subframe);

extern uint8_t nfapi_mode;
extern void oai_subframe_ind(uint16_t sfn, uint16_t sf);
extern void add_subframe(uint16_t *frameP, uint16_t *subframeP, int offset);

//#define TICK_TO_US(ts) (ts.diff)
#define TICK_TO_US(ts) (ts.trials==0?0:ts.diff/ts.trials)


static inline int rxtx(PHY_VARS_gNB *gNB,gNB_L1_rxtx_proc_t *proc, char *thread_name) {
  start_meas(&softmodem_stats_rxtx_sf);

  // *******************************************************************

  if (nfapi_mode == 1) {

    // I am a PNF and I need to let nFAPI know that we have a (sub)frame tick
    uint16_t frame = proc->frame_rx;
    uint16_t slot  = proc->slot_rx;

    //add_subframe(&frame, &subframe, 4);

    //oai_subframe_ind(proc->frame_tx, proc->subframe_tx);
    //LOG_D(PHY, "oai_subframe_ind(frame:%u, subframe:%d) - NOT CALLED ********\n", frame, subframe);
    start_meas(&nfapi_meas);
    oai_subframe_ind(frame, slot);
    stop_meas(&nfapi_meas);

    if (gNB->UL_INFO.rx_ind.rx_indication_body.number_of_pdus||
        gNB->UL_INFO.harq_ind.harq_indication_body.number_of_harqs ||
        gNB->UL_INFO.crc_ind.crc_indication_body.number_of_crcs ||
        gNB->UL_INFO.rach_ind.rach_indication_body.number_of_preambles ||
        gNB->UL_INFO.cqi_ind.number_of_cqis
       ) {
      LOG_D(PHY, "UL_info[rx_ind:%05d:%d harqs:%05d:%d crcs:%05d:%d preambles:%05d:%d cqis:%d] RX:%04d%d TX:%04d%d \n", 
          NFAPI_SFNSF2DEC(gNB->UL_INFO.rx_ind.sfn_sf),   gNB->UL_INFO.rx_ind.rx_indication_body.number_of_pdus, 
          NFAPI_SFNSF2DEC(gNB->UL_INFO.harq_ind.sfn_sf), gNB->UL_INFO.harq_ind.harq_indication_body.number_of_harqs, 
          NFAPI_SFNSF2DEC(gNB->UL_INFO.crc_ind.sfn_sf),  gNB->UL_INFO.crc_ind.crc_indication_body.number_of_crcs, 
          NFAPI_SFNSF2DEC(gNB->UL_INFO.rach_ind.sfn_sf), gNB->UL_INFO.rach_ind.rach_indication_body.number_of_preambles,
          gNB->UL_INFO.cqi_ind.number_of_cqis, 
          proc->frame_rx, proc->slot_rx, 
      proc->frame_tx, proc->slot_tx);
    }
  }

  /// NR disabling

  // ****************************************
  // Common RX procedures subframe n

  T(T_GNB_PHY_DL_TICK, T_INT(gNB->Mod_id), T_INT(proc->frame_tx), T_INT(proc->slot_tx));
/*
  // if this is IF5 or 3GPP_gNB
  if (gNB && gNB->RU_list && gNB->RU_list[0] && gNB->RU_list[0]->function < NGFI_RAU_IF4p5) {
    wakeup_prach_gNB(gNB,NULL,proc->frame_rx,proc->slot_rx);
  }

  // UE-specific RX processing for subframe n
  if (nfapi_mode == 0 || nfapi_mode == 1) {
    phy_procedures_gNB_uespec_RX(gNB, proc, no_relay );
  }
*/
  pthread_mutex_lock(&gNB->UL_INFO_mutex);

  gNB->UL_INFO.frame     = proc->frame_rx;
  gNB->UL_INFO.slot      = proc->slot_rx;
  gNB->UL_INFO.module_id = gNB->Mod_id;
  gNB->UL_INFO.CC_id     = gNB->CC_id;

  gNB->if_inst->NR_UL_indication(&gNB->UL_INFO);

  pthread_mutex_unlock(&gNB->UL_INFO_mutex);

/// end
  // *****************************************
  // TX processing for subframe n+sl_ahead
  // run PHY TX procedures the one after the other for all CCs to avoid race conditions
  // (may be relaxed in the future for performance reasons)
  // *****************************************
  //if (wait_CCs(proc)<0) return(-1);
  
  if (oai_exit) return(-1);
  if(get_thread_parallel_conf() == PARALLEL_SINGLE_THREAD){
    phy_procedures_gNB_TX(gNB, proc, 1);
  }

  stop_meas( &softmodem_stats_rxtx_sf );

  LOG_D(PHY,"%s() Exit proc[rx:%d%d tx:%d%d]\n", __FUNCTION__, proc->frame_rx, proc->slot_rx, proc->frame_tx, proc->slot_tx);

#if 0
  LOG_D(PHY, "rxtx:%lld nfapi:%lld phy:%lld tx:%lld rx:%lld prach:%lld ofdm:%lld ",
      softmodem_stats_rxtx_sf.diff_now, nfapi_meas.diff_now,
      TICK_TO_US(gNB->phy_proc),
      TICK_TO_US(gNB->phy_proc_tx),
      TICK_TO_US(gNB->phy_proc_rx),
      TICK_TO_US(gNB->rx_prach),
      TICK_TO_US(gNB->ofdm_mod_stats),
      softmodem_stats_rxtx_sf.diff_now, nfapi_meas.diff_now);
  LOG_D(PHY,
    "dlsch[enc:%lld mod:%lld scr:%lld rm:%lld t:%lld i:%lld] rx_dft:%lld ",
      TICK_TO_US(gNB->dlsch_encoding_stats),
      TICK_TO_US(gNB->dlsch_modulation_stats),
      TICK_TO_US(gNB->dlsch_scrambling_stats),
      TICK_TO_US(gNB->dlsch_rate_matching_stats),
      TICK_TO_US(gNB->dlsch_turbo_encoding_stats),
      TICK_TO_US(gNB->dlsch_interleaving_stats),
      TICK_TO_US(gNB->rx_dft_stats));

  LOG_D(PHY," ulsch[ch:%lld freq:%lld dec:%lld demod:%lld ru:%lld ",
      TICK_TO_US(gNB->ulsch_channel_estimation_stats),
      TICK_TO_US(gNB->ulsch_freq_offset_estimation_stats),
      TICK_TO_US(gNB->ulsch_decoding_stats),
      TICK_TO_US(gNB->ulsch_demodulation_stats),
      TICK_TO_US(gNB->ulsch_rate_unmatching_stats));

  LOG_D(PHY, "td:%lld dei:%lld dem:%lld llr:%lld tci:%lld ",
      TICK_TO_US(gNB->ulsch_turbo_decoding_stats),
      TICK_TO_US(gNB->ulsch_deinterleaving_stats),
      TICK_TO_US(gNB->ulsch_demultiplexing_stats),
      TICK_TO_US(gNB->ulsch_llr_stats),
      TICK_TO_US(gNB->ulsch_tc_init_stats));
  LOG_D(PHY, "tca:%lld tcb:%lld tcg:%lld tce:%lld l1:%lld l2:%lld]\n\n", 
      TICK_TO_US(gNB->ulsch_tc_alpha_stats),
      TICK_TO_US(gNB->ulsch_tc_beta_stats),
      TICK_TO_US(gNB->ulsch_tc_gamma_stats),
      TICK_TO_US(gNB->ulsch_tc_ext_stats),
      TICK_TO_US(gNB->ulsch_tc_intl1_stats),
      TICK_TO_US(gNB->ulsch_tc_intl2_stats)
      );
#endif
  
  return(0);
}

static void* gNB_L1_thread_tx(void* param) {

  PHY_VARS_gNB *gNB        = (PHY_VARS_gNB*)param;
  gNB_L1_proc_t *gNB_proc  = &gNB->proc;
  gNB_L1_rxtx_proc_t *proc = &gNB_proc->L1_proc_tx;
  //PHY_VARS_gNB *gNB = RC.gNB[0][proc->CC_id];

  
  char thread_name[100];
  sprintf(thread_name,"TXnp4_%d\n",&gNB->proc.L1_proc == proc ? 0 : 1);
  //thread_top_init(thread_name,1,470000,500000,500000);
  
  //wait_sync("tx_thread");
  
  while (!oai_exit) {
    
    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_gNB_PROC_RXTX1, 0 );
    if (wait_on_condition(&proc->mutex,&proc->cond,&proc->instance_cnt,thread_name)<0) break;
    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_gNB_PROC_RXTX1, 1 );
    if (oai_exit) break;    
    // *****************************************
    // TX processing for subframe n+4
    // run PHY TX procedures the one after the other for all CCs to avoid race conditions
    // (may be relaxed in the future for performance reasons)
    // *****************************************
    
    VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_SLOT_NUMBER_TX1_GNB,proc->slot_tx);
    VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_SLOT_NUMBER_RX1_GNB,proc->slot_rx);
    VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_FRAME_NUMBER_TX1_GNB,proc->frame_tx);
    VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_FRAME_NUMBER_RX1_GNB,proc->frame_rx);

    phy_procedures_gNB_TX(gNB, proc, 1);

    pthread_mutex_lock( &proc->mutex );
    proc->instance_cnt = -1;
    // the thread can now be woken up
    if (pthread_cond_signal(&proc->cond) != 0) {
      LOG_E( PHY, "[gNB] ERROR pthread_cond_signal for gNB TXnp4 thread\n");
      exit_fun( "ERROR pthread_cond_signal" );
    }
    pthread_mutex_unlock( &proc->mutex );
    wakeup_txfh(proc,gNB);
  }

  return 0;
}

/*!
 * \brief The RX UE-specific and TX thread of gNB.
 * \param param is a \ref gNB_L1_proc_t structure which contains the info what to process.
 * \returns a pointer to an int. The storage is not on the heap and must not be freed.
 */

static void* gNB_L1_thread( void* param ) {

  static int gNB_thread_rxtx_status;
  PHY_VARS_gNB *gNB        = (PHY_VARS_gNB*)param;
  gNB_L1_proc_t *gNB_proc  = &gNB->proc;
  gNB_L1_rxtx_proc_t *proc = &gNB_proc->L1_proc;
  //PHY_VARS_gNB *gNB = RC.gNB[0][proc->CC_id];


  char thread_name[100];



  // set default return value
  gNB_thread_rxtx_status = 0;


  sprintf(thread_name,"RXn_TXnp4_%d",&gNB->proc.L1_proc == proc ? 0 : 1);
  //thread_top_init(thread_name,1,850000L,1000000L,2000000L);

  while (!oai_exit) {

    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_gNB_PROC_RXTX0, 0 );
    if (wait_on_condition(&proc->mutex,&proc->cond,&proc->instance_cnt,thread_name)<0) break;
    VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME( VCD_SIGNAL_DUMPER_FUNCTIONS_gNB_PROC_RXTX0, 1 );

    VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_SLOT_NUMBER_TX0_GNB,proc->slot_tx);
    VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_SLOT_NUMBER_RX0_GNB,proc->slot_rx);
    VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_FRAME_NUMBER_TX0_GNB,proc->frame_tx);
    VCD_SIGNAL_DUMPER_DUMP_VARIABLE_BY_NAME(VCD_SIGNAL_DUMPER_VARIABLES_FRAME_NUMBER_RX0_GNB,proc->frame_rx);

    if (oai_exit) break;

    if (gNB->CC_id==0)
    {
      if (rxtx(gNB,proc,thread_name) < 0) break;

    }
    if(get_thread_parallel_conf() == PARALLEL_RU_L1_SPLIT){
      phy_procedures_gNB_TX(gNB, proc, 1);
    }
    if (release_thread(&proc->mutex,&proc->instance_cnt,thread_name)<0) break;
    if(get_thread_parallel_conf() == PARALLEL_RU_L1_TRX_SPLIT){
      wakeup_tx(gNB);
    }
    else if(get_thread_parallel_conf() == PARALLEL_RU_L1_SPLIT){
      wakeup_txfh(proc,gNB);
    }

  } // while !oai_exit


  LOG_D(PHY, " *** Exiting gNB thread RXn_TXnp4\n");

  gNB_thread_rxtx_status = 0;
  return &gNB_thread_rxtx_status;
}


#if 0 //defined(ENABLE_ITTI) && defined(ENABLE_USE_MME)
// Wait for gNB application initialization to be complete (gNB registration to MME)
static void wait_system_ready (char *message, volatile int *start_flag) {
  
  static char *indicator[] = {".    ", "..   ", "...  ", ".... ", ".....",
			      " ....", "  ...", "   ..", "    .", "     "};
  int i = 0;
  
  while ((!oai_exit) && (*start_flag == 0)) {
    LOG_N(EMU, message, indicator[i]);
    fflush(stdout);
    i = (i + 1) % (sizeof(indicator) / sizeof(indicator[0]));
    usleep(200000);
  }
  
  LOG_D(EMU,"\n");
}
#endif





void gNB_top(PHY_VARS_gNB *gNB, int frame_rx, int slot_rx, char *string, struct RU_t_s *ru)
{
  gNB_L1_proc_t *proc           = &gNB->proc;
  gNB_L1_rxtx_proc_t *L1_proc = &proc->L1_proc;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  RU_proc_t *ru_proc=&ru->proc;

  proc->frame_rx    = frame_rx;
  proc->slot_rx = slot_rx;

  if (!oai_exit) {
    T(T_ENB_MASTER_TICK, T_INT(0), T_INT(proc->frame_rx), T_INT(proc->slot_rx));

    L1_proc->timestamp_tx = ru_proc->timestamp_rx + (sl_ahead*fp->samples_per_slot);
    L1_proc->frame_rx     = ru_proc->frame_rx;
    L1_proc->slot_rx      = ru_proc->tti_rx;
    L1_proc->frame_tx     = (L1_proc->slot_rx > (fp->slots_per_frame-1-sl_ahead)) ? (L1_proc->frame_rx+1)&1023 : L1_proc->frame_rx;
    L1_proc->slot_tx      = (L1_proc->slot_rx + sl_ahead)%fp->slots_per_frame;

    if (rxtx(gNB,L1_proc,string) < 0) LOG_E(PHY,"gNB %d CC_id %d failed during execution\n",gNB->Mod_id,gNB->CC_id);
    ru_proc->timestamp_tx = L1_proc->timestamp_tx;
    ru_proc->tti_tx       = L1_proc->slot_tx;
    ru_proc->frame_tx     = L1_proc->frame_tx;
  }
}

int wakeup_txfh(gNB_L1_rxtx_proc_t *proc,PHY_VARS_gNB *gNB) {

  RU_t *ru;
  RU_proc_t *ru_proc;


  struct timespec wait;
  wait.tv_sec=0;
  wait.tv_nsec=5000000L;
//printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~inside wakeup_txfh %d.%d IC_RU = %d\n", proc->frame_tx, proc->slot_tx, proc->instance_cnt_RUs);

  if(wait_on_condition(&proc->mutex_RUs,&proc->cond_RUs,&proc->instance_cnt_RUs,"wakeup_txfh")<0) {
    LOG_E(PHY,"Frame %d, subframe %d: TX FH not ready\n", proc->frame_tx, proc->slot_tx);
    return(-1);
  }
  pthread_mutex_lock(&gNB->proc.mutex_RU_tx);
  gNB->proc.RU_mask_tx = 0;
  pthread_mutex_unlock(&gNB->proc.mutex_RU_tx);
  if (release_thread(&proc->mutex_RUs,&proc->instance_cnt_RUs,"wakeup_txfh")<0) return(-1);

  for(int i=0; i<gNB->num_RU; i++)
  {
    ru      = gNB->RU_list[i];
    ru_proc = &ru->proc;
    if (ru_proc->instance_cnt_gNBs == 0) {
      LOG_E(PHY,"Frame %d, subframe %d: TX FH thread busy, dropping Frame %d, subframe %d\n", ru_proc->frame_tx, ru_proc->tti_tx, proc->frame_rx, proc->slot_rx);
      return(-1);
    }
    if (pthread_mutex_timedlock(&ru_proc->mutex_gNBs,&wait) != 0) {
      LOG_E( PHY, "[eNB] ERROR pthread_mutex_lock for eNB TX1 thread %d (IC %d)\n", ru_proc->tti_rx&1,ru_proc->instance_cnt_gNBs );
      exit_fun( "error locking mutex_gNB" );
      return(-1);
    }

    ru_proc->instance_cnt_gNBs = 0;
    ru_proc->timestamp_tx = proc->timestamp_tx;
    ru_proc->tti_tx       = proc->slot_tx;
    ru_proc->frame_tx     = proc->frame_tx;

    LOG_I(PHY,"Signaling tx_thread_fh for %d.%d\n",ru_proc->frame_tx,ru_proc->tti_tx);  
    // the thread can now be woken up
    if (pthread_cond_signal(&ru_proc->cond_gNBs) != 0) {
      LOG_E( PHY, "[gNB] ERROR pthread_cond_signal for gNB TXnp4 thread\n");
      exit_fun( "ERROR pthread_cond_signal" );
      return(-1);
    }
  
    pthread_mutex_unlock( &ru_proc->mutex_gNBs );
  }

  return(0);
}

int wakeup_tx(PHY_VARS_gNB *gNB) {

  gNB_L1_proc_t *proc=&gNB->proc;

  gNB_L1_rxtx_proc_t *L1_proc_tx = &proc->L1_proc_tx;
  gNB_L1_rxtx_proc_t *L1_proc    = &proc->L1_proc;

  
  struct timespec wait;
  wait.tv_sec=0;
  wait.tv_nsec=5000000L;
  
  
  if (pthread_mutex_timedlock(&L1_proc_tx->mutex,&wait) != 0) {
    LOG_E(PHY, "[SCHED][eNB] ERROR locking mutex for eNB L1_thread_tx\n");
    exit_fun("ERROR pthread_lock");
    return(-1);
  }
  while(L1_proc_tx->instance_cnt == 0){
    pthread_cond_wait(&L1_proc_tx->cond,&L1_proc_tx->mutex);
  }

  L1_proc_tx->instance_cnt = 0;

  
  L1_proc_tx->slot_rx   = L1_proc->slot_rx;
  L1_proc_tx->frame_rx      = L1_proc->frame_rx;
  L1_proc_tx->slot_tx   = L1_proc->slot_tx;
  L1_proc_tx->frame_tx      = L1_proc->frame_tx;
  L1_proc_tx->timestamp_tx  = L1_proc->timestamp_tx;
  
  // the thread can now be woken up
  if (pthread_cond_signal(&L1_proc_tx->cond) != 0) {
    LOG_E( PHY, "[eNB] ERROR pthread_cond_signal for eNB TXnp4 thread\n");
    exit_fun( "ERROR pthread_cond_signal" );
    return(-1);
  }
  
  pthread_mutex_unlock( &L1_proc_tx->mutex);

  return(0);
}

int wakeup_rxtx(PHY_VARS_gNB *gNB,RU_t *ru) {

  gNB_L1_proc_t *proc=&gNB->proc;
  gNB_L1_rxtx_proc_t *L1_proc=&proc->L1_proc;
  NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  RU_proc_t *ru_proc=&ru->proc;

  int i;
  struct timespec wait;
  
  pthread_mutex_lock(&proc->mutex_RU);
  for (i=0;i<gNB->num_RU;i++) {
    if (ru == gNB->RU_list[i]) {
      if ((proc->RU_mask&(1<<i)) > 0)
	LOG_E(PHY,"gNB %d frame %d, subframe %d : previous information from RU %d (num_RU %d,mask %x) has not been served yet!\n",
	      gNB->Mod_id,proc->frame_rx,proc->slot_rx,ru->idx,gNB->num_RU,proc->RU_mask);
      proc->RU_mask |= (1<<i);
    }
  }
  if (proc->RU_mask != (1<<gNB->num_RU)-1) {  // not all RUs have provided their information so return
    LOG_E(PHY,"Not all RUs have provided their info\n");
    pthread_mutex_unlock(&proc->mutex_RU);
    return(0);
  }
  else { // all RUs have provided their information so continue on and wakeup gNB processing
    proc->RU_mask = 0;
    pthread_mutex_unlock(&proc->mutex_RU);
  }


  wait.tv_sec=0;
  wait.tv_nsec=5000000L;

  /* accept some delay in processing - up to 5ms */
  for (i = 0; i < fp->slots_per_frame && L1_proc->instance_cnt == 0; i++) {
    LOG_W( PHY,"[gNB] SFN.SL %d.%d, gNB RXn-TXnp4 thread busy!! (i %d, cnt %i)\n", L1_proc->frame_tx, L1_proc->slot_tx, i,L1_proc->instance_cnt);
    usleep(100);
  }
  if (L1_proc->instance_cnt == 0) {
    exit_fun( "TX thread busy" );
    return(-1);
  }

  // wake up TX for subframe n+sl_ahead
  // lock the TX mutex and make sure the thread is ready
  if (pthread_mutex_timedlock(&L1_proc->mutex,&wait) != 0) {
    LOG_E( PHY, "[gNB] ERROR pthread_mutex_lock for gNB RXTX thread %d (IC %d)\n", L1_proc->slot_rx&1,L1_proc->instance_cnt );
    exit_fun( "error locking mutex" );
    return(-1);
  }
  
  ++L1_proc->instance_cnt;
  
  // We have just received and processed the common part of a subframe, say n. 
  // TS_rx is the last received timestamp (start of 1st slot), TS_tx is the desired 
  // transmitted timestamp of the next TX slot (first).
  // The last (TS_rx mod samples_per_frame) was n*samples_per_tti, 
  // we want to generate subframe (n+sl_ahead), so TS_tx = TX_rx+sl_ahead*samples_per_tti,
  // and proc->slot_tx = proc->slot_rx+sl_ahead
  L1_proc->timestamp_tx = ru_proc->timestamp_rx + (sl_ahead*fp->samples_per_slot);
  L1_proc->frame_rx     = ru_proc->frame_rx;
  L1_proc->slot_rx  = ru_proc->tti_rx;
  L1_proc->frame_tx     = (L1_proc->slot_rx > (fp->slots_per_frame-1-sl_ahead)) ? (L1_proc->frame_rx+1)&1023 : L1_proc->frame_rx;
  L1_proc->slot_tx  = (L1_proc->slot_rx + sl_ahead)%fp->slots_per_frame;

  LOG_I(PHY,"wakeupL1: passing parameter IC = %d, RX: %d.%d, TX: %d.%d to L1 sl_ahead = %d\n", L1_proc->instance_cnt, L1_proc->frame_rx, L1_proc->slot_rx, L1_proc->frame_tx, L1_proc->slot_tx, sl_ahead);

  // the thread can now be woken up
  if (pthread_cond_signal(&L1_proc->cond) != 0) {
    LOG_E( PHY, "[gNB] ERROR pthread_cond_signal for gNB RXn-TXnp4 thread\n");
    exit_fun( "ERROR pthread_cond_signal" );
    return(-1);
  }
  
  pthread_mutex_unlock( &L1_proc->mutex );

  return(0);
}
/*
void wakeup_prach_gNB(PHY_VARS_gNB *gNB,RU_t *ru,int frame,int subframe) {

  gNB_L1_proc_t *proc = &gNB->proc;
  LTE_DL_FRAME_PARMS *fp=&gNB->frame_parms;
  int i;

  if (ru!=NULL) {
    pthread_mutex_lock(&proc->mutex_RU_PRACH);
    for (i=0;i<gNB->num_RU;i++) {
      if (ru == gNB->RU_list[i]) {
	LOG_D(PHY,"frame %d, subframe %d: RU %d for gNB %d signals PRACH (mask %x, num_RU %d)\n",frame,subframe,i,gNB->Mod_id,proc->RU_mask_prach,gNB->num_RU);
	if ((proc->RU_mask_prach&(1<<i)) > 0)
	  LOG_E(PHY,"gNB %d frame %d, subframe %d : previous information (PRACH) from RU %d (num_RU %d, mask %x) has not been served yet!\n",
		gNB->Mod_id,frame,subframe,ru->idx,gNB->num_RU,proc->RU_mask_prach);
	proc->RU_mask_prach |= (1<<i);
      }
    }
    if (proc->RU_mask_prach != (1<<gNB->num_RU)-1) {  // not all RUs have provided their information so return
      pthread_mutex_unlock(&proc->mutex_RU_PRACH);
      return;
    }
    else { // all RUs have provided their information so continue on and wakeup gNB processing
      proc->RU_mask_prach = 0;
      pthread_mutex_unlock(&proc->mutex_RU_PRACH);
    }
  }
    
  // check if we have to detect PRACH first
  if (is_prach_subframe(fp,frame,subframe)>0) { 
    LOG_D(PHY,"Triggering prach processing, frame %d, subframe %d\n",frame,subframe);
    if (proc->instance_cnt_prach == 0) {
      LOG_W(PHY,"[gNB] Frame %d Subframe %d, dropping PRACH\n", frame,subframe);
      return;
    }
    
    // wake up thread for PRACH RX
    if (pthread_mutex_lock(&proc->mutex_prach) != 0) {
      LOG_E( PHY, "[gNB] ERROR pthread_mutex_lock for gNB PRACH thread %d (IC %d)\n", proc->thread_index, proc->instance_cnt_prach);
      exit_fun( "error locking mutex_prach" );
      return;
    }
    
    ++proc->instance_cnt_prach;
    // set timing for prach thread
    proc->frame_prach = frame;
    proc->subframe_prach = subframe;
    
    // the thread can now be woken up
    if (pthread_cond_signal(&proc->cond_prach) != 0) {
      LOG_E( PHY, "[gNB] ERROR pthread_cond_signal for gNB PRACH thread %d\n", proc->thread_index);
      exit_fun( "ERROR pthread_cond_signal" );
      return;
    }
    
    pthread_mutex_unlock( &proc->mutex_prach );
  }

}*/

/*!
 * \brief The prach receive thread of gNB.
 * \param param is a \ref gNB_L1_proc_t structure which contains the info what to process.
 * \returns a pointer to an int. The storage is not on the heap and must not be freed.
 */
 /*
static void* gNB_thread_prach( void* param ) {
  static int gNB_thread_prach_status;


  PHY_VARS_gNB *gNB= (PHY_VARS_gNB *)param;
  gNB_L1_proc_t *proc = &gNB->proc;

  // set default return value
  gNB_thread_prach_status = 0;

  thread_top_init("gNB_thread_prach",1,500000L,1000000L,20000000L);


  while (!oai_exit) {
    
    if (oai_exit) break;

    
    if (wait_on_condition(&proc->mutex_prach,&proc->cond_prach,&proc->instance_cnt_prach,"gNB_prach_thread") < 0) break;

    LOG_D(PHY,"Running gNB prach procedures\n");
    prach_procedures(gNB
#if (RRC_VERSION >= MAKE_VERSION(14, 0, 0))
		     ,0
#endif
		     );
    
    if (release_thread(&proc->mutex_prach,&proc->instance_cnt_prach,"gNB_prach_thread") < 0) break;
  }

  LOG_I(PHY, "Exiting gNB thread PRACH\n");

  gNB_thread_prach_status = 0;
  return &gNB_thread_prach_status;
}
*/

extern void init_td_thread(PHY_VARS_gNB *, pthread_attr_t *);
extern void init_te_thread(PHY_VARS_gNB *, pthread_attr_t *);

void init_gNB_proc(int inst) {
  
  int i=0;
  int CC_id;
  PHY_VARS_gNB *gNB;
  gNB_L1_proc_t *proc;
  gNB_L1_rxtx_proc_t *L1_proc,*L1_proc_tx;
  pthread_attr_t *attr0=NULL,*attr1=NULL;
  //*attr_prach=NULL;

  LOG_I(PHY,"%s(inst:%d) RC.nb_nr_CC[inst]:%d \n",__FUNCTION__,inst,RC.nb_nr_CC[inst]);

  for (CC_id=0; CC_id<RC.nb_nr_CC[inst]; CC_id++) {
    gNB = RC.gNB[inst][CC_id];
#ifndef OCP_FRAMEWORK
    LOG_I(PHY,"Initializing gNB processes instance:%d CC_id %d \n",inst,CC_id);
#endif
    proc = &gNB->proc;

    L1_proc                        = &proc->L1_proc;
    L1_proc_tx                     = &proc->L1_proc_tx;
    L1_proc->instance_cnt          = -1;
    L1_proc_tx->instance_cnt       = -1;
    L1_proc->instance_cnt_RUs      = 0;
    L1_proc_tx->instance_cnt_RUs   = 0;
    proc->instance_cnt_prach       = -1;
    proc->instance_cnt_asynch_rxtx = -1;
    proc->CC_id                    = CC_id;    

    proc->first_rx                 =1;
    proc->first_tx                 =1;
    proc->RU_mask                  =0;
    proc->RU_mask_tx               = (1<<gNB->num_RU)-1;
    proc->RU_mask_prach            =0;

    pthread_mutex_init( &gNB->UL_INFO_mutex, NULL);
    pthread_mutex_init( &L1_proc->mutex, NULL);
    pthread_mutex_init( &L1_proc_tx->mutex, NULL);
    pthread_cond_init( &L1_proc->cond, NULL);
    pthread_cond_init( &L1_proc_tx->cond, NULL);

    pthread_mutex_init( &proc->mutex_prach, NULL);
    pthread_mutex_init( &proc->mutex_asynch_rxtx, NULL);
    pthread_mutex_init( &proc->mutex_RU,NULL);
    pthread_mutex_init( &proc->mutex_RU_tx,NULL);
    pthread_mutex_init( &proc->mutex_RU_PRACH,NULL);

    pthread_cond_init( &proc->cond_prach, NULL);
    pthread_cond_init( &proc->cond_asynch_rxtx, NULL);

    pthread_attr_init( &proc->attr_prach);
    pthread_attr_init( &proc->attr_asynch_rxtx);
    //    pthread_attr_init( &proc->attr_td);
    //    pthread_attr_init( &proc->attr_te);
    pthread_attr_init( &L1_proc->attr);
    pthread_attr_init( &L1_proc_tx->attr);
    attr0       = &L1_proc->attr;
    attr1       = &L1_proc_tx->attr;

    LOG_I(PHY,"gNB->single_thread_flag:%d\n", gNB->single_thread_flag);

    if (get_thread_parallel_conf() == PARALLEL_RU_L1_SPLIT || get_thread_parallel_conf() == PARALLEL_RU_L1_TRX_SPLIT) {
      pthread_create( &L1_proc->pthread, attr0, gNB_L1_thread, gNB );
      pthread_create( &L1_proc_tx->pthread, attr1, gNB_L1_thread_tx, gNB);
    }
    //pthread_create( &proc->pthread_prach, attr_prach, gNB_thread_prach, gNB );

    char name[16];
    if (gNB->single_thread_flag==0) {
      snprintf( name, sizeof(name), "L1 %d", i );
      pthread_setname_np( L1_proc->pthread, name );
      snprintf( name, sizeof(name), "L1TX %d", i );
      pthread_setname_np( L1_proc_tx->pthread, name );
    }

    AssertFatal(proc->instance_cnt_prach == -1,"instance_cnt_prach = %d\n",proc->instance_cnt_prach);

    
  }

  /* setup PHY proc TX sync mechanism */
  pthread_mutex_init(&sync_phy_proc.mutex_phy_proc_tx, NULL);
  pthread_cond_init(&sync_phy_proc.cond_phy_proc_tx, NULL);
  sync_phy_proc.phy_proc_CC_id = 0;
}



/*!
 * \brief Terminate gNB TX and RX threads.
 */
void kill_gNB_proc(int inst) {

  int *status;
  PHY_VARS_gNB *gNB;
  gNB_L1_proc_t *proc;
  gNB_L1_rxtx_proc_t *L1_proc, *L1_proc_tx;
  for (int CC_id=0; CC_id<MAX_NUM_CCs; CC_id++) {
    gNB=RC.gNB[inst][CC_id];
    
    proc = &gNB->proc;
    L1_proc     = &proc->L1_proc;
    L1_proc_tx  = &proc->L1_proc_tx;

    LOG_I(PHY, "Killing TX CC_id %d inst %d\n", CC_id, inst );

    if (get_thread_parallel_conf() == PARALLEL_RU_L1_SPLIT || get_thread_parallel_conf() == PARALLEL_RU_L1_TRX_SPLIT) {
      pthread_mutex_lock(&L1_proc->mutex);
      L1_proc->instance_cnt = 0;
      pthread_cond_signal(&L1_proc->cond);
      pthread_mutex_unlock(&L1_proc->mutex);

      pthread_mutex_lock(&L1_proc_tx->mutex);
      L1_proc_tx->instance_cnt = 0;
      pthread_cond_signal(&L1_proc_tx->cond);
      pthread_mutex_unlock(&L1_proc_tx->mutex);
    }
    proc->instance_cnt_prach = 0;
    pthread_cond_signal( &proc->cond_prach );

    pthread_cond_signal( &proc->cond_asynch_rxtx );
    pthread_cond_broadcast(&sync_phy_proc.cond_phy_proc_tx);

//    LOG_D(PHY, "joining pthread_prach\n");
//    pthread_join( proc->pthread_prach, (void**)&status );    

    LOG_I(PHY, "Destroying prach mutex/cond\n");
    pthread_mutex_destroy( &proc->mutex_prach );
    pthread_cond_destroy( &proc->cond_prach );
       
    LOG_I(PHY, "Destroying UL_INFO mutex\n");
    pthread_mutex_destroy(&gNB->UL_INFO_mutex);
    if (get_thread_parallel_conf() == PARALLEL_RU_L1_SPLIT || get_thread_parallel_conf() == PARALLEL_RU_L1_TRX_SPLIT) {
      LOG_I(PHY, "Joining L1_proc mutex/cond\n");
      pthread_join( L1_proc->pthread, (void**)&status );
      LOG_I(PHY, "Joining L1_proc_tx mutex/cond\n");
      pthread_join( L1_proc_tx->pthread, (void**)&status );
    }
    LOG_I(PHY, "Destroying L1_proc mutex/cond\n");
    pthread_mutex_destroy( &L1_proc->mutex );
    pthread_cond_destroy( &L1_proc->cond );
    LOG_I(PHY, "Destroying L1_proc_tx mutex/cond\n");
    pthread_mutex_destroy( &L1_proc_tx->mutex );
    pthread_cond_destroy( &L1_proc_tx->cond );


    pthread_mutex_destroy( &proc->mutex_RU );
    pthread_mutex_destroy( &proc->mutex_RU_tx );
  }
}




void reset_opp_meas(void) {

  int sfn;
  reset_meas(&softmodem_stats_mt);
  reset_meas(&softmodem_stats_hw);
  
  for (sfn=0; sfn < 10; sfn++) {
    reset_meas(&softmodem_stats_rxtx_sf);
    reset_meas(&softmodem_stats_rx_sf);
  }
}


void print_opp_meas(void) {

  int sfn=0;
  print_meas(&softmodem_stats_mt, "Main gNB Thread", NULL, NULL);
  print_meas(&softmodem_stats_hw, "HW Acquisation", NULL, NULL);
  
  for (sfn=0; sfn < 10; sfn++) {
    print_meas(&softmodem_stats_rxtx_sf,"[gNB][total_phy_proc_rxtx]",NULL, NULL);
    print_meas(&softmodem_stats_rx_sf,"[gNB][total_phy_proc_rx]",NULL,NULL);
  }
}


/// eNB kept in function name for nffapi calls, TO FIX
void init_eNB_afterRU(void) {

  int inst,CC_id,ru_id,i,aa;
  PHY_VARS_gNB *gNB;

  LOG_I(PHY,"%s() RC.nb_nr_inst:%d\n", __FUNCTION__, RC.nb_nr_inst);

  for (inst=0;inst<RC.nb_nr_inst;inst++) {
    LOG_I(PHY,"RC.nb_nr_CC[inst]:%d\n", RC.nb_nr_CC[inst]);
    for (CC_id=0;CC_id<RC.nb_nr_CC[inst];CC_id++) {

      LOG_I(PHY,"RC.nb_nr_CC[inst:%d][CC_id:%d]:%p\n", inst, CC_id, RC.gNB[inst][CC_id]);

      gNB                                  =  RC.gNB[inst][CC_id];
      phy_init_nr_gNB(gNB,0,0);
      // map antennas and PRACH signals to gNB RX
      if (0) AssertFatal(gNB->num_RU>0,"Number of RU attached to gNB %d is zero\n",gNB->Mod_id);
      LOG_I(PHY,"Mapping RX ports from %d RUs to gNB %d\n",gNB->num_RU,gNB->Mod_id);

      //LOG_I(PHY,"Overwriting gNB->prach_vars.rxsigF[0]:%p\n", gNB->prach_vars.rxsigF[0]);

      gNB->prach_vars.rxsigF[0] = (int16_t**)malloc16(64*sizeof(int16_t*));

      LOG_I(PHY,"gNB->num_RU:%d\n", gNB->num_RU);

      for (ru_id=0,aa=0;ru_id<gNB->num_RU;ru_id++) {

	AssertFatal(gNB->RU_list[ru_id]->common.rxdataF!=NULL,
		    "RU %d : common.rxdataF is NULL\n",
		    gNB->RU_list[ru_id]->idx);

	AssertFatal(gNB->RU_list[ru_id]->prach_rxsigF!=NULL,
		    "RU %d : prach_rxsigF is NULL\n",
		    gNB->RU_list[ru_id]->idx);

	for (i=0;i<gNB->RU_list[ru_id]->nb_rx;aa++,i++) { 
	  LOG_I(PHY,"Attaching RU %d antenna %d to gNB antenna %d\n",gNB->RU_list[ru_id]->idx,i,aa);
	  gNB->prach_vars.rxsigF[0][aa]    =  gNB->RU_list[ru_id]->prach_rxsigF[i];
	  gNB->common_vars.rxdataF[aa]     =  gNB->RU_list[ru_id]->common.rxdataF[i];
	}
      }



      /* TODO: review this code, there is something wrong.
       * In monolithic mode, we come here with nb_antennas_rx == 0
       * (not tested in other modes).
       */

      //init_precoding_weights(RC.gNB[inst][CC_id]);
    }
    init_gNB_proc(inst);
  }

  for (ru_id=0;ru_id<RC.nb_RU;ru_id++) {

    AssertFatal(RC.ru[ru_id]!=NULL,"ru_id %d is null\n",ru_id);
    
    RC.ru[ru_id]->nr_wakeup_rxtx         = wakeup_rxtx;
//    RC.ru[ru_id]->wakeup_prach_eNB    = wakeup_prach_gNB;
    RC.ru[ru_id]->gNB_top             = gNB_top;
  }
}

void init_gNB(int single_thread_flag,int wait_for_sync) {

  int CC_id;
  int inst;
  PHY_VARS_gNB *gNB;

  LOG_I(PHY,"[nr-softmodem.c] gNB structure about to allocated RC.nb_nr_L1_inst:%d RC.nb_nr_L1_CC[0]:%d\n",RC.nb_nr_L1_inst,RC.nb_nr_L1_CC[0]);

  if (RC.gNB == NULL) RC.gNB = (PHY_VARS_gNB***) malloc(RC.nb_nr_L1_inst*sizeof(PHY_VARS_gNB **));
  LOG_I(PHY,"[lte-softmodem.c] gNB structure RC.gNB allocated\n");
  for (inst=0;inst<RC.nb_nr_L1_inst;inst++) {
    if (RC.gNB[inst] == NULL) RC.gNB[inst] = (PHY_VARS_gNB**) malloc(RC.nb_nr_CC[inst]*sizeof(PHY_VARS_gNB *));
    for (CC_id=0;CC_id<RC.nb_nr_L1_CC[inst];CC_id++) {
      if (RC.gNB[inst][CC_id] == NULL) RC.gNB[inst][CC_id] = (PHY_VARS_gNB*) malloc(sizeof(PHY_VARS_gNB));
      gNB                     = RC.gNB[inst][CC_id]; 
      gNB->abstraction_flag   = 0;
      gNB->single_thread_flag = single_thread_flag;
      /*nr_polar_init(&gNB->nrPolar_params,
    		  	  	NR_POLAR_PBCH_MESSAGE_TYPE,
					NR_POLAR_PBCH_PAYLOAD_BITS,
					NR_POLAR_PBCH_AGGREGATION_LEVEL);*/

      LOG_I(PHY,"Initializing gNB %d CC_id %d single_thread_flag:%d\n",inst,CC_id,single_thread_flag);
#ifndef OCP_FRAMEWORK
      LOG_I(PHY,"Initializing gNB %d CC_id %d\n",inst,CC_id);
#endif

      LOG_I(PHY,"Registering with MAC interface module\n");
      AssertFatal((gNB->if_inst         = NR_IF_Module_init(inst))!=NULL,"Cannot register interface");
      gNB->if_inst->NR_Schedule_response   = nr_schedule_response;
      gNB->if_inst->NR_PHY_config_req      = nr_phy_config_request;
      memset((void*)&gNB->UL_INFO,0,sizeof(gNB->UL_INFO));
      memset((void*)&gNB->Sched_INFO,0,sizeof(gNB->Sched_INFO));
      LOG_I(PHY,"Setting indication lists\n");
      gNB->UL_INFO.rx_ind.rx_indication_body.rx_pdu_list   = gNB->rx_pdu_list;
      gNB->UL_INFO.crc_ind.crc_indication_body.crc_pdu_list = gNB->crc_pdu_list;
      gNB->UL_INFO.sr_ind.sr_indication_body.sr_pdu_list = gNB->sr_pdu_list;
      gNB->UL_INFO.harq_ind.harq_indication_body.harq_pdu_list = gNB->harq_pdu_list;
      gNB->UL_INFO.cqi_ind.cqi_pdu_list = gNB->cqi_pdu_list;
      gNB->UL_INFO.cqi_ind.cqi_raw_pdu_list = gNB->cqi_raw_pdu_list;
      gNB->prach_energy_counter = 0;
    }

  }

  LOG_I(PHY,"[nr-softmodem.c] gNB structure allocated\n");
}


void stop_gNB(int nb_inst) {

  for (int inst=0;inst<nb_inst;inst++) {
    LOG_I(PHY,"Killing gNB %d processing threads\n",inst);
    kill_gNB_proc(inst);
  }
}
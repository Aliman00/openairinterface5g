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

#include "nr_refsig.h"

void nr_init_pbch_dmrs(PHY_VARS_gNB* gNB)
{
  unsigned int x1, x2;
  uint16_t Nid, i_ssb, i_ssb2;
  unsigned char Lmax, l, n_hf, N_hf;
  nfapi_nr_config_request_t *cfg = &gNB->gNB_config;
  NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  uint8_t reset;

  Nid = cfg->sch_config.physical_cell_id.value;

  Lmax = fp->Lmax;
  N_hf = (Lmax == 4)? 2:1;

  for (n_hf = 0; n_hf < N_hf; n_hf++) {
    for (l = 0; l < Lmax ; l++) {
      i_ssb = l & (Lmax-1);
      i_ssb2 = i_ssb + (n_hf<<2);

      reset = 1;
      x2 = (1<<11) * (i_ssb2 + 1) * ((Nid>>2) + 1) + (1<<6) * (i_ssb2 + 1) + (Nid&3);

      for (uint8_t n=0; n<NR_PBCH_DMRS_LENGTH_DWORD; n++) {
        gNB->nr_gold_pbch_dmrs[n_hf][l][n] = lte_gold_generic(&x1, &x2, reset);
        reset = 0;
      }

    }
  }

}

void nr_init_pdcch_dmrs(PHY_VARS_gNB* gNB, uint32_t Nid)
{

  uint32_t x1, x2;
  uint8_t reset;
  NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  uint32_t ***pdcch_dmrs = gNB->nr_gold_pdcch_dmrs;

  for (uint8_t slot=0; slot<fp->slots_per_frame; slot++) {
    for (uint8_t symb=0; symb<fp->symbols_per_slot; symb++) {

      reset = 1;
      x2 = ((1<<17) * (14*slot+symb+1) * ((Nid<<1)+1) + (Nid<<1));

      for (uint32_t n=0; n<NR_MAX_PDCCH_DMRS_INIT_LENGTH_DWORD; n++) {
        pdcch_dmrs[slot][symb][n] = lte_gold_generic(&x1, &x2, reset);
        reset = 0;
      }
    }  
  }

}


void nr_init_pdsch_dmrs(PHY_VARS_gNB* gNB, uint32_t Nid)
{
  
  uint32_t x1, x2;
  uint8_t reset, q;
  NR_DL_FRAME_PARMS *fp = &gNB->frame_parms;
  uint32_t ****pdsch_dmrs = gNB->nr_gold_pdsch_dmrs;

  uint16_t N_n_scid[NR_MAX_NB_CODEWORDS]={Nid, Nid}; // Not correct, appropriate scrambling IDs have to be updated to support DCI 1_1
  uint8_t n_scid=0; // again works only for 1_0
  for (uint8_t slot=0; slot<fp->slots_per_frame; slot++) {
    for (uint8_t symb=0; symb<fp->symbols_per_slot; symb++) {
        reset = 1;
        x2 = ((1<<17) * (fp->symbols_per_slot*slot+symb+1) * ((N_n_scid[n_scid]<<1)+1) +((N_n_scid[n_scid]<<1)+n_scid));
        for (uint32_t n=0; n<NR_MAX_PDSCH_DMRS_INIT_LENGTH_DWORD; n++) {
          pdsch_dmrs[slot][symb][0][n] = lte_gold_generic(&x1, &x2, reset);
          reset = 0;
        }

        for (q = 1; q < NR_MAX_NB_CODEWORDS; q++)
          memcpy(pdsch_dmrs[slot][symb][q],pdsch_dmrs[slot][symb][0],sizeof(uint32_t)*NR_MAX_PDSCH_DMRS_INIT_LENGTH_DWORD);
    }
  }
}


void nr_gold_pusch(PHY_VARS_gNB* gNB, unsigned short lbar,unsigned short *n_idDMRS, unsigned short length_dmrs)
{

  unsigned char ns,l;
  unsigned int n,x1,x2,x2tmp0;
  int nscid;
  unsigned int nid;

  /// to be updated from higher layer
  //unsigned short lbar = 0;

  for (nscid=0; nscid<2; nscid++) {
    if (n_idDMRS)
      nid = n_idDMRS[nscid];
    else
      nid = gNB->frame_parms.Nid_cell;
      
      //printf("gold pdsch nid %d lbar %d\n",nid,lbar);

    for (ns=0; ns<20; ns++) {

      for (l=0; l<length_dmrs; l++) {

      x2tmp0 = ((14*ns+(lbar+l)+1)*((nid<<1)+1))<<17;
        x2 = (x2tmp0+(nid<<1)+nscid)%(1<<31);  //cinit
        
                //printf("ns %d gold pdsch x2 %d\n",ns,x2);

        x1 = 1+ (1<<31);
        x2=x2 ^ ((x2 ^ (x2>>1) ^ (x2>>2) ^ (x2>>3))<<31);

        // skip first 50 double words (1600 bits)
        for (n=1; n<50; n++) {
          x1 = (x1>>1) ^ (x1>>4);
          x1 = x1 ^ (x1<<31) ^ (x1<<28);
          x2 = (x2>>1) ^ (x2>>2) ^ (x2>>3) ^ (x2>>4);
          x2 = x2 ^ (x2<<31) ^ (x2<<30) ^ (x2<<29) ^ (x2<<28);
            //printf("x1 : %x, x2 : %x\n",x1,x2);
        }

        for (n=0; n<NR_MAX_PUSCH_DMRS_INIT_LENGTH_DWORD; n++) {
          x1 = (x1>>1) ^ (x1>>4);
          x1 = x1 ^ (x1<<31) ^ (x1<<28);
          x2 = (x2>>1) ^ (x2>>2) ^ (x2>>3) ^ (x2>>4);
          x2 = x2 ^ (x2<<31) ^ (x2<<30) ^ (x2<<29) ^ (x2<<28);
          gNB->nr_gold_pusch[nscid][ns][l][n] = x1^x2;
            // if ((ns==2)&&(l==0))
            //printf("n=%d : c %x\n",n,x1^x2);
        }

      }
    }
  }
}
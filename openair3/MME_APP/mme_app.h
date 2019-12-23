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

/*
                                mme_app.h
                             -------------------
  AUTHOR  : Javier Morgade
  COMPANY : VICOMTECH Spain
  EMAIL   : javier.morgade@ieee.org
*/

#ifndef MME_APP_H_
#define MME_APP_H_

#include <stdint.h>
#include "platform_types.h"


void *MME_app_task(void *args_p);

//void handle_reconfiguration(module_id_t mod_id);

#endif /* MME_APP_H_ */

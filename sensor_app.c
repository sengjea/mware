/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 * $Id: app.c,v 1.5 2010/01/15 10:24:37 nifi Exp $
 */

/**
 * \file
 *         Example for using MWARE module
 * \author
 *         Seng Jea, Lee <sengjea@gmail.com>
 */

#include "contiki.h"
#include "dev/leds.h"
#include "dev/button-sensor.h"
#include "mware.h"
#include <stdio.h>

PROCESS(mware_app, "Middleware App");
AUTOSTART_PROCESSES(&mware_app);
static void
sense_callback(struct identifier *i, struct subscription *r) {
  PRINTF("Sense Called\n");
}
static void
publish_callback(struct identifier *i, uint16_t value) {
}
static const struct mware_callbacks mware_cb = { sense_callback, publish_callback };

PROCESS_THREAD(mware_app, ev, data)
{
  static struct etimer et;
  PROCESS_EXITHANDLER(;)
  PROCESS_BEGIN();
  random_init(rimeaddr_node_addr.u8[0]);
  mware_bootstrap(128, &mware_cb);
  PRINTF("Initialised Sensor.\n");
  //TODO: Initialise random set of available sensors.
  leds_on(LEDS_GREEN);  
  while (1) {
    etimer_set(&et, RANDOM_INTERVAL(20));
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    if (random_rand() % 100 < 5) {
      leds_on(LEDS_RED);  
      leds_off(LEDS_GREEN);  
      mware_shutdown(); 
      etimer_set(&et, RANDOM_INTERVAL(60));
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
      mware_bootstrap(128, &mware_cb);
    }
    leds_off(LEDS_RED);  
    leds_on(LEDS_GREEN);  
  }
  PROCESS_END(); 
}



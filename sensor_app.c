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

PROCESS_THREAD(mware_app, ev, data)
{
  PROCESS_EXITHANDLER(;)
  PROCESS_BEGIN();
  PRINTF("Init Complete:\n");
  mware_bootstrap(128);
  while (1) {
    PRINTF("<press button to fire>\n");
    PROCESS_WAIT_EVENT_UNTIL(ev   == sensors_event &&
         data == &button_sensor);
    struct subscription s = { .type = MAGNETOMETER,
                              .aggregation = MIN,
                              .period = 0x57 }; 
    mware_subscribe(1,&s);
  }
  PROCESS_END(); 
}



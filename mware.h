/**
 * \addtogroup mware
 * @{
 */


/*
 *
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
 * $Id: mware.c,v 1.12 2010/03/26 12:29:29 nifi Exp $
 */

/**
 * @file mware.c
 * @brief Implementation of a Pub/Sub Middleware
 * @author Seng Jea LEE
 * @email <sengjea@gmail.com>
 * @date 2014-02-27
 */

#include "contiki.h"
#include "net/rime.h"
#include <stddef.h>
#define DEBUG 1 
#define PRINTF(...) printf(__VA_ARGS__)
#if DEBUG
#define DPRINTF(...) printf(__VA_ARGS__)
#else
#define DPRINTF(...)
#endif

#define DPRINT2ADDR(addr) DPRINTF("%02x%02x",(addr)->u8[1], (addr)->u8[0])
#define PRINT2ADDR(addr) PRINTF("%02x%02x",(addr)->u8[1], (addr)->u8[0])
#define MWARE_SIZE 10
enum message {
	SUBSCRIBE = 0x9,
	PUBLISH = 0xA,
	UNSUBSCRIBE = 0xB
};
enum analysis {
	MAX = 0x5,
	MIN = 0x6,
	AVG = 0x7
};
enum sensor {
	LIGHT = 0x41,
	MAGNETOMETER = 0x42,
	ACCELEROMETER = 0x43
};


struct identifier {
	rimeaddr_t subscriber;
	uint8_t id;
};

struct subscription {
	enum sensor type;
	enum analysis aggregation;
	uint8_t period;
};

struct msg_header {
	enum message message_type;
	struct identifier edition;
	uint8_t hops;
};

struct subscription_item {
	struct subscription_item *next;
	struct identifier id;
	struct subscription sub;
	rimeaddr_t parent;
	uint8_t cost;
	timer_t t;
};

struct manuscript {
	rimeaddr_t next_hop;
	uint16_t timestamp;
	uint16_t v1;
	uint16_t v2;
};

struct mware_callbacks {
	void (*sense) (struct identifier *i, struct subscription *r);
	void (*publish) (struct identifier *i, struct manuscript *m);
};

void mware_bootstrap(uint16_t channel, struct mware_callbacks *cb);

void mware_subscribe(uint8_t id, struct subscription *r);
void mware_publish(struct identifier *i, struct manuscript *m);
void mware_unsubscribe(uint8_t id);


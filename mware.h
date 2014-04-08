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

#ifndef __COLLECT_H__
#define __MWARE_H__
#include "contiki.h"
#include "lib/random.h"
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
#define RANDOM_INTERVAL(i) ((i/2)+random_rand()%(i)) 
#define MWARE_INFINITE_COST 30
#define MWARE_BEACON_INTERVAL 30 
#define MWARE_SLOT_SIZE CLOCK_SECOND
#define MWARE_SHELFLIFE 120
#define MWARE_ATTRIBUTES  { PACKETBUF_ATTR_PACKET_TYPE, PACKETBUF_ATTR_BIT* 4}, \
	BROADCAST_ATTRIBUTES
struct identifier {
	rimeaddr_t subscriber;
	uint16_t id;
	uint16_t cost;
};
//struct verifier {
//  uint32_t nonce;
//  uint32_t response;
//};
enum analysis {
	MAX = 0x1,
	MIN = 0x2,
	AVG = 0x3
};

enum sensor {
	LIGHT = 0x1,
	MAGNETOMETER = 0x2,
	ACCELEROMETER = 0x3
};

struct subscription {
	enum sensor type;
	enum analysis aggregation;
	clock_time_t period;
};

struct subscription_item {
	struct subscription_item *next;
	struct identifier id;
	struct subscription sub;
	rimeaddr_t next_hop;
	uint16_t v1;
	uint16_t v2;
	clock_time_t last_heard;
	clock_time_t last_shout;
	struct ctimer sense_timer;
	struct ctimer publish_timer;
};

#define MWARE_MSG_SUB 0x1
struct subscribe_message {
	struct identifier id;
	struct subscription sub;
	uint8_t reading_count;	
	clock_time_t remaining;
};

#define MWARE_MSG_PUB 0x2
struct publish_message {
	struct identifier id;
	rimeaddr_t next_hop;
	uint16_t v1;
	uint16_t v2;
};

#define MWARE_MSG_UNSUB 0x3
struct unsubscribe_message {
	struct identifier id;
};


struct mware_callbacks {
	void (*sense) (struct identifier *i, struct subscription *s);
	void (*publish) (struct identifier *i, uint16_t value);
};

void mware_bootstrap(uint16_t channel, const struct mware_callbacks *cb);

int mware_subscribe(struct identifier *i, struct subscription *s);
void mware_publish(struct identifier *i, uint16_t v1, uint16_t v2);
void mware_unsubscribe(struct identifier *i);
void mware_shutdown(void);
#endif /* __MWARE_H__*/
/* vim: set ts=8 sw=8 tw=80 noet :*/

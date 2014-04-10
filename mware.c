#include "contiki.h"
#include "mware.h"
#include <stddef.h>
#include "net/rime/broadcast.h"
#include <string.h>

static struct broadcast_conn connection;
static const struct mware_callbacks *callback;
static void wind_item_timer(struct subscription_item *si, clock_time_t remaining);
static const struct packetbuf_attrlist attributes[] =
{
	MWARE_ATTRIBUTES
		PACKETBUF_ATTR_LAST
};

/*------------Subscription Manager (movable) -----------------*/
LIST(subscription_list);
MEMB(subscription_memb, struct subscription_item, MWARE_SIZE);

void
subscription_init() {
	list_init(subscription_list);
	memb_init(&subscription_memb);
}

struct subscription_item *
subscription_get(struct identifier * i) {
	struct subscription_item *si;
	for (si = list_head(subscription_list);
			si != NULL; si = list_item_next(si)) {
		if (si->id.id == i->id 
				&& rimeaddr_cmp(&si->id.subscriber, &i->subscriber)) {
			return si;
		}
	}
	return NULL;
}
void
subscription_print(struct subscription_item *si, char *title) {
	DPRINTF("%s:", title);
	DPRINT2ADDR(&si->id.subscriber);
	DPRINTF(", i:%d, nh:", si->id.id);
	DPRINT2ADDR(&si->next_hop);
	DPRINTF(", c:%d, e:%d\n", si->id.cost, si->sub.epoch);
}

void
subscription_print_table(void) {
	struct subscription_item *si;
	for (si = list_head(subscription_list);
			si != NULL; si = list_item_next(si)) {
		subscription_print(si, "st");	
	}
}

void
subscription_update_last_heard(const rimeaddr_t  *next_hop) {
	struct subscription_item *si;
	for (si = list_head(subscription_list);
			si != NULL; si = list_item_next(si)) {
		if (si->last_heard != 0 &&
				rimeaddr_cmp(&si->next_hop, next_hop)) {
			si->last_heard = clock_seconds();	
		}
	}
}
void
subscription_reset_last_shout(struct subscription_item *si) {
	si->last_shout = 0;
}
void
subscription_update_last_shout(struct subscription_item *si) {
	si->last_shout = clock_seconds();
}
int
subscription_update(struct subscription_item *si,
		rimeaddr_t *next_hop, uint8_t cost) {
	if (rimeaddr_cmp(&si->next_hop, next_hop)) {
		if (si->id.cost != cost) {	
			si->id.cost = cost;
			return 1;
		}
	} else if (cost < si->id.cost) {
		rimeaddr_copy(&si->next_hop, next_hop);
		si->id.cost = cost;
		return 1;
	}
	return 0;
}

struct subscription_item *
subscription_insert(struct identifier *i, struct subscription *s,
		rimeaddr_t *next_hop, uint8_t cost) {
	struct subscription_item *si;
	si = memb_alloc(&subscription_memb);
	if (si == NULL) {
		return NULL;
	}
	memcpy(&si->id,i,sizeof(struct identifier));
	memcpy(&si->sub,s,sizeof(struct subscription));
	rimeaddr_copy(&si->next_hop, next_hop);
	si->id.cost = cost;
	si->last_heard = clock_seconds();	
	si->last_shout = 0;	
	si->sub.jitter = 0;
	list_add(subscription_list, si);
	return si;
}
int
subscription_get_jitter(struct subscription *s) {
	return s->jitter;
}
clock_time_t
subscription_set_jitter(struct subscription *s, clock_time_t j) {
	s->jitter = j;
	return j;
}
void
subscription_clean(void) {
	struct subscription_item *si;
	for (si = list_head(subscription_list);
			si != NULL; si = list_item_next(si)) {
		ctimer_stop(&si->t);
		subscription_init();  
	}
}

void
subscription_remove(struct subscription_item *si) {
	ctimer_stop(&si->t);
	list_remove(subscription_list, si);
	memset(si,0,sizeof(struct subscription_item)); 
	memb_free(&subscription_memb, si);

}

void
subscription_unsubscribe(struct subscription_item *si) {
	if (!rimeaddr_cmp(&si->next_hop, &rimeaddr_null)) { 
		subscription_reset_last_shout(si);	
		rimeaddr_copy(&si->next_hop, &rimeaddr_null);
	}
}

int 
subscription_is_unsubscribed(struct subscription_item *si) {
	return (rimeaddr_cmp(&si->next_hop, &rimeaddr_null));
}	
int 
subscription_is_stale(struct subscription_item *si) {
	return (si->last_heard + MWARE_SHELFLIFE < clock_seconds());
}	
int
subscription_needs_broadcast(struct subscription_item *si) {
	return (si->last_shout == 0 ||
			si->last_shout + MWARE_BEACON_INTERVAL < clock_seconds());
}
void
subscription_data_reset(struct subscription_item *si) {
	si->sub.epoch++;	
	si->v1 = 0;
	si->v2 = 0;
}
void
subscription_data_input(struct subscription_item *si, uint16_t v1, uint16_t v2) {
	if (v2 == 0)  { 
		return;	
	}	
	switch (si->sub.aggregation) {
		case MIN:
			if (si->v1 == 0 || v1 < si->v1) {
				si->v1 = v1;
			}
			break;
		case MAX:
			if (si->v1 == 0 || v1 > si->v1) {
				si->v1 = v1;
			}
			break;
		case AVG:
			si->v1 += v1;
			break; 
	}
	si->v2 += v2;
}
uint16_t
subscription_data_output(struct subscription_item *si) {
	switch (si->sub.aggregation) {
		case MIN:
		case MAX:
			//PRINTF("data_output: MAX|MIN %d,%d\n",si->v1, si->v2);	
			return si->v1;  
		case AVG:
			//PRINTF("data_output: AVG %d,%d\n",si->v1, si->v2);	
			if (si->v2 > 0) {
				return si->v1/si->v2; 
			}
	}
	return 0;
}
clock_time_t
subscription_time_to_next_sense(struct subscription_item *si) {
	return timer_remaining(&si->t.etimer.timer)
		+ (si->sub.jitter > 0 ? si->sub.period/2 - si->sub.jitter : 0);
}
/*----------------------------------------------*/
int
identifier_is_mine(struct identifier *i) {
	return rimeaddr_cmp(&i->subscriber, &rimeaddr_node_addr);
}
/*----------------------------------------------*/
	static void
print_raw_packetbuf(void)
{
	uint16_t i;
	for(i = 0; i < packetbuf_hdrlen(); i++) {
		DPRINTF("%02x ", *(char *)(packetbuf_hdrptr() + i));
	}
	DPRINTF("| ");
	for(i = 0; i < packetbuf_datalen(); i++) {
		DPRINTF("%02x ", *(char *)(packetbuf_dataptr() + i));
	}
	DPRINTF("[%d]\n",
			(int16_t) (packetbuf_dataptr() - packetbuf_hdrptr()));
}
/*-------------------------------------------------------*/
void
broadcast_subscription(struct subscription_item *si) {
	struct subscribe_message *msg; 
	packetbuf_clear();
	packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, MWARE_MSG_SUB);
	packetbuf_set_datalen(sizeof(struct subscribe_message)); 
	msg = packetbuf_dataptr();
	memcpy(&msg->id, &si->id, sizeof(struct identifier));
	memcpy(&msg->sub, &si->sub, sizeof(struct subscription));
	//'Jitter' set here is reused later.	
	subscription_set_jitter(&msg->sub,subscription_time_to_next_sense(si));	
	if (broadcast_send(&connection)) {
		subscription_print(si, "bs"); 
		subscription_update_last_shout(si);	
	}
}
void
broadcast_publication(struct subscription_item *si) {
	struct publish_message *msg; 
	packetbuf_clear();
	packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, MWARE_MSG_PUB);
	packetbuf_set_datalen(sizeof(struct publish_message)); 
	msg = packetbuf_dataptr();  
	memcpy(&msg->id, &si->id,sizeof(struct identifier)); 
	rimeaddr_copy(&msg->next_hop, &si->next_hop); 
	msg->v1 = si->v1;
	msg->v2 = si->v2; 
	subscription_print(si, "bp");	
	broadcast_send(&connection);
}
void
broadcast_unsubscription(struct subscription_item *si) {
	struct unsubscribe_message *msg; 
	packetbuf_clear();
	packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, MWARE_MSG_UNSUB);
	packetbuf_set_datalen(sizeof(struct unsubscribe_message)); 
	msg = packetbuf_dataptr();
	memcpy(&msg->id, &si->id, sizeof(struct identifier));
	if (broadcast_send(&connection)) {
		subscription_print(si, "bu"); 
		subscription_update_last_shout(si);	
	}
}
/*-------------------Broadcast Reception---------------------------*/
struct subscribe_message *
packetbuf_msg_sub(void) {
	return (struct subscribe_message *) packetbuf_dataptr();
}
struct publish_message *
packetbuf_msg_pub(void) {
	return (struct publish_message *) packetbuf_dataptr();
}
struct unsubscribe_message *
packetbuf_msg_unsub(void) {
	return (struct unsubscribe_message *) packetbuf_dataptr();
}
int
message_is_published_to_me(void) {
	return (packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE) == MWARE_MSG_PUB &&
			rimeaddr_cmp(&(packetbuf_msg_pub())->next_hop,
				&rimeaddr_node_addr));
}
	void
packet_received(struct broadcast_conn *connection, const rimeaddr_t *from)
{
	struct subscription_item *si;
	switch(packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE)) {
	case MWARE_MSG_SUB:
		si = subscription_get(&(packetbuf_msg_sub())->id);	
		if (si != NULL) {
			if (subscription_is_unsubscribed(si)) {
				subscription_reset_last_shout(si);	
			} else if (subscription_update(si, (rimeaddr_t *) from,
						(packetbuf_msg_sub())->id.cost + 1)) {
				//TODO: Fix this!	
				si->sub.epoch = (packetbuf_msg_sub())->sub.epoch;	
			} else {
				si = NULL;
			}
		} else if ((packetbuf_msg_sub())->sub.period
			> (packetbuf_msg_sub())->id.cost * MWARE_SLOT_SIZE) {
			si = subscription_insert(&(packetbuf_msg_sub())->id,
					&(packetbuf_msg_sub())->sub,
					( identifier_is_mine(&(packetbuf_msg_sub())->id) ?
					  	&rimeaddr_null : (rimeaddr_t *) from),
					(packetbuf_msg_sub())->id.cost + 1);	
		}
		if (si != NULL) {	
			//TODO: Fix this +1 shit..
			clock_time_t next_sense_time = subscription_get_jitter(&(packetbuf_msg_sub())->sub) +
					si->sub.period - MWARE_SLOT_SIZE + 1;
			si->sub.epoch++;	
			subscription_set_jitter(&si->sub,1);	
			wind_item_timer(si,next_sense_time);
		}	
	break;
	case MWARE_MSG_PUB:
		si = subscription_get(&(packetbuf_msg_pub())->id);
		if (si == NULL || subscription_is_unsubscribed(si)) {
			break;
		}
		if (message_is_published_to_me()) {	
			//PRINTF("sdi\n");	
			subscription_data_input(si,
					(packetbuf_msg_pub())->v1,
					(packetbuf_msg_pub())->v2);	
		}  
		break;
	case MWARE_MSG_UNSUB:
		si = subscription_get(&(packetbuf_msg_unsub())->id);
		if (si != NULL) {
			subscription_unsubscribe(si);
		}
		break;
	}
	subscription_update_last_heard(from);	
	packetbuf_clear();
}

static const struct broadcast_callbacks connection_cb = { packet_received };
/*--------------------Subscription Broadcasting --------------------------*/
static void
mware_service_item(void *p) {
	struct subscription_item *si = (struct subscription_item *) p;	
	//PRINTF("msip\n");	
	if (subscription_get_jitter(&si->sub) > 0) {	
		if (identifier_is_mine(&si->id)) {
			callback->publish(&si->id,&si->sub,
					subscription_data_output(si)); 
		} else {
			broadcast_publication(si);	
		}	
		subscription_data_reset(si);
	} else {
		if (subscription_is_stale(si) &&
				(!identifier_is_mine(&si->id) || subscription_is_unsubscribed(si))) {
			subscription_remove(si); 
			return;	
		}	
		if (!subscription_is_unsubscribed(si)) {
			//PRINTF("cbs\n");	
			callback->sense(&si->id, &si->sub); 
		}
		if (subscription_needs_broadcast(si)) {
			if (subscription_is_unsubscribed(si)) {
				broadcast_unsubscription(si);
			} else {
				broadcast_subscription(si); 
			} 
		}
	}
	wind_item_timer(si, si->sub.period/2);	
}
static void wind_item_timer(struct subscription_item *si, clock_time_t offset) {
	clock_time_t next_time;	
	if (subscription_get_jitter(&si->sub) > 0) {	
		next_time = offset - subscription_get_jitter(&si->sub);	
		subscription_set_jitter(&si->sub,0);
	} else {
		//Jitter must not be 0, defined as sense cycle.
		next_time = offset +
			subscription_set_jitter(&si->sub, FULL_JITTER(MWARE_SLOT_SIZE/2 - 1) + 1);
	}	
	if(ctimer_expired(&si->t)) {
		ctimer_set(&si->t, next_time,
			mware_service_item, si);
	}
}

/*--------------------API Implementations--------------------------*/
void
mware_bootstrap(uint16_t channel, const struct mware_callbacks *m) {
	subscription_init();
	broadcast_open(&connection, channel, &connection_cb);
	channel_set_attributes(channel, attributes);
	callback = m;
}

int
mware_subscribe(struct identifier *i, struct subscription *s) {
	struct subscription_item *si;	
	si = subscription_insert(i, s, &i->subscriber, 0);
	if (si != NULL) {	
		wind_item_timer(si, si->sub.period/2);	
		return 1;
	} else {
		return 0;
	}
}

void
mware_unsubscribe(struct identifier *i) {
	struct subscription_item *si;
	si = subscription_get(i);	
	if (si != NULL) {
		subscription_unsubscribe(si);
	}
}

void
mware_publish(struct identifier *i, uint16_t v1, uint16_t v2) {
	struct subscription_item *si;
	si = subscription_get(i);
	if (si == NULL || subscription_is_unsubscribed(si)) {
		return;
	}
	subscription_data_input(si, v1, v2);	
}

void
mware_shutdown(void) {
	broadcast_close(&connection);
	subscription_clean();  
}

/* vim: set ts=8 sw=8 tw=80 noet :*/

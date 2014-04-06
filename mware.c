#include "contiki.h"
#include "mware.h"
#include <stddef.h>
#include "net/rime/broadcast.h"
#include <string.h>

static struct broadcast_conn connection;
static const struct mware_callbacks *callback;
static struct ctimer timer;
static void wind_timer(struct subscription_item *si);
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
		if (memcmp(i,&si->id,sizeof(struct identifier)) == 0) {
			return si;
		}
	}
	return NULL;
}

void
subscription_print_table(void) {
	struct subscription_item *si;
	for (si = list_head(subscription_list);
			si != NULL; si = list_item_next(si)) {
		PRINTF("- s:");
		PRINT2ADDR(&si->id.subscriber);
		PRINTF(", i:%d, nh:", si->id.id);
		PRINT2ADDR(&si->next_hop);
		PRINTF(", c:%d\n", si->cost);
	}
}

void
subscription_next_hop_refresh(const rimeaddr_t  *next_hop) {
	struct subscription_item *si;
	for (si = list_head(subscription_list);
			si != NULL; si = list_item_next(si)) {
		if (rimeaddr_cmp(&si->next_hop, next_hop) == 1) {
			si->last_heard = clock_seconds();	
		}
	}
}

int
subscription_update(struct subscription_item *si,
		rimeaddr_t *next_hop, uint8_t cost) {
	si->last_heard = clock_seconds();	
	if (rimeaddr_cmp(&si->next_hop, next_hop) == 1) {
		si->cost = cost;
		return 0;
	} else if (cost < si->cost) {
		rimeaddr_copy(&si->next_hop, next_hop);
		si->cost = cost;
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
	si->cost = cost;
	si->last_heard = clock_seconds();	
	si->last_shout = 0;	
	list_add(subscription_list, si);
	return si;
}

void
subscription_remove(struct identifier *i) {
	struct subscription_item *si = subscription_get(i);
	if (si == NULL) {
		return;
	}
	ctimer_stop(&si->t);
	list_remove(subscription_list, si);
	memb_free(&subscription_memb, si);
}

void
subscription_expire(struct subscription_item *si) {
	si->last_heard = 0;
}

int 
subscription_is_expired(struct subscription_item *si) {
	return (si->last_heard == 0 || si->last_heard + MWARE_SHELFLIFE < clock_seconds());
}	

int
subscription_is_mine(struct subscription_item *si) {
	return rimeaddr_cmp(&si->id.subscriber, &rimeaddr_node_addr);
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
	DPRINTF("[%d]\n", (int16_t) (packetbuf_dataptr() - packetbuf_hdrptr()));
}
/*----------------------------------------------*/
int
message_is_published_to_me(void) {
	return (packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE) == MWARE_MSG_PUB &&
			rimeaddr_cmp(&((struct publish_message *) packetbuf_dataptr())->next_hop,
				&rimeaddr_node_addr));
}
/*-------------------Broadcast Reception---------------------------*/
void
packet_received(struct broadcast_conn *connection, const rimeaddr_t *from)
{
	struct subscription_item *si;
	switch(packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE)) {
		case MWARE_MSG_SUB:
			if ((si = subscription_get(&((struct subscribe_message *) packetbuf_dataptr())->id))) {
				subscription_update(si, (rimeaddr_t *) from,
						((struct subscribe_message *) packetbuf_dataptr())->cost + 1);
			} else {
				si = subscription_insert(&((struct subscribe_message *) packetbuf_dataptr())->id,
						&((struct subscribe_message *) packetbuf_dataptr())->sub,
						(rimeaddr_t *) from,
						((struct subscribe_message *) packetbuf_dataptr())->cost + 1);	
				if (si != NULL) {
					wind_timer(si);
				}	
			}
			subscription_print_table();
			break;
		case MWARE_MSG_PUB:
			si = subscription_get(&((struct publish_message *) packetbuf_dataptr())->id);
			if (si != NULL) {
				//Ignore here.	
			}
			else if (message_is_published_to_me() && !subscription_is_expired(si)) {	
				//TODO: i'm the responsible parent...
			}  
			else if (subscription_is_mine(si)) {
				callback->publish(&si->id, 0); 
				//TODO: I'm the final destination...
			}
			break;
		case MWARE_MSG_UNSUB:
			si = subscription_get(&((struct publish_message *) packetbuf_dataptr())->id);
			if (si != NULL && rimeaddr_cmp(&si->next_hop, &rimeaddr_node_addr)) {
				subscription_expire(si);
			}	
			break;
	}
	subscription_next_hop_refresh(from);	
	packetbuf_clear();
}

static const struct broadcast_callbacks connection_cb = { packet_received };
/*--------------------Subscription Broadcasting --------------------------*/

void
broadcast_subscription(struct subscription_item *si) {
	packetbuf_clear();
	packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, MWARE_MSG_SUB);
	packetbuf_set_datalen(sizeof(struct subscribe_message)); 
	msg = packetbuf_dataptr();
	memcpy(&msg->id, &si->id, sizeof(struct identifier));
	memcpy(&msg->sub, &si->sub, sizeof(struct subscription));
	msg->cost = si->cost; 
	if (broadcast_send(&connection)) {
		si->last_shout = clock_seconds(); 
	}
}

void
broadcast_unsubscription(struct subscription_item *si) {
	packetbuf_clear();
	packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, MWARE_MSG_UNSUB);
	packetbuf_set_datalen(sizeof(struct unsubscribe_message)); 
	msg = packetbuf_dataptr();
	memcpy(&msg->id, &si->id, sizeof(struct identifier));
	broadcast_send(&connection);
}

static void
mware_service_item(void *p) {
	struct subscription_item *si = (struct subscription_item *) p;	
	callback->sense(&si->id, &si->sub); 
	if (subscription_is_expired(si) && broadcast_unsubscription(si)) {
		subscription_remove(si);  
		return;   
	}
	if (si->last_shout + MWARE_BEACON_INTERVAL < clock_seconds()) {
		broadcast_subscription(si); 
	} 
	wind_timer(si);
}

static void wind_timer(struct subscription_item *si) {
	if (ctimer_expired(&si->t)) {
		ctimer_set(&si->t, si->sub.period, mware_service_item, si);
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
		wind_timer(si);	
		broadcast_subscription(si);
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
		subscription_expire(si);
	}
}

void
mware_publish(struct identifier *i, uint16_t v1, uint16_t v2) {
	struct subscription_item *si;
	struct publish_message *msg; 
	si = subscription_get(i);
	if (si == NULL) {
		return;
	}
	packetbuf_clear();
	packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, MWARE_MSG_PUB);
	packetbuf_set_datalen(sizeof(struct publish_message)); 
	msg = packetbuf_dataptr();  
	memcpy(&msg->id, i,sizeof(struct identifier)); 
	rimeaddr_copy(&msg->next_hop, &si->next_hop); 
	msg->v1 = v1;
	msg->v2 = v2; 
	broadcast_send(&connection);
}

void
mware_shutdown(void) {
	broadcast_close(&connection);
}


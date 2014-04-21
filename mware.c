#include "contiki.h"
#include "mware.h"
#include <stddef.h>
#include "net/rime/broadcast.h"
#include <string.h>

static struct broadcast_conn connection;
static const struct mware_callbacks *callback;
static void wind_item_timer(struct subscription_item *si, clock_time_t remaining);
static const struct packetbuf_attrlist attributes[] =
{ MWARE_ATTRIBUTES PACKETBUF_ATTR_LAST };

/*------------Subscription Manager -----------------*/
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
	DPRINTF("%s:%d", title, si->id.subscriber.u8[0]);
	DPRINTF(", i:%d, nh:%d", si->id.id, si->next_hop.u8[0]);
	DPRINTF(", h:%d, e:%d", si->sub.hops, si->sub.epoch);
	DPRINTF(", lh:%d, ls:%d", si->last_heard, si->last_shout);
	DPRINTF(", data:%d, node_count:%d\n", si->data, si->node_count);
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
subscription_update_last_heard(struct subscription_item *si) {
	si->last_heard = clock_seconds();
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
		rimeaddr_t *next_hop, uint8_t hops) {
	if (rimeaddr_cmp(&si->next_hop, next_hop)) {
		if (si->sub.hops != hops) {
			PRINTF("new_hops %d -> %d\n", si->sub.hops, hops);
			si->sub.hops = hops;
			return 1;
		}
	} else if (hops < si->sub.hops) {
		DPRINTF("new_parent ");
		DPRINT2ADDR(&si->next_hop);
		DPRINTF(" -> ");
		DPRINT2ADDR(next_hop);
		DPRINTF("\n");
		rimeaddr_copy(&si->next_hop, next_hop);
		si->sub.hops = hops;
		return 1;
	}
	return 0;
}

struct subscription_item *
subscription_insert(struct identifier *i, struct subscription *s,
		rimeaddr_t *next_hop, uint8_t hops) {
	struct subscription_item *si;
	si = memb_alloc(&subscription_memb);
	if (si == NULL) {
		return NULL;
	}
	memcpy(&si->id,i,sizeof(struct identifier));
	memcpy(&si->sub,s,sizeof(struct subscription));
	rimeaddr_copy(&si->next_hop, next_hop);
	si->sub.hops = hops;
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
	return (si->last_heard + 12*(si->sub.period/CLOCK_SECOND) < clock_seconds());
}

int
subscription_needs_broadcast(struct subscription_item *si) {
	return (si->last_shout == 0 ||
			si->last_shout + 4*(si->sub.period/CLOCK_SECOND) < clock_seconds());
}

void
subscription_data_reset(struct subscription_item *si) {
	si->sub.epoch++;
	si->data = 0;
	si->node_count = 0;
}

void
subscription_data_input(struct subscription_item *si, uint16_t data, uint16_t node_count) {
	if (node_count == 0) {
		return;
	}
	switch (si->sub.aggregation) {
		case MIN:
			if (si->node_count == 0 || data < si->data) {
				si->data = data;
			}
			break;
		case MAX:
			if (si->node_count == 0 || data > si->data) {
				si->data = data;
			}
			break;
		case AVG:
			si->data += data;
			break;
		case COUNT:
			//COUNT just adds the values of node_count (see below)
			break;
	}
	si->node_count += node_count;
}

uint16_t
subscription_data_output(struct subscription_item *si) {
	switch (si->sub.aggregation) {
		case MIN:
		case MAX:
			return si->data;
		case AVG:
			if (si->node_count > 0) {
				return si->data/si->node_count;
			}
		case COUNT:
			return si->node_count;
	}
	return 0;
}

void
subscription_sync_jitter(struct subscription *s, struct subscription_item *si) {
	//Whatever set here will be negated later below.
	s->jitter = timer_remaining(&si->t.etimer.timer)
		+ (si->sub.jitter > 0 ? si->sub.period/2 - si->sub.jitter : 0);
}

clock_time_t
subscription_desync_jitter(struct subscription_item *si, struct subscription *s) {
	//This staggers the winding of the clock by a short slot time.
	si->sub.jitter = 0;
	return s->jitter + (s->slot_size >= s->jitter ? si->sub.period : 0)
			- s->slot_size;

}

void
subscription_sync_epoch(struct subscription_item *si, struct subscription *s) {
	//This is +1 because the sync jitter will only begin one period later
	//(see above).
	si->sub.epoch = s->epoch + (s->slot_size >= s->jitter ? 1 : 0);
}

int
identifier_is_mine(struct identifier *i) {
	return rimeaddr_cmp(&i->subscriber, &rimeaddr_node_addr);
}
/*--------------------Broadcast Generator---------------------------*/
void
broadcast_subscription(struct subscription_item *si) {
	struct subscribe_message *msg;
	packetbuf_clear();
	packetbuf_set_attr(PACKETBUF_ATTR_PACKET_TYPE, MWARE_MSG_SUB);
	packetbuf_set_datalen(sizeof(struct subscribe_message));
	msg = packetbuf_dataptr();
	memcpy(&msg->id, &si->id, sizeof(struct identifier));
	memcpy(&msg->sub, &si->sub, sizeof(struct subscription));
	subscription_sync_jitter(&msg->sub,si);
	if (broadcast_send(&connection)) {
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
	msg->data = si->data;
	msg->node_count = si->node_count;
	if (broadcast_send(&connection)) {
		subscription_print(si, "bp");
	}
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
		subscription_update_last_shout(si);
	}
}
/*-------------------Broadcast Handler---------------------------*/
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

int
subscription_is_too_far(struct subscription *s) {
	return (s->period < (s->hops + 1) * s->slot_size);
}

void
packet_received(struct broadcast_conn *connection, const rimeaddr_t *from)
{
	struct subscription_item *si;
	switch(packetbuf_attr(PACKETBUF_ATTR_PACKET_TYPE)) {
	case MWARE_MSG_SUB:
		if (subscription_is_too_far(&(packetbuf_msg_sub())->sub)) {
		break;
		}
		si = subscription_get(&(packetbuf_msg_sub())->id);
		if (si != NULL) {
			if (subscription_is_unsubscribed(si)) {
				subscription_reset_last_shout(si);
			} else {
				subscription_update_last_heard(si);
				if (!subscription_update(si, (rimeaddr_t *) from,
							(packetbuf_msg_sub())->sub.hops + 1)) {
					si = NULL;

				}
			}
		} else  {
			si = subscription_insert(&(packetbuf_msg_sub())->id,
					&(packetbuf_msg_sub())->sub,
					( identifier_is_mine(&(packetbuf_msg_sub())->id) ?
					  &rimeaddr_null : (rimeaddr_t *) from),
					(packetbuf_msg_sub())->sub.hops + 1);
		}
		if (si != NULL) {
			subscription_print(si, "rs");
			subscription_sync_epoch(si, &(packetbuf_msg_sub())->sub);
			wind_item_timer(si, subscription_desync_jitter(si, &(packetbuf_msg_sub())->sub));
		}
	break;
	case MWARE_MSG_PUB:
		si = subscription_get(&(packetbuf_msg_pub())->id);
		if (si == NULL || subscription_is_unsubscribed(si)) {
			break;
		}
		if (message_is_published_to_me()) {
			subscription_data_input(si,
					(packetbuf_msg_pub())->data,
					(packetbuf_msg_pub())->node_count);
		}
		subscription_update_last_heard(si);
		break;
	case MWARE_MSG_UNSUB:
		si = subscription_get(&(packetbuf_msg_unsub())->id);
		if (si == NULL) {
			break;
		}
		subscription_print(si, "ru");
		subscription_unsubscribe(si);
		break;
	}
	packetbuf_clear();
}

static const struct broadcast_callbacks connection_cb = { packet_received };

/*--------------------Subscription Service Function --------------------------*/
static void
mware_service_item(void *p) {
	struct subscription_item *si = (struct subscription_item *) p;
	if (subscription_is_stale(si) &&
			(!identifier_is_mine(&si->id) || subscription_is_unsubscribed(si))) {
		subscription_remove(si);
		return;
	}
	//NOTE: No jitter means ready to sense, else ready to publish
	if (subscription_get_jitter(&si->sub) > 0) {
		wind_item_timer(si, si->sub.period/2 - subscription_get_jitter(&si->sub));
		subscription_set_jitter(&si->sub,0);

		if (subscription_is_unsubscribed(si)) {
			if (subscription_needs_broadcast(si)) {
				broadcast_unsubscription(si);
			}
		} else {
			if (identifier_is_mine(&si->id)) {
				callback->publish(&si->id,&si->sub,
						subscription_data_output(si));
			}
			broadcast_publication(si);
			subscription_data_reset(si);
			if (subscription_needs_broadcast(si)) {
				broadcast_subscription(si);
			}
		}
	} else {
		wind_item_timer(si, si->sub.period/2 +
			subscription_set_jitter(&si->sub, FULL_JITTER(si->sub.slot_size/2 - 1) + 1));

		if (!subscription_is_unsubscribed(si)) {
			callback->sense(&si->id, &si->sub);
		}
	}
}

static void wind_item_timer(struct subscription_item *si, clock_time_t offset) {
	ctimer_set(&si->t, offset,
		mware_service_item, si);
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
		wind_item_timer(si, HALF_JITTER(2*CLOCK_SECOND));
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
mware_publish(struct identifier *i, uint16_t data, uint16_t node_count) {
	struct subscription_item *si;
	si = subscription_get(i);
	if (si == NULL || subscription_is_unsubscribed(si)) {
		return;
	}
	subscription_data_input(si, data, node_count);
}

void
mware_shutdown(void) {
	broadcast_close(&connection);
	subscription_clean();
}

/* vim: set ts=8 sw=8 tw=80 noet :*/

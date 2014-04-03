#include "contiki.h"
#include "mware.h"
#include <stddef.h>
#include "net/rime/broadcast.h"
#include <string.h>

static struct broadcast_conn connection;
static timer_t subscription_timer;
/*------------Subscription Manager-----------------*/
LIST(subscription_list);
MEMB(subscription_memb, struct subscription_item, MWARE_SIZE);
void
subscription_init() {
	list_init(subscription_list);
	memb_init(subscription_memb);
}
struct subscription_item *
subscription_get(struct identifier * i) {
	struct subscription_item *si;
	for (si = list_head(subscription_list); si != NULL; si = list_item_next(it)) {
		if (memcmp(i,si->id,sizeof(struct identifier)) == 0) {
			return si;
		}
	}
	return NULL; 
}
struct subscription_item * 
subscription_insert(struct identifier *i, struct subscription *s,
		rimeaddr_t *parent, uint8_t cost) {
	struct subscription_item *si;
	if (si = subscription_get(i)) {
		if (si->cost < cost) {
			rimeaddr_copy(&si->parent, parent);
			si->cost = cost;	
		} else if (rimeaddr_cmp(&si->parent, parent) == 0) {
			si->cost = cost;
		}
		
	}
	else {
		si = memb_alloc(subscription_memb);
		if (si == NULL) {
			return NULL; 
		}
		memcpy(&si->id,i,sizeof(struct identifier));
		memcpy(&si->sub,s,sizeof(struct subscription));
		rimeaddr_copy(&si->parent, parent);
		si->cost = cost;	
		list_add(subscription_list, si);
	}
	return si;
}
void
subscription_remove(struct identifier *i) {
	struct subscription_item *si = subscription_get(i);
	if (si == NULL) {
		return;
	}
	list_remove(subscription_list, si);
	memb_free(subscription_memb, si);

}
/*----------------------------------------------*/
	void
packetbuf_prepend_hdr(void * ptr, size_t size)
{
	packetbuf_hdralloc(size);
	memcpy(packetbuf_hdrptr(),ptr, size);
}
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
void
packet_received(struct broadcast_conn *connection, const rimeaddr_t *from)
{
	print_raw_packetbuf();
	struct msg_header *hdr = packetbuf_dataptr();
	struct identifier edition;
	struct subscription *s;
	struct manuscript *m;
	PRINT2ADDR(&hdr->edition.subscriber);	
	PRINTF("ID: %d ",hdr->edition.id);
	packetbuf_hdrreduce(sizeof(struct msg_header));
	switch(hdr->message_type) {
	case SUBSCRIBE:
		s = packetbuf_dataptr();
			
		break;
	case PUBLISH:
		break;
	case UNSUBSCRIBE:
		break;
	}
	packetbuf_clear();
}
static const struct broadcast_callbacks connection_cb = { packet_received };

static void wind_timer(void);
static void
mware_service_subscriptions(void *p) {
	wind_timer();
}
static void wind_timer(void) {
	if (ctimer_expired(&subscription_timer)) {
		ctimer_set(&subscription_timer, CLOCK_SECOND, mware_service_subscriptions, NULL);
	}
}	
/*----------------------------------------------*/
void
mware_bootstrap(uint16_t channel) {
	broadcast_open(&connection, channel, &connection_cb);
	wind_timer();
}

void 
mware_subscribe(uint8_t id, struct subscription *s) {
	struct msg_header hdr = { .message_type = SUBSCRIBE,
				  .hops = 0,
       				.edition = { .id = id }	};
	rimeaddr_copy(&hdr.edition.subscriber,&rimeaddr_node_addr);
	packetbuf_clear();
	packetbuf_prepend_hdr(s, sizeof(struct subscription));
	packetbuf_prepend_hdr(&hdr, sizeof(struct msg_header));
	broadcast_send(&connection);
}

void
mware_unsubscribe(uint8_t id) {
	struct msg_header hdr = { .message_type = UNSUBSCRIBE,
				.hops = 0,
       				.edition = { .id = id }	};
	rimeaddr_copy(&hdr.edition.subscriber, &rimeaddr_node_addr);
	packetbuf_clear();
	packetbuf_prepend_hdr(&hdr, sizeof(struct msg_header));
	broadcast_send(&connection);
}

void
mware_publish(struct identifier *i, struct manuscript *m) {
	struct msg_header hdr = { .message_type = PUBLISH, .hops = 0 };
	struct subscription_item *si;
	si = subscription_get(i);
	if (si == NULL) {
		return;
	}
	memcpy(&hdr.edition, i, sizeof(struct identifier));
	packetbuf_clear();
	m->timestamp = clock_seconds();
	rimeaddr_copy(&m->next_hop,&si->parent); 
	packetbuf_prepend_hdr(m, sizeof(struct manuscript));
	packetbuf_prepend_hdr(&hdr, sizeof(struct msg_header));
	broadcast_send(&connection);
}

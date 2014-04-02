#include "contiki.h"
#include "mware.h"
#include <stddef.h>
#include "net/rime/broadcast.h"
#include <string.h>

static struct broadcast_conn connection;
LIST(subscription_list);
MEMB(subscription_memb, struct sub_item, MWARE_SIZE);
  void
subscription_init() {
 list_init(subscription_list);
 memb_init(subscription_memb);
}
  struct sub_item *
subscription_get(struct identifier * i) {
  struct sub_item *it;
  for (it = list_head(subscription_list); it != NULL; it = list_item_next(it)) {
    if (memcmp(i,it->id,sizeof(struct identifier)) != 0) {
      return it;
    }
  }
  return NULL; 
}
  int 
subscription_insert(struct sub_item *s) {
  struct sub_item *i;
  if (subscription_get(&(s->id))) {

  }
  else {
    i = memb_alloc(subscription_memb);
    if (i == NULL) {
      return 0; 
    }
    memcpy(i,s,sizeof(struct sub_item));
    list_add(subscription_list, i);
  }
}
  void
subscription_remove(struct identifier *i) {
  struct sub_item *it = subscription_get(i);
  if (it == NULL) {
    return;
  }
  list_remove(subscription_list, it);
  memb_free(subscription_memb, it);

}

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
  void
process_subscribe() {
    print_raw_packetbuf();
    struct subscription *s;
    s = packetbuf_dataptr();
    switch (s->type) {
      case LIGHT:
        PRINTF("LIGHT ");
        break;
      case ACCELEROMETER:
        PRINTF("ACCELEROMETER ");
        break;
      case MAGNETOMETER:
        PRINTF("MAGNETOMETER ");
        break;
    }
    switch (s->aggregation) {
      case AVG:
        PRINTF("AVG ");
        break;
      case MIN:
        PRINTF("MIN ");
        break;
      case MAX:
        PRINTF("MAX ");
        break;
    }
    PRINTF("\n");
}
  void
packet_received(struct broadcast_conn *connection, const rimeaddr_t *from)
{
  print_raw_packetbuf();
  struct msg_header *hdr = packetbuf_dataptr();
  struct identifier rid;
  switch(hdr->message_type) {
    case SUBSCRIBE:
      PRINTF("SUBSCRIBE ");
      memcpy(&rid, &(hdr->sid),sizeof(struct identifier));
      PRINTF("ID: %d ",rid.id);
      packetbuf_hdrreduce(sizeof(struct msg_header));
      process_subscribe();
      break;
    case PUBLISH:
      PRINTF("Publish Message Received\n");
      break;
    case UNSUBSCRIBE:
      PRINTF("UnSub received\n");
      break;
  }
  packetbuf_clear();
}

static const struct broadcast_callbacks connection_cb = { packet_received };

  void
mware_bootstrap(uint16_t channel) {
  broadcast_open(&connection, channel, &connection_cb);

}

  void 
mware_subscribe(uint8_t id, struct subscription *s) {
  struct msg_header hdr = { .message_type = SUBSCRIBE};
  rimeaddr_copy(&hdr.sid.subscriber,&rimeaddr_node_addr);
  hdr.sid.id = id;
  packetbuf_clear();
  packetbuf_prepend_hdr(s, sizeof(struct subscription));
  packetbuf_prepend_hdr(&hdr, sizeof(struct msg_header));
  print_raw_packetbuf();
  broadcast_send(&connection);
  print_raw_packetbuf();
}

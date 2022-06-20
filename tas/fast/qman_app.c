#include <stddef.h>
#include <stdint.h>

#include "internal.h"


int appcont_init(struct qman_thread *t)
{
  int ret;
  unsigned i;
  struct app_queue *aq;
  t->a_cont = malloc(sizeof(struct app_cont));
  struct app_cont *ac = t->a_cont;

  ac->queues = calloc(1, sizeof(*ac->queues) * FLEXNIC_NUM_QMAPPQUEUES);
  if (ac->queues == NULL)
  {
    fprintf(stderr, "appcont_init: queues malloc failed\n");
    return -1;
  }

  for (i = 0; i < FLEXNIC_NUM_QMAPPQUEUES; i++)
  {
    aq = &ac->queues[i];
    ret = flowcont_init(aq);

    if (ret != 0)
    {
      return -1;
    }
  }

  ac->head_idx = ac->tail_idx = IDXLIST_INVAL;
  
  return 0;
}

int app_qman_poll(struct qman_thread *t, struct app_cont *ac, unsigned num, 
    unsigned *app_id, unsigned *q_ids, uint16_t *q_bytes)
{
  int ret;

  uint32_t idx = ac->head_idx;

  if (idx == IDXLIST_INVAL)
  {
    return 0;
  }

  *app_id = idx;
  struct app_queue *aq = &ac->queues[idx];
  struct flow_cont *fc = aq->f_cont;
  

  ret = flow_qman_poll(t, fc, num, q_ids, q_bytes);

  return ret;
}

int app_qman_set(struct qman_thread *t, uint32_t app_id, uint32_t flow_id, uint32_t rate, uint32_t avail,
    uint16_t max_chunk, uint8_t flags)
{
  int ret;

  struct app_cont *ac = t->a_cont;
  struct app_queue *aq = &ac->queues[app_id];
  struct flow_cont *fc = aq->f_cont;

  if (app_id >= FLEXNIC_NUM_QMAPPQUEUES) {
    fprintf(stderr, "app_qman_set: invalid queue id: %u >= %u\n", app_id,
        FLEXNIC_NUM_QMAPPQUEUES);
    return -1;
  }

  ret = flow_qman_set(t, fc, flow_id, rate, avail, max_chunk, flags);
  return ret;
}

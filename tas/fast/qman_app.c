#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "internal.h"

// TODO: Remove apps that have no more packets to send from list
// TODO: Retrieve packets from more than one app with each call to qman_poll
// TODO: Add rate to app containers
// TODO: Make sure flow_id makes sense when distributed among different apps.
// find way to translate absolute flow_id to an id in the application queue.

static inline void app_set_impl(struct app_cont *ac, uint32_t a_idx,
    uint32_t f_idx, uint32_t avail, uint8_t flags);
static inline void app_queue_activate(struct app_cont *ac,
    struct app_queue *q, uint32_t idx);

int appcont_init(struct qman_thread *t)
{
  int ret;
  unsigned i;
  struct app_queue *aq;
  t->a_cont = malloc(sizeof(struct app_cont));
  struct app_cont *ac = t->a_cont;

  ac->queues = calloc(1, sizeof(*ac->queues) * FLEXNIC_PL_APPST_NUM);
  if (ac->queues == NULL)
  {
    fprintf(stderr, "appcont_init: queues malloc failed\n");
    return -1;
  }

  for (i = 0; i < FLEXNIC_PL_APPST_NUM; i++)
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

  ac->head_idx = aq->next_idx;
  if (aq->next_idx == IDXLIST_INVAL)
  {
    ac->tail_idx = IDXLIST_INVAL;
  }

  aq->flags &= ~FLAG_INNOLIMITL;
  ret = flow_qman_poll(t, fc, num, q_ids, q_bytes);

  return ret;
}

int app_qman_set(struct qman_thread *t, uint32_t app_id, uint32_t flow_id, 
    uint32_t rate, uint32_t avail, uint16_t max_chunk, uint8_t flags)
{
  int ret;
  struct app_cont *ac = t->a_cont;
  struct app_queue *aq = &ac->queues[app_id];
  struct flow_cont *fc = aq->f_cont;

  if (app_id >= FLEXNIC_PL_APPST_NUM) 
  {
    fprintf(stderr, "app_qman_set: invalid queue id: %u >= %u\n", app_id,
        FLEXNIC_PL_APPST_NUM);
    return -1;
  }

  ret = flow_qman_set(t, fc, flow_id, rate, avail, max_chunk, flags);

  app_set_impl(ac, app_id, flow_id, avail, flags);
  return ret;
  // if (ac->tail_idx == IDXLIST_INVAL)
  // {
  //   ac->head_idx = ac->tail_idx = app_id;
  //   return ret;
  // }

  // a_tail = &ac->queues[ac->tail_idx];
  // a_tail->next_idx = app_id;
  // ac->tail_idx = app_id;
  // return ret;
}

static inline void app_set_impl(struct app_cont *ac, uint32_t a_idx,
    uint32_t f_idx, uint32_t avail, uint8_t flags)
{
  struct app_queue *aq = &ac->queues[a_idx];
  struct flow_cont *fc = aq->f_cont;
  struct flow_queue *fq = &fc->queues[f_idx];

  int new_avail = 0;

  if ((flags & QMAN_SET_AVAIL) != 0)
  {
      new_avail = 1;
      int prev_avail = fq->avail;
      aq->avail -= prev_avail;
      aq->avail += avail;
  }

  if ((flags & QMAN_ADD_AVAIL) != 0)
  {
      aq->avail += avail;
      new_avail = 1;
  }

  if (new_avail && aq->avail > 0 && ((aq->flags & (FLAG_INNOLIMITL)) == 0)) 
  {
    app_queue_activate(ac, aq, a_idx);
  }

}

static inline void app_queue_activate(struct app_cont *ac,
    struct app_queue *q, uint32_t idx)
{
  struct app_queue *q_tail;

  assert((q->flags & FLAG_INNOLIMITL) == 0);

  q->flags |= FLAG_INNOLIMITL;
  q->next_idx = IDXLIST_INVAL;
  if (ac->tail_idx == IDXLIST_INVAL)
  {
    ac->head_idx = ac->tail_idx = idx;
    return;
  }

  q_tail = &ac->queues[ac->tail_idx];
  q_tail->next_idx = idx;
  ac->tail_idx = idx;
}
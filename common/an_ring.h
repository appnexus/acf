#ifndef _AN_RING_H
#define _AN_RING_H

#include <ck_ring.h>

/*
 * Prepare to enqueue.
 *
 * The data to enqueue will be stored at the returned pointer
 * for up to `size' bytes. Caller should modify this space as
 * they see fit before calling commit(). If commit is not called,
 * the enqueue is implicitly aborted.
 */
inline static void *
an_ring_enqueue_spsc_prepare(struct ck_ring *ring, void *restrict buffer,
    unsigned int size)
{
	unsigned int consumer, producer, delta;
	unsigned int mask = ring->mask;

	consumer = ck_pr_load_uint(&ring->c_head);
	producer = ring->p_tail;
	delta = producer + 1;

	if ((delta & mask) == (consumer & mask)) {
		return NULL;
	}

	buffer = (char *)buffer + size * (producer & mask);
	return buffer;
}

/*
 * Commit the last prepared enqueue.
 */
inline static void
an_ring_enqueue_spsc_commit(struct ck_ring *ring)
{
	unsigned int producer, delta;

	producer = ring->p_tail;
	delta = producer + 1;

	/*
	 * Make sure to update slot value before indicating
	 * that the slot is available for consumption.
	 */
	ck_pr_fence_store();
	ck_pr_store_uint(&ring->p_tail, delta);
}

/*
 * Prepare to dequeue an object.
 *
 * Return a pointer to the beginning of the object.
 */
inline static void *
an_ring_dequeue_spsc_prepare(struct ck_ring *ring, void *restrict buffer,
    unsigned int size)
{
	unsigned int consumer, producer;
	unsigned int mask = ring->mask;

	consumer = ring->c_head;
	producer = ck_pr_load_uint(&ring->p_tail);

	if (consumer == producer) {
		return NULL;
	}

	/*
	 * Make sure to serialize with respect to our snapshot
	 * of the producer counter.
	 */
	ck_pr_fence_load();

	buffer = (char *)buffer + size * (consumer & mask);
	return buffer;
}

/*
 * Commit the last prepared dequeue.
 */
inline static void
an_ring_dequeue_spsc_commit(struct ck_ring *ring)
{
	unsigned int consumer;

	consumer = ring->c_head;

	/*
	 * Make sure copy is completed with respect to consumer
	 * update.
	 */
	ck_pr_fence_store();
	ck_pr_store_uint(&ring->c_head, consumer + 1);
}

#endif /* _AN_RING_H */

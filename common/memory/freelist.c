#include <ck_cc.h>
#include <ck_pr.h>
#include <stdbool.h>

#include "common/memory/freelist.h"
#include "common/rtbr/rtbr.h"

CK_STACK_CONTAINER(struct an_freelist_entry, stack_entry,
    freelist_entry_of_stack_entry);

/*
 * See ck_fifo_mpmc_trydequeue
 */
CK_CC_INLINE static bool
ck_fifo_mpmc_maybe_dequeue(struct ck_fifo_mpmc *fifo,
    void *value,
    struct ck_fifo_mpmc_entry **garbage,
    void *(*pred)(void *value, void *state),
    void *state)
{
        struct ck_fifo_mpmc_pointer head, tail, next, update;
	void *value_tmp;

        head.generation = ck_pr_load_ptr(&fifo->head.generation);
        ck_pr_fence_load();
        head.pointer = ck_pr_load_ptr(&fifo->head.pointer);

        tail.generation = ck_pr_load_ptr(&fifo->tail.generation);
        ck_pr_fence_load();
        tail.pointer = ck_pr_load_ptr(&fifo->tail.pointer);

        next.generation = ck_pr_load_ptr(&head.pointer->next.generation);
        ck_pr_fence_load();
        next.pointer = ck_pr_load_ptr(&head.pointer->next.pointer);

        update.pointer = next.pointer;

	*(void **)value = NULL;
	if (head.pointer == tail.pointer) {
                if (next.pointer == NULL) {
                        return false;
		}

                /* Forward the tail pointer if necessary. */
                update.generation = tail.generation + 1;
                ck_pr_cas_ptr_2(&fifo->tail, &tail, &update);
                return false;
	}

	if (next.pointer == NULL) {
		return false;
	}

	/* ***only change is here.*** */
	value_tmp = ck_pr_load_ptr(&next.pointer->value);
	if (pred != NULL) {
		value_tmp = pred(value_tmp, state);
		if (value_tmp == NULL) {
			return false;
		}
	}

	*(void **)value = value_tmp;

	/* Forward the head pointer to the next entry. */
	update.generation = head.generation + 1;
	if (ck_pr_cas_ptr_2(&fifo->head, &head, &update) == false) {
		*(void **)value = NULL;
		return false;
        }

        *garbage = head.pointer;
        return true;
}

struct an_freelist_entry *
an_freelist_register(struct an_freelist *freelist)
{
	uint64_t n_elem = ck_pr_load_64(&freelist->n_elem);
	uint64_t used_elem = ck_pr_load_64(&freelist->used_elem);

	if (used_elem >= n_elem) {
		return NULL;
	}

	used_elem = ck_pr_faa_64(&freelist->used_elem, 1);
	if (used_elem >= n_elem) {
		return NULL;
	}

	return &freelist->entries[used_elem];
}

static void *
an_freelist_pop_predicate(void *value, void *arg)
{
	struct an_freelist_entry *entry = value;

	(void)arg;
	if (ck_pr_load_64(&entry->deletion_timestamp) > an_rtbr_epoch()) {
		return NULL;
	}

	return ck_pr_load_ptr(&entry->value);
}

/*
 * Incrementally age values from the limbo FIFO to the reuse stack.
 *
 * We cap the number of iterations to 3 to bound pauses.  That
 * iteration count (3) is also greater than the number of items we
 * push on the FIFO (at most 1), so we will quickly catch up.
 */
static void *
an_freelist_manage(struct an_freelist *freelist, struct an_freelist_entry **OUT_entry)
{
	struct an_freelist_entry *entry = NULL;

	for (size_t i = 0; i < 3; i++) {
		void *value;
		struct ck_fifo_mpmc_entry *garbage;

		if (ck_fifo_mpmc_maybe_dequeue(&freelist->fifo,
		    &value, &garbage,
		    an_freelist_pop_predicate, NULL) == false) {
			break;
		}

		if (entry != NULL) {
			ck_stack_push_mpmc(&freelist->stack, &entry->stack_entry);
		}

		entry = (void *)garbage;
		entry->value = value;
	}

	if (entry == NULL) {
		return NULL;

	}

	if (OUT_entry == NULL) {
		ck_stack_push_mpmc(&freelist->stack, &entry->stack_entry);
		return NULL;
	}

	*OUT_entry = entry;
	return entry->value;
}

void *
an_freelist_pop(struct an_freelist *freelist,
    struct an_freelist_entry **OUT_entry)
{
	if (ck_pr_load_ptr(&freelist->stack.head) != NULL) {
		struct ck_stack_entry *stack_entry;

		stack_entry = ck_stack_pop_mpmc(&freelist->stack);
		if (stack_entry != NULL) {
			struct an_freelist_entry *entry;

			entry = freelist_entry_of_stack_entry(stack_entry);
			*OUT_entry = entry;
			return entry->value;
		}
	}

	return an_freelist_manage(freelist, OUT_entry);
}

void
an_freelist_shelve(struct an_freelist *freelist,
    struct an_freelist_entry *entry, void *value)
{

	entry->value = value;
	entry->deletion_timestamp = an_rtbr_prepare().timestamp;

	an_freelist_manage(freelist, NULL);
	ck_fifo_mpmc_enqueue(&freelist->fifo, &entry->fifo_entry,
	    entry);
	return;
}

void
an_freelist_push(struct an_freelist *freelist,
    struct an_freelist_entry *entry, void *value)
{

	entry->value = value;
	ck_stack_push_mpmc(&freelist->stack, &entry->stack_entry);
	return;
}

#ifndef AN_BUF_H_
#define AN_BUF_H_

#include <unistd.h>
#include <stdbool.h>

/*
 * An abstract buffer API.
 *
 * The backend specific headers (an_buf_<backend>.h) contain the
 * functions required to create an an_[rw]buf object. Consumers should
 * include the backend specific header they need instead of this one.
 */

struct an_rbuf;
struct an_wbuf;

/*
 * We use transparent unions so that any function that can act on both
 * read and write buffers can be called with a pointer to either type
 * of buffer without needing to cast.
 *
 * You can think of a function having an an_buf_ptr_t parameter as a
 * function that accepts either a struct an_rbuf * or a struct an_wbuf *.
 * Similarly, an an_buf_const_ptr_t parameter is equivalent to a function
 * accepting either a const struct an_rbuf * or a const struct an_wbuf *.
 */
typedef union {
	struct an_rbuf *rbuf;
	struct an_wbuf *wbuf;
} an_buf_ptr_t __attribute__((__transparent_union__));

typedef union {
	struct an_rbuf *rbuf;
	struct an_wbuf *wbuf;
	const struct an_rbuf *const_rbuf;
	const struct an_wbuf *const_wbuf;
} an_buf_const_ptr_t __attribute__((__transparent_union__));

typedef void (an_buf_cleanup_t)(an_buf_ptr_t, void *);


/* API for both read and write buffers. */

/*
 * Returns true if the underlying buffer is "owned" or false.
 *
 * When the underlying buffer is owned, it is destroyed when the parent
 * an_buf object is destroyed using an_buf_destroy(). If it isn't owned,
 * it will persist and remain usable after an_buf_destroy() is called.
 */
bool		 an_buf_owned(an_buf_const_ptr_t);
/* Mark the underlying buffer as owned. */
void		 an_buf_own(an_buf_ptr_t);
/* Mark the underlying buffer as *not* owned. */
void		 an_buf_disown(an_buf_ptr_t);
/* Destroy an an_buf object (and the underlying buffer if owned). */
void		 an_buf_destroy(an_buf_ptr_t);
/* Return the total length of the buffer. */
size_t		 an_buf_length(an_buf_const_ptr_t);
/* Add a cleanup function to be called at destruction time. */
void		 an_buf_add_cleanup(an_buf_ptr_t, an_buf_cleanup_t *, void *);


/* API for write buffers. */

/* Append data to a buffer. */
void		 an_buf_add(struct an_wbuf *, const void *, size_t);
/* Append a printf() formatted string to the buffer. */
void		 an_buf_add_printf(struct an_wbuf *, const char *, ...)
		     __attribute__((format(printf, 2, 3)));
/* Empty the buffer. */
void		 an_buf_reset(struct an_wbuf *);
/*
 * Mark this buffer as frozen. Any subsequent writes to this buffer
 * will cause an asertion failure. Mostly useful for debugging.
 */
void		 an_buf_freeze(struct an_wbuf *);
/* Allow writes on this buffer again. */
void		 an_buf_thaw(struct an_wbuf *);


/* API for read buffers. */

/*
 * Linearize the entire buffer chain in memory.
 *
 * This is a destructive operation: if the buffer hasn't been modified
 * since the last call, subsequent calls will be mostly free. The zone
 * of memory we return a pointer to is owned by the an_buf object.
 */
const void	*an_buf_linearize(struct an_rbuf *);

#endif /* AN_BUF_H_ */

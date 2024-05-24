/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

struct tls;

#define BIO_CHUNK	128
struct buf {
	uint8_t		*buf;
	size_t		 len;
	size_t		 cap;
	size_t		 cur;
};

struct bufio {
	int		 fd;
	struct tls	*ctx;
	int		 wantev;
	struct buf	 wbuf;
	struct buf	 rbuf;
};

#define	BUFIO_WANT_READ		0x1
#define	BUFIO_WANT_WRITE	0x2

int		 buf_init(struct buf *);
int		 buf_has_line(struct buf *, const char *);
char		*buf_getdelim(struct buf *, const char *, size_t *);
void		 buf_drain(struct buf *, size_t);
void		 buf_drain_line(struct buf *, const char *);
void		 buf_free(struct buf *);

int		 bufio_init(struct bufio *);
void		 bufio_free(struct bufio *);
int		 bufio_close(struct bufio *);
int		 bufio_reset(struct bufio *);
void		 bufio_set_fd(struct bufio *, int);
int		 bufio_starttls(struct bufio *, const char *, int,
		    const uint8_t *, size_t, const uint8_t *, size_t);
int		 bufio_ev(struct bufio *);
int		 bufio_handshake(struct bufio *);
ssize_t		 bufio_read(struct bufio *);
size_t		 bufio_drain(struct bufio *, void *, size_t);
ssize_t		 bufio_write(struct bufio *);
const char	*bufio_io_err(struct bufio *);
int		 bufio_compose(struct bufio *, const void *, size_t);
int		 bufio_compose_str(struct bufio *, const char *);
int		 bufio_compose_fmt(struct bufio *, const char *, ...)
		    __attribute__((__format__ (printf, 2, 3)));
void		 bufio_rewind_cursor(struct bufio *);

/* callbacks for pdjson */
int		 bufio_get_cb(void *);
int		 bufio_peek_cb(void *);

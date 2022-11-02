cat << EOF
.Dd ${MAN_DATE}
.Dt GRAPHEME_DECODE_UTF8 3
.Os suckless.org
.Sh NAME
.Nm grapheme_decode_utf8
.Nd decode first codepoint in UTF-8-encoded string
.Sh SYNOPSIS
.In grapheme.h
.Ft size_t
.Fn grapheme_decode_utf8 "const char *str" "size_t len" "uint_least32_t *cp"
.Sh DESCRIPTION
The
.Fn grapheme_decode_utf8
function decodes the first codepoint in the UTF-8-encoded string
.Va str
of length
.Va len .
If the UTF-8-sequence is invalid (overlong encoding, unexpected byte,
string ends unexpectedly, empty string, etc.) the decoding is stopped
at the last processed byte and the decoded codepoint set to
.Dv GRAPHEME_INVALID_CODEPOINT .
.Pp
If
.Va cp
is not
.Dv NULL
the decoded codepoint is stored in the memory pointed to by
.Va cp .
.Pp
Given NUL has a unique 1 byte representation, it is safe to operate on
NUL-terminated strings by setting
.Va len
to
.Dv SIZE_MAX
(stdint.h is already included by grapheme.h) and terminating when
.Va cp
is 0 (see
.Sx EXAMPLES
for an example).
.Sh RETURN VALUES
The
.Fn grapheme_decode_utf8
function returns the number of processed bytes and 0 if
.Va str
is
.Dv NULL
or
.Va len
is 0.
If the string ends unexpectedly in a multibyte sequence, the desired
length (that is larger than
.Va len )
is returned.
.Sh EXAMPLES
.Bd -literal
/* cc (-static) -o example example.c -lgrapheme */
#include <grapheme.h>
#include <inttypes.h>
#include <stdio.h>

void
print_cps(const char *str, size_t len)
{
	size_t ret, off;
	uint_least32_t cp;

	for (off = 0; off < len; off += ret) {
		if ((ret = grapheme_decode_utf8(str + off,
		                                len - off, &cp)) > (len - off)) {
			/*
			 * string ended unexpectedly in the middle of a
			 * multibyte sequence and we have the choice
			 * here to possibly expand str by ret - len + off
			 * bytes to get a full sequence, but we just
			 * bail out in this case.
			 */
			break;
		}
		printf("%"PRIxLEAST32"\\\\n", cp);
	}
}

void
print_cps_nul_terminated(const char *str)
{
	size_t ret, off;
	uint_least32_t cp;

	for (off = 0; (ret = grapheme_decode_utf8(str + off,
	                                          SIZE_MAX, &cp)) > 0 &&
	     cp != 0; off += ret) {
		printf("%"PRIxLEAST32"\\\\n", cp);
	}
}
.Ed
.Sh SEE ALSO
.Xr grapheme_encode_utf8 3 ,
.Xr libgrapheme 7
.Sh AUTHORS
.An Laslo Hunhold Aq Mt dev@frign.de
EOF

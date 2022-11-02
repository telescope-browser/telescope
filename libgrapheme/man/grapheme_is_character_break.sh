cat << EOF
.Dd ${MAN_DATE}
.Dt GRAPHEME_IS_CHARACTER_BREAK 3
.Os suckless.org
.Sh NAME
.Nm grapheme_is_character_break
.Nd test for a grapheme cluster break between two codepoints
.Sh SYNOPSIS
.In grapheme.h
.Ft size_t
.Fn grapheme_is_character_break "uint_least32_t cp1" "uint_least32_t cp2" "uint_least16_t *state"
.Sh DESCRIPTION
The
.Fn grapheme_is_character_break
function determines if there is a grapheme cluster break (see
.Xr libgrapheme 7 )
between the two codepoints
.Va cp1
and
.Va cp2 .
By specification this decision depends on a
.Va state
that can at most be completely reset after detecting a break and must
be reset every time one deviates from sequential processing.
.Pp
If
.Va state
is
.Dv NULL
.Fn grapheme_is_character_break
behaves as if it was called with a fully reset state.
.Sh RETURN VALUES
The
.Fn grapheme_is_character_break
function returns
.Va true
if there is a grapheme cluster break between the codepoints
.Va cp1
and
.Va cp2
and
.Va false
if there is not.
.Sh EXAMPLES
.Bd -literal
/* cc (-static) -o example example.c -lgrapheme */
#include <grapheme.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
	uint_least16_t state = 0;
	uint_least32_t s1[] = ..., s2[] = ...; /* two input arrays */
	size_t i;

	for (i = 0; i + 1 < sizeof(s1) / sizeof(*s1); i++) {
		if (grapheme_is_character_break(s[i], s[i + 1], &state)) {
			printf("break in s1 at offset %zu\n", i);
		}
	}
	memset(&state, 0, sizeof(state)); /* reset state */
	for (i = 0; i + 1 < sizeof(s2) / sizeof(*s2); i++) {
		if (grapheme_is_character_break(s[i], s[i + 1], &state)) {
			printf("break in s2 at offset %zu\n", i);
		}
	}

	return 0;
}
.Ed
.Sh SEE ALSO
.Xr grapheme_next_character_break 3 ,
.Xr grapheme_next_character_break_utf8 3 ,
.Xr libgrapheme 7
.Sh STANDARDS
.Fn grapheme_is_character_break
is compliant with the Unicode ${UNICODE_VERSION} specification.
.Sh AUTHORS
.An Laslo Hunhold Aq Mt dev@frign.de
EOF

if [ "$ENCODING" = "utf8" ]; then
	UNIT="byte"
	ARRAYTYPE="UTF-8-encoded string"
	SUFFIX="_utf8"
	ANTISUFFIX=""
	DATATYPE="char"
else
	UNIT="codepoint"
	ARRAYTYPE="codepoint array"
	SUFFIX=""
	ANTISUFFIX="_utf8"
	DATATYPE="uint_least32_t"
fi

cat << EOF
.Dd ${MAN_DATE}
.Dt GRAPHEME_TO_$(printf "%s%s" "$CASE" "$SUFFIX" | tr [:lower:] [:upper:]) 3
.Os suckless.org
.Sh NAME
.Nm grapheme_to_${CASE}${SUFFIX}
.Nd convert ${ARRAYTYPE} to ${CASE}
.Sh SYNOPSIS
.In grapheme.h
.Ft size_t
.Fn grapheme_to_${CASE}${SUFFIX} "const ${DATATYPE} *src" "size_t srclen" "${DATATYPE} *dest" "size_t destlen"
.Sh DESCRIPTION
The
.Fn grapheme_to_${CASE}${SUFFIX}
function converts the ${ARRAYTYPE}
.Va str
to ${CASE} and writes the result to
.Va dest
up to
.Va destlen ,
unless
.Va dest
is set to
.Dv NULL .
.Pp
If
.Va srclen
is set to
.Dv SIZE_MAX
(stdint.h is already included by grapheme.h) the ${ARRAYTYPE}
.Va src
is interpreted to be NUL-terminated and processing stops when a
NUL-byte is encountered.
.Pp
For $(if [ "$ENCODING" != "utf8" ]; then printf "UTF-8-encoded"; else printf "non-UTF-8"; fi) input data
.Xr grapheme_to_${CASE}${ANTISUFFIX} 3
can be used instead.
.Sh RETURN VALUES
The
.Fn grapheme_to_${CASE}${SUFFIX}
function returns the number of ${UNIT}s in the array resulting
from converting
.Va src
to ${CASE}, even if
.Va destlen
is not large enough or
.Va dest
is
.Dv NULL .
.Sh SEE ALSO
.Xr grapheme_to_${CASE}${ANTISUFFIX} 3 ,
.Xr libgrapheme 7
.Sh STANDARDS
.Fn grapheme_to_${CASE}${SUFFIX}
is compliant with the Unicode ${UNICODE_VERSION} specification.
.Sh AUTHORS
.An Laslo Hunhold Aq Mt dev@frign.de
EOF

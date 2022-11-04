# See LICENSE file for copyright and license details
# libgrapheme - unicode string library
.POSIX:
.SUFFIXES:

VERSION_MAJOR = 2
VERSION_MINOR = 0
VERSION_PATCH = 2
UNICODE_VERSION = 15.0.0
MAN_DATE = 2022-10-06

include config.mk

VERSION = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)

BENCHMARK =\
	benchmark/case\
	benchmark/character\
	benchmark/sentence\
	benchmark/line\
	benchmark/utf8-decode\
	benchmark/word\

DATA =\
	data/DerivedCoreProperties.txt\
	data/EastAsianWidth.txt\
	data/emoji-data.txt\
	data/GraphemeBreakProperty.txt\
	data/GraphemeBreakTest.txt\
	data/LICENSE\
	data/LineBreak.txt\
	data/LineBreakTest.txt\
	data/SentenceBreakProperty.txt\
	data/SentenceBreakTest.txt\
	data/SpecialCasing.txt\
	data/UnicodeData.txt\
	data/WordBreakProperty.txt\
	data/WordBreakTest.txt\

GEN =\
	gen/case\
	gen/character\
	gen/character-test\
	gen/line\
	gen/line-test\
	gen/sentence\
	gen/sentence-test\
	gen/word\
	gen/word-test\

SRC =\
	src/case\
	src/character\
	src/line\
	src/sentence\
	src/utf8\
	src/util\
	src/word\

TEST =\
	test/case\
	test/character\
	test/line\
	test/sentence\
	test/utf8-decode\
	test/utf8-encode\
	test/word\

MAN_TEMPLATE =\
	man/template/is_case.sh\
	man/template/next_break.sh\
	man/template/to_case.sh\

MAN3 =\
	man/grapheme_decode_utf8\
	man/grapheme_encode_utf8\
	man/grapheme_is_character_break\
	man/grapheme_is_uppercase\
	man/grapheme_is_uppercase_utf8\
	man/grapheme_is_lowercase\
	man/grapheme_is_lowercase_utf8\
	man/grapheme_is_titlecase\
	man/grapheme_is_titlecase_utf8\
	man/grapheme_next_character_break\
	man/grapheme_next_line_break\
	man/grapheme_next_sentence_break\
	man/grapheme_next_word_break\
	man/grapheme_next_character_break_utf8\
	man/grapheme_next_line_break_utf8\
	man/grapheme_next_sentence_break_utf8\
	man/grapheme_next_word_break_utf8\
	man/grapheme_to_uppercase\
	man/grapheme_to_uppercase_utf8\
	man/grapheme_to_lowercase\
	man/grapheme_to_lowercase_utf8\
	man/grapheme_to_titlecase\
	man/grapheme_to_titlecase_utf8\

MAN7 =\
	man/libgrapheme\

all: data/LICENSE $(MAN3:=.3) $(MAN7:=.7) libgrapheme.a $(SONAME)

data/DerivedCoreProperties.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/DerivedCoreProperties.txt

data/EastAsianWidth.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/EastAsianWidth.txt

data/emoji-data.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/emoji/emoji-data.txt

data/GraphemeBreakProperty.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/auxiliary/GraphemeBreakProperty.txt

data/GraphemeBreakTest.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/auxiliary/GraphemeBreakTest.txt

data/LICENSE:
	wget -O $@ https://www.unicode.org/license.txt

data/LineBreak.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/LineBreak.txt

data/LineBreakTest.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/auxiliary/LineBreakTest.txt

data/SentenceBreakProperty.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/auxiliary/SentenceBreakProperty.txt

data/SentenceBreakTest.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/auxiliary/SentenceBreakTest.txt

data/SpecialCasing.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/SpecialCasing.txt

data/UnicodeData.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/UnicodeData.txt

data/WordBreakProperty.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/auxiliary/WordBreakProperty.txt

data/WordBreakTest.txt:
	wget -O $@ https://www.unicode.org/Public/$(UNICODE_VERSION)/ucd/auxiliary/WordBreakTest.txt

benchmark/case.o: benchmark/case.c Makefile config.mk gen/word-test.h grapheme.h benchmark/util.h
benchmark/character.o: benchmark/character.c Makefile config.mk gen/character-test.h grapheme.h benchmark/util.h
benchmark/line.o: benchmark/line.c Makefile config.mk gen/line-test.h grapheme.h benchmark/util.h
benchmark/utf8-decode.o: benchmark/utf8-decode.c Makefile config.mk gen/character-test.h grapheme.h benchmark/util.h
benchmark/sentence.o: benchmark/sentence.c Makefile config.mk gen/sentence-test.h grapheme.h benchmark/util.h
benchmark/util.o: benchmark/util.c Makefile config.mk benchmark/util.h
benchmark/word.o: benchmark/word.c Makefile config.mk gen/word-test.h grapheme.h benchmark/util.h
gen/case.o: gen/case.c Makefile config.mk gen/util.h
gen/character.o: gen/character.c Makefile config.mk gen/util.h
gen/character-test.o: gen/character-test.c Makefile config.mk gen/util.h
gen/line.o: gen/line.c Makefile config.mk gen/util.h
gen/line-test.o: gen/line-test.c Makefile config.mk gen/util.h
gen/sentence.o: gen/sentence.c Makefile config.mk gen/util.h
gen/sentence-test.o: gen/sentence-test.c Makefile config.mk gen/util.h
gen/word.o: gen/word.c Makefile config.mk gen/util.h
gen/word-test.o: gen/word-test.c Makefile config.mk gen/util.h
gen/util.o: gen/util.c Makefile config.mk gen/util.h
src/case.o: src/case.c Makefile config.mk gen/case.h grapheme.h src/util.h
src/character.o: src/character.c Makefile config.mk gen/character.h grapheme.h src/util.h
src/line.o: src/line.c Makefile config.mk gen/line.h grapheme.h src/util.h
src/sentence.o: src/sentence.c Makefile config.mk gen/sentence.h grapheme.h src/util.h
src/utf8.o: src/utf8.c Makefile config.mk grapheme.h
src/util.o: src/util.c Makefile config.mk gen/types.h grapheme.h src/util.h
src/word.o: src/word.c Makefile config.mk gen/word.h grapheme.h src/util.h
test/case.o: test/case.c Makefile config.mk grapheme.h test/util.h
test/character.o: test/character.c Makefile config.mk gen/character-test.h grapheme.h test/util.h
test/line.o: test/line.c Makefile config.mk gen/line-test.h grapheme.h test/util.h
test/sentence.o: test/sentence.c Makefile config.mk gen/sentence-test.h grapheme.h test/util.h
test/utf8-encode.o: test/utf8-encode.c Makefile config.mk grapheme.h test/util.h
test/utf8-decode.o: test/utf8-decode.c Makefile config.mk grapheme.h test/util.h
test/util.o: test/util.c Makefile config.mk test/util.h
test/word.o: test/word.c Makefile config.mk gen/word-test.h grapheme.h test/util.h

benchmark/case: benchmark/case.o benchmark/util.o libgrapheme.a
benchmark/character: benchmark/character.o benchmark/util.o libgrapheme.a
benchmark/line: benchmark/line.o benchmark/util.o libgrapheme.a
benchmark/sentence: benchmark/sentence.o benchmark/util.o libgrapheme.a
benchmark/utf8-decode: benchmark/utf8-decode.o benchmark/util.o libgrapheme.a
benchmark/word: benchmark/word.o benchmark/util.o libgrapheme.a
gen/case: gen/case.o gen/util.o
gen/character: gen/character.o gen/util.o
gen/character-test: gen/character-test.o gen/util.o
gen/line: gen/line.o gen/util.o
gen/line-test: gen/line-test.o gen/util.o
gen/sentence: gen/sentence.o gen/util.o
gen/sentence-test: gen/sentence-test.o gen/util.o
gen/word: gen/word.o gen/util.o
gen/word-test: gen/word-test.o gen/util.o
test/case: test/case.o test/util.o libgrapheme.a
test/character: test/character.o test/util.o libgrapheme.a
test/line: test/line.o test/util.o libgrapheme.a
test/sentence: test/sentence.o test/util.o libgrapheme.a
test/utf8-encode: test/utf8-encode.o test/util.o libgrapheme.a
test/utf8-decode: test/utf8-decode.o test/util.o libgrapheme.a
test/word: test/word.o test/util.o libgrapheme.a

gen/case.h: data/DerivedCoreProperties.txt data/UnicodeData.txt data/SpecialCasing.txt gen/case
gen/character.h: data/emoji-data.txt data/GraphemeBreakProperty.txt gen/character
gen/character-test.h: data/GraphemeBreakTest.txt gen/character-test
gen/line.h: data/emoji-data.txt data/EastAsianWidth.txt data/LineBreak.txt gen/line
gen/line-test.h: data/LineBreakTest.txt gen/line-test
gen/sentence.h: data/SentenceBreakProperty.txt gen/sentence
gen/sentence-test.h: data/SentenceBreakTest.txt gen/sentence-test
gen/word.h: data/WordBreakProperty.txt gen/word
gen/word-test.h: data/WordBreakTest.txt gen/word-test

man/grapheme_is_character_break.3: man/grapheme_is_character_break.sh Makefile config.mk
man/grapheme_is_uppercase.3: man/grapheme_is_uppercase.sh man/template/is_case.sh Makefile config.mk
man/grapheme_is_uppercase_utf8.3: man/grapheme_is_uppercase_utf8.sh man/template/is_case.sh Makefile config.mk
man/grapheme_is_lowercase.3: man/grapheme_is_lowercase.sh man/template/is_case.sh Makefile config.mk
man/grapheme_is_lowercase_utf8.3: man/grapheme_is_lowercase_utf8.sh man/template/is_case.sh Makefile config.mk
man/grapheme_is_titlecase.3: man/grapheme_is_titlecase.sh man/template/is_case.sh Makefile config.mk
man/grapheme_is_titlecase_utf8.3: man/grapheme_is_titlecase_utf8.sh man/template/is_case.sh Makefile config.mk
man/grapheme_next_character_break.3: man/grapheme_next_character_break.sh man/template/next_break.sh Makefile config.mk
man/grapheme_next_line_break.3: man/grapheme_next_line_break.sh man/template/next_break.sh Makefile config.mk
man/grapheme_next_sentence_break.3: man/grapheme_next_sentence_break.sh man/template/next_break.sh Makefile config.mk
man/grapheme_next_word_break.3: man/grapheme_next_word_break.sh man/template/next_break.sh Makefile config.mk
man/grapheme_next_character_break_utf8.3: man/grapheme_next_character_break_utf8.sh man/template/next_break.sh Makefile config.mk
man/grapheme_next_line_break_utf8.3: man/grapheme_next_line_break_utf8.sh man/template/next_break.sh Makefile config.mk
man/grapheme_next_sentence_break_utf8.3: man/grapheme_next_sentence_break_utf8.sh man/template/next_break.sh Makefile config.mk
man/grapheme_next_word_break_utf8.3: man/grapheme_next_word_break_utf8.sh man/template/next_break.sh Makefile config.mk
man/grapheme_to_uppercase.3: man/grapheme_to_uppercase.sh man/template/to_case.sh Makefile config.mk
man/grapheme_to_lowercase.3: man/grapheme_to_lowercase.sh man/template/to_case.sh Makefile config.mk
man/grapheme_to_titlecase.3: man/grapheme_to_titlecase.sh man/template/to_case.sh Makefile config.mk
man/grapheme_to_uppercase_utf8.3: man/grapheme_to_uppercase_utf8.sh man/template/to_case.sh Makefile config.mk
man/grapheme_to_lowercase_utf8.3: man/grapheme_to_lowercase_utf8.sh man/template/to_case.sh Makefile config.mk
man/grapheme_to_titlecase_utf8.3: man/grapheme_to_titlecase_utf8.sh man/template/to_case.sh Makefile config.mk
man/grapheme_decode_utf8.3: man/grapheme_decode_utf8.sh Makefile config.mk
man/grapheme_encode_utf8.3: man/grapheme_encode_utf8.sh Makefile config.mk

man/libgrapheme.7: man/libgrapheme.sh Makefile config.mk

$(GEN:=.o) gen/util.o:
	$(BUILD_CC) -c -o $@ $(BUILD_CPPFLAGS) $(BUILD_CFLAGS) $(@:.o=.c)

$(BENCHMARK:=.o) benchmark/util.o $(TEST:=.o) test/util.o:
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $(@:.o=.c)

$(SRC:=.o):
	$(CC) -c -o $@ $(CPPFLAGS) $(CFLAGS) $(SHFLAGS) $(@:.o=.c)

$(BENCHMARK):
	$(CC) -o $@ $(LDFLAGS) $@.o benchmark/util.o libgrapheme.a -lutf8proc

$(GEN):
	$(BUILD_CC) -o $@ $(BUILD_LDFLAGS) $@.o gen/util.o

$(TEST):
	$(CC) -o $@ $(LDFLAGS) $@.o test/util.o libgrapheme.a

$(GEN:=.h):
	$(@:.h=) > $@

libgrapheme.a: $(SRC:=.o)
	$(AR) -rc $@ $?
	$(RANLIB) $@

$(SONAME): $(SRC:=.o)
	$(CC) -o $@ $(SOFLAGS) $(LDFLAGS) $(SRC:=.o)

$(MAN3:=.3):
	SH="$(SH)" MAN_DATE="$(MAN_DATE)" UNICODE_VERSION="$(UNICODE_VERSION)" $(SH) $(@:.3=.sh) > $@

$(MAN7:=.7):
	SH="$(SH)" MAN_DATE="$(MAN_DATE)" UNICODE_VERSION="$(UNICODE_VERSION)" $(SH) $(@:.7=.sh) > $@

benchmark: $(BENCHMARK)
	for m in $(BENCHMARK); do ./$$m; done

test: $(TEST)
	for m in $(TEST); do ./$$m; done

install: all
	mkdir -p "$(DESTDIR)$(LIBPREFIX)"
	mkdir -p "$(DESTDIR)$(INCPREFIX)"
	mkdir -p "$(DESTDIR)$(MANPREFIX)/man3"
	mkdir -p "$(DESTDIR)$(MANPREFIX)/man7"
	cp -f $(MAN3:=.3) "$(DESTDIR)$(MANPREFIX)/man3"
	cp -f $(MAN7:=.7) "$(DESTDIR)$(MANPREFIX)/man7"
	cp -f libgrapheme.a "$(DESTDIR)$(LIBPREFIX)"
	cp -f $(SONAME) "$(DESTDIR)$(LIBPREFIX)/$(SONAME)"
	if [ "$(SOSYMLINK)" = "true" ]; then i=0; while [ "$$i" -le $(VERSION_MINOR) ]; do ln -sf "$(SONAME)" "$(DESTDIR)$(LIBPREFIX)/libgrapheme.so.$(VERSION_MAJOR).$$i"; i=$$((i+1)); done; fi
	if [ "$(SOSYMLINK)" = "true" ]; then ln -sf "$(SONAME)" "$(DESTDIR)$(LIBPREFIX)/libgrapheme.so.$(VERSION_MAJOR)"; fi
	if [ "$(SOSYMLINK)" = "true" ]; then ln -sf "$(SONAME)" "$(DESTDIR)$(LIBPREFIX)/libgrapheme.so"; fi
	cp -f grapheme.h "$(DESTDIR)$(INCPREFIX)"
	if ! [ -z "$(LDCONFIG)" ]; then $(SHELL) -c "$(LDCONFIG)"; fi
	if ! [ -z "$(PCPREFIX)" ]; then mkdir -p "$(DESTDIR)$(PCPREFIX)"; printf "Name: libgrapheme\nDescription: Unicode string library\nURL: https://libs.suckless.org/libgrapheme/\nVersion: $(VERSION)\nCflags: -I$(INCPREFIX)\nLibs: -L$(LIBPREFIX) -lgrapheme\n" > "$(DESTDIR)$(PCPREFIX)/libgrapheme.pc"; fi

uninstall:
	for m in $(MAN3:=.3); do rm -f "$(DESTDIR)$(MANPREFIX)/man3/`basename $$m`"; done
	for m in $(MAN7:=.7); do rm -f "$(DESTDIR)$(MANPREFIX)/man7/`basename $$m`"; done
	rm -f "$(DESTDIR)$(LIBPREFIX)/libgrapheme.a"
	rm -f "$(DESTDIR)$(LIBPREFIX)/$(SONAME)"
	if [ "$(SOSYMLINK)" = "true" ]; then i=0; while [ "$$i" -le $(VERSION_MINOR) ]; do rm -f "$(DESTDIR)$(LIBPREFIX)/libgrapheme.so.$(VERSION_MAJOR).$$i"; i=$$((i+1)); done; fi
	if [ "$(SOSYMLINK)" = "true" ]; then rm -f "$(DESTDIR)$(LIBPREFIX)/libgrapheme.so.$(VERSION_MAJOR)"; fi
	if [ "$(SOSYMLINK)" = "true" ]; then rm -f "$(DESTDIR)$(LIBPREFIX)/libgrapheme.so"; fi
	rm -f "$(DESTDIR)$(INCPREFIX)/grapheme.h"
	if ! [ -z "$(LDCONFIG)" ]; then $(SHELL) -c "$(LDCONFIG)"; fi
	if ! [ -z "$(PCPREFIX)" ]; then rm -f "$(DESTDIR)$(PCPREFIX)/libgrapheme.pc"; fi

clean:
	rm -f $(BENCHMARK:=.o) benchmark/util.o $(BENCHMARK) $(GEN:=.h) $(GEN:=.o) gen/util.o $(GEN) $(SRC:=.o) src/util.o $(TEST:=.o) test/util.o $(TEST) libgrapheme.a $(SONAME) $(MAN3:=.3) $(MAN7:=.7)

clean-data:
	rm -f $(DATA)

dist:
	rm -rf "libgrapheme-$(VERSION)"
	mkdir "libgrapheme-$(VERSION)"
	for m in benchmark data gen man man/template src test; do mkdir "libgrapheme-$(VERSION)/$$m"; done
	cp config.mk configure grapheme.h LICENSE Makefile README "libgrapheme-$(VERSION)"
	cp $(BENCHMARK:=.c) benchmark/util.c benchmark/util.h "libgrapheme-$(VERSION)/benchmark"
	cp $(DATA) "libgrapheme-$(VERSION)/data"
	cp $(GEN:=.c) gen/util.c gen/types.h gen/util.h "libgrapheme-$(VERSION)/gen"
	cp $(MAN3:=.sh) $(MAN7:=.sh) "libgrapheme-$(VERSION)/man"
	cp $(MAN_TEMPLATE) "libgrapheme-$(VERSION)/man/template"
	cp $(SRC:=.c) src/util.h "libgrapheme-$(VERSION)/src"
	cp $(TEST:=.c) test/util.c test/util.h "libgrapheme-$(VERSION)/test"
	tar -cf - "libgrapheme-$(VERSION)" | gzip -c > "libgrapheme-$(VERSION).tar.gz"
	rm -rf "libgrapheme-$(VERSION)"

.PHONY: all benchmark test install uninstall clean clean-data dist

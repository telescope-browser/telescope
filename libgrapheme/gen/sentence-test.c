/* See LICENSE file for copyright and license details. */
#include <stddef.h>

#include "util.h"

int
main(int argc, char *argv[])
{
	struct break_test *test = NULL;
	size_t testlen = 0;

	(void)argc;

	break_test_list_parse("data/SentenceBreakTest.txt", &test, &testlen);
	break_test_list_print(test, testlen, "sentence_break_test", argv[0]);
	break_test_list_free(test, testlen);

	return 0;
}

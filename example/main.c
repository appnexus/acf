#include <stdio.h>
#include <stdlib.h>

#include "an_array.h"
#include "an_allocator.h"

struct foo {
	int x;
};

AN_ARRAY(foo, foo_list)

static void
print_array(AN_ARRAY_INSTANCE(foo_list) *a)
{
	printf("foo_array: \n");
	struct foo *cursor;
	AN_ARRAY_FOREACH(a, cursor) {
		printf("%d ", cursor->x);
	}
	printf("\n");
}

int main(int argc, char **argv) {
	AN_ARRAY_INSTANCE(foo_list) foo_array;
	AN_ARRAY_INIT(foo_list, &foo_array, 8);

	print_array(&foo_array);

	struct foo foo1 = { .x = 1 };
	AN_ARRAY_PUSH(foo_list, &foo_array, &foo1);
	struct foo foo2 = { .x = 2 };
	AN_ARRAY_PUSH(foo_list, &foo_array, &foo2);

	print_array(&foo_array);

	AN_ARRAY_DEINIT(foo_list, &foo_array);
	return 0;
}

#include "an_array.h"

struct node {
	int value;
};

AN_ARRAY(node, test);
AN_ARRAY_PRIMITIVE(int, test2);

int
main(int argc, char *argv[])
{
	return 0;
}

// @array = size keeps the elements
int test_resize()
{
	int values[2];
	values[0] = 7;
	@values = 5;
	values[4] = 1;
	return @values == 5 && values[0] == 7 && values[4] == 1;
}

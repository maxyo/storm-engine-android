// Locals are block scoped: the same name in sibling blocks or loops is a separate variable.
int test_sibling_blocks()
{
	int total = 0;
	if (true)
	{
		int value = 5;
		total += value;
	}
	if (true)
	{
		string value = "7";
		total += makeint(value);
	}
	return total == 12;
}

int test_for_variables()
{
	int sum = 0;
	for (int i = 0; i < 3; i++)
	{
		sum += i;
	}
	for (int i = 10; i < 12; i++)
	{
		sum += i;
	}
	return sum == 24;
}

int test_shadowing()
{
	int x = 1;
	int seen = 0;
	if (x == 1)
	{
		int x = 2;
		seen = x;
	}
	return seen == 2 && x == 1;
}

int test_declarator_list()
{
	int a = 3, b, c = 4;
	b = a + c;
	return b == 7;
}

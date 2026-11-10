// Local arrays sized at run time and array initializers.
int Sum(ref values, int count)
{
	int total = 0;
	for (int i = 0; i < count; i++)
	{
		total += values[i];
	}
	return total;
}

int test_runtime_size()
{
	int count = 4;
	int values[count];
	for (int i = 0; i < count; i++)
	{
		values[i] = i + 1;
	}
	return GetArraySize(&values) == 4 && Sum(&values, count) == 10;
}

int test_initializer()
{
	int values[3] = {2, 3, 4};
	string names[] = {"a", "b"};
	return Sum(&values, 3) == 9 && names[1] == "b" && GetArraySize(&names) == 2;
}

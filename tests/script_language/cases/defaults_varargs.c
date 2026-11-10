// Default parameter values and "..." with GetArg/GetArgCount.
native ref GetArg(int index);
native int GetArgCount();

int Scale(int value, int factor = 2, string suffix = "")
{
	return value * factor + strlen(suffix);
}

int Count(...)
{
	return GetArgCount();
}

int SumAfterFirst(string label, ...)
{
	int total = 0;
	for (int i = 1; i < GetArgCount(); i++)
	{
		total += GetArg(i);
	}
	return total;
}

int test_defaults()
{
	return Scale(5) == 10 && Scale(5, 3) == 15 && Scale(5, 3, "ab") == 17;
}

int test_varargs()
{
	return Count() == 0 && Count(1, "two", 3.0) == 3 && SumAfterFirst("sum", 1, 2, 3) == 6;
}

int test_default_statement()
{
	Scale(1);
	return 1;
}

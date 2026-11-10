// goto labels belong to their function; two functions may use the same label.
int CountDown(int from)
{
	int steps = 0;
again:
	if (from > 0)
	{
		from--;
		steps++;
		goto again;
	}
	return steps;
}

int Twice()
{
	int n = 0;
again:
	n++;
	if (n < 2) goto again;
	return n;
}

int test_goto()
{
	return CountDown(3) == 3 && Twice() == 2;
}

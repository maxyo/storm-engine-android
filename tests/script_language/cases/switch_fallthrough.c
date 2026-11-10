// cases without break: return, fall through to the next case, or end at the switch's '}'
int Kind(string name)
{
	switch (name)
	{
		case "a": return 1;
		case "b": return 2;
	}
	return 0;
}

int Stacked(int n)
{
	int result = 0;
	switch (n)
	{
		case 1:
		case 2:
			result = 12;
			break;
		case 3:
			result = 3;
		case 4:
			result += 40;
	}
	return result;
}

int test_switch()
{
	return Kind("a") == 1 && Kind("b") == 2 && Kind("c") == 0 && Stacked(1) == 12 && Stacked(2) == 12 &&
	       Stacked(3) == 43 && Stacked(4) == 40 && Stacked(9) == 0;
}

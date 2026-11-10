// negative case values and default:
int Classify(int n)
{
	switch (n)
	{
		case -1: return 10; break;
		case 0: return 20; break;
		default: return 30; break;
	}
	return 0;
}

int test_switch()
{
	return Classify(-1) == 10 && Classify(0) == 20 && Classify(7) == 30;
}

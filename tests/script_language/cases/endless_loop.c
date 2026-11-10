// An endless script loop stops with an error and the script stack (call time limit) instead of
// hanging the engine; the VM works again for the next call.
int Spin(int n)
{
	while (true)
	{
		n++;
	}
	return n;
}

int fail_endless_loop()
{
	Spin(0);
	return 1;
}

int fail_endless_for()
{
	int sum = 0;
	for (int i = 0; i < 10; i++)
	{
		i = 0;
	}
	return 1;
}

int test_after_stop()
{
	return 1 + 1 == 2;
}

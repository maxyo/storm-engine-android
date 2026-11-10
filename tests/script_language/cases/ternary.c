// cond ? a : b, nested and inside other expressions
int Pick(int n)
{
	return n > 0 ? 1 : (n < 0 ? -1 : 0);
}

int test_ternary()
{
	string word = Pick(5) == 1 ? "plus" : "other";
	int sum = 10 + (Pick(-3) == -1 ? 5 : 100);
	return word == "plus" && sum == 15 && Pick(0) == 0 && Pick(-2) == -1;
}

int test_ternary_in_condition()
{
	int x = 3;
	if ((x > 2 ? x : 0) == 3 && (x == 1 || x == 3))
		return 1;
	return 0;
}

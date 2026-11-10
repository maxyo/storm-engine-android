// nullptr comparisons and typeid
object holder;

ref FindNothing()
{
	return nullptr;
}

int TypeName(ref value)
{
	switch (typeid(value))
	{
		case typeid(int): return 1; break;
		case typeid(string): return 2; break;
		case typeid(object): return 3; break;
	}
	return 0;
}

int test_nullptr()
{
	ref r = FindNothing();
	ref h;
	makeref(h, holder);
	return r == nullptr && r is nullptr && h != nullptr;
}

int test_typeid()
{
	int number = 3;
	string text = "a";
	return TypeName(&number) == 1 && TypeName(&text) == 2 && TypeName(&holder) == 3 && typeid(number) == typeid(int);
}

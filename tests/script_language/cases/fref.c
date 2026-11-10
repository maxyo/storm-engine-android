// function references: &Function, calls through a variable, attributes, fref(nullptr)
object hooks;

int Twice(int value)
{
	return value * 2;
}

int Apply(fref callback, int value)
{
	return callback(value);
}

int test_call()
{
	fref f = &Twice;
	return f(4) == 8 && Apply(&Twice, 5) == 10;
}

int test_attribute()
{
	hooks.handler := &Twice;
	fref restored = hooks.handler$fref(fref(nullptr));
	fref missing = hooks.nothing$fref(fref(nullptr));
	return restored(3) == 6 && !bool(missing) && bool(restored) && missing == nullptr;
}

int test_compare()
{
	fref a = &Twice;
	fref b = &Twice;
	return a == b;
}

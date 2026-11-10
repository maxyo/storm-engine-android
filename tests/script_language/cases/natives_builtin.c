// Scripts may declare built-in functions as natives when the signature matches.
native int rand(int max);
native void Trace(string text);
native void Event(string name, ...);

int test_builtin()
{
	int value = rand(3);
	Trace("rand: " + value);
	return value >= 0 && value <= 3;
}

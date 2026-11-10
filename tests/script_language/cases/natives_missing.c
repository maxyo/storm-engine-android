// expect-error: native function 'ScriptCheckNotProvided' is not implemented by the engine
native int ScriptCheckNotProvided(int value);

int test_call()
{
	return ScriptCheckNotProvided(1);
}

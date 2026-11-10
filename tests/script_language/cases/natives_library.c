// A library may register natives the scripts never declare (script_check's test library also
// registers ScriptCheckUnused): only a log line, not an error.
#libriary "script_check_test_library"

native int ScriptCheckAddOne(int value);

int test_registered_native()
{
	return ScriptCheckAddOne(41) == 42;
}

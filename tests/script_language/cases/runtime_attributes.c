// function references kept in attributes with := and called through them; natives read an int
// passed for a float parameter as that number; float() of attribute text
#libriary "script_check_test_library"

native int ScriptCheckAddOne(int value);
native float ScriptCheckHalf(float value);
object encounters;

int Check_Jungle()
{
	return 7;
}

void Register(string name)
{
	aref entry = touchattr(encounters.Data.(name));
	entry.Weight := 3;
	entry.isValid := fref("Check_" + name);
}

int test_attribute_functions()
{
	Register("Jungle");
	aref entry = &encounters.Data.Jungle;
	return entry.isValid() == 7 && entry.Weight$int(0) == 3 && encounters.Data.Jungle.isValid() == 7;
}

int test_native_arguments()
{
	encounters.angle = 0.5;
	return ScriptCheckHalf(3) == 1.5 && ScriptCheckHalf(float(encounters.angle)) == 0.25 && ScriptCheckAddOne(41) == 42;
}

// int() float() bool() string() kstring() casts
int test_casts()
{
	string text = "42";
	float half = 2.5;
	int fromString = int(text) + 1;
	int fromFloat = int(half * 2.0);
	string back = string(fromString);
	kstring key = kstring("id_" + back);
	return fromString == 43 && fromFloat == 5 && back == "43" && key == "id_43" && float(3) == 3.0;
}

int test_bool()
{
	return bool(5) && !bool(0) && bool("yes") && !bool("") && !bool("0");
}

int test_cast_keeps_variable()
{
	string text = "7";
	int value = int(text);
	return value == 7 && text == "7";
}

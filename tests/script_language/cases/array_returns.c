// functions returning arrays, array literals; UTF-8 identifiers
string []Pair(string first)
{
	string second = first + "!";
	return {first, second};
}

int test_array_return()
{
	string values[2];
	values = Pair("a");
	return values[0] == "a" && values[1] == "a!";
}

int Double(int значение)
{
	return значение * 2;
}

int test_utf8_identifier()
{
	return Double(4) == 8;
}

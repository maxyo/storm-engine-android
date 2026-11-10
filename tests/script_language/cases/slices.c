// text[a .. b]: bounds optional, negative ones count from the end; arrays sized at run time
string names[];

int test_slices()
{
	string word = "[SEL]choice";
	return word[.. 5] == "[SEL]" && word[5 ..] == "choice" && word[-1 ..] == "e" && word[1 .. 4] == "SEL" &&
	       word[.. -6] == "[SEL]" && ("abc" + "def")[2 .. 4] == "cd";
}

int test_dynamic_arrays()
{
	int local[];
	SetArraySize(&names, 3);
	names[2] = "x";
	@local = 2;
	local[1] = 5;
	return GetArraySize(&names) == 3 && names[2] == "x" && @local == 2 && local[1] == 5;
}

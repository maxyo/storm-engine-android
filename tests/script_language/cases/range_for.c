// for (item : container): attributes (aref item) or array elements, with an optional index
object bag;

int test_attributes()
{
	DeleteAttribute(&bag, "");
	bag.first = "1";
	bag.second = "2";
	bag.third = "3";
	int sum = 0;
	string names = "";
	for (aref entry: bag)
	{
		sum += makeint(GetAttributeValue(entry));
		names += GetAttributeName(entry);
	}
	return sum == 6 && names == "firstsecondthird";
}

int test_elements()
{
	int values[3] = {4, 5, 6};
	int sum = 0;
	int weighted = 0;
	for (int position, int value: values)
	{
		sum += value;
		weighted += position * value;
	}
	for (int value: values)
	{
		if (value == 5) continue;
		sum += value;
	}
	return sum == 25 && weighted == 17;
}

int test_reference_items()
{
	int values[2] = {1, 2};
	for (ref value: &values) value = value * 10;
	return values[0] == 10 && values[1] == 20;
}

int test_attribute_names_as_keys()
{
	DeleteAttribute(&bag, "");
	bag.left = "0.5";
	bag.right = "1.0";
	aref view;
	makearef(view, bag);
	string names = "";
	float sum = 0.0;
	for (string name, aref entry: &view)
	{
		names += name;
		sum += makefloat(view.(name));
	}
	return names == "leftright" && sum == 1.5;
}

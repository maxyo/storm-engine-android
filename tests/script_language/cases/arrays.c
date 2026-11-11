// Local arrays sized at run time and array initializers.
int Sum(ref values, int count)
{
	int total = 0;
	for (int i = 0; i < count; i++)
	{
		total += values[i];
	}
	return total;
}

int test_runtime_size()
{
	int count = 4;
	int values[count];
	for (int i = 0; i < count; i++)
	{
		values[i] = i + 1;
	}
	return GetArraySize(&values) == 4 && Sum(&values, count) == 10;
}

int test_initializer()
{
	int values[3] = {2, 3, 4};
	string names[] = {"a", "b"};
	return Sum(&values, 3) == 9 && names[1] == "b" && GetArraySize(&names) == 2;
}

// Object arrays passed by reference: indexed attribute writes, resizing, a dynamic call and
// shuffling the objects with their attributes.
void FillLabels(ref items, string prefix)
{
	SetArraySize(items, 3);
	for (int i = 0; i < 3; i++)
	{
		items[i].label = prefix + i;
		items[i].label.order = i;
	}
}

void CopyLabels(ref target)
{
	int count = GetArraySize(target);
	object source[1];
	string filler = "FillLabels";
	call filler(&source, "x");
	ArrayShuffle(&source);
	int total = 0;
	for (int i = 0; i < 3; i++)
	{
		total += makeint(source[i].label.order);
		if (source[i].label != "x" + source[i].label.order) return;
	}
	if (total != 3) return;
	for (i = 0; i < count; i++)
	{
		target[i].label = source[i % 3].label;
	}
}

int test_object_array_reference()
{
	object targets[4];
	CopyLabels(&targets);
	return GetArraySize(&targets) == 4 && strlen(targets[3].label) == 2 && targets[0].label != "";
}

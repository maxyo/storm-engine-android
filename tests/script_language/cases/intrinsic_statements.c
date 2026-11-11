// touchattr/deleteattr as statements end at their ';': as the body of an unbraced if/else they do
// not take the next statement with them
native int GetAttributesNum(ref ar);

int Collect(int kind, int size, ref result)
{
	deleteattr(&result.old);
	switch (kind)
	{
		case 1:
			touchattr(result.first);
			if (size <= 2) touchattr(result.big);
			else touchattr(result.small);
		break;
		case 2:
			if (size > 0) deleteattr(&result.first);
			touchattr(result.second);
		break;
		case 3:
			return -1;
		break;
	}
	return GetAttributesNum(result);
}

int test_unbraced_if_bodies()
{
	object o;
	o.old = 1;
	int large = Collect(1, 1, &o);
	object p;
	int small = Collect(1, 6, &p);
	return large == 2 && CheckAttribute(&o, "big") && small == 2 && CheckAttribute(&p, "small") && !CheckAttribute(&o, "old");
}

int test_unbraced_deleteattr()
{
	object o;
	o.first = 1;
	return Collect(2, 0, &o) == 2 && Collect(2, 1, &o) == 1 && !CheckAttribute(&o, "first");
}

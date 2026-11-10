// $type(default) accessors, "key" in object, @ counts
object store;

int test_dollar()
{
	store.count = "12";
	store.ratio = "0.5";
	return store.count$int(0) == 12 && store.missing$int(-1) == -1 && store.ratio$float(1.0) == 0.5 &&
	       store.missing$string("none") == "none" && store.count$bool(false);
}

int test_in()
{
	DeleteAttribute(&store, "");
	store.a.b = "1";
	return "a" in store && "a.b" in store && !("c" in store) && "c" !in store;
}

int test_count()
{
	int values[5];
	DeleteAttribute(&store, "");
	store.x = "1";
	store.y = "2";
	aref branch;
	makearef(branch, store);
	return @values == 5 && @store == 2 && @branch == 2 && @values - 1 == 4;
}

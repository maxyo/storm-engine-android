// aref[i] is the i-th attribute; aref(x) the attribute reference of an object; INT_MAX; #define x true
#define FLAG_ON true
object table;

int test_aref_index()
{
	DeleteAttribute(&table, "");
	table.alpha = "1";
	table.beta = "2";
	aref root;
	makearef(root, table);
	aref second = root[1];
	return GetAttributeName(second) == "beta" && GetAttributeValue(root[0]) == "1";
}

int test_aref_cast()
{
	DeleteAttribute(&table, "");
	table.x = "7";
	aref whole = aref(&table);
	return GetAttributesNum(whole) == 1 && INT_MAX > 0 && INT_MIN < 0 && FLAG_ON == 1;
}

// obj.attr(args) calls the function the attribute names; call (expression)(args); $(x); aref[i].x
object shop;
int calls = 0;

int Shop_Price(ref this, int base)
{
	return base * 2;
}

void Shop_Count()
{
	calls++;
}

int test_attribute_call()
{
	shop.Price := &Shop_Price;
	shop.Count := &Shop_Count;
	shop.Count();
	call ("Shop_" + "Count")();
	return shop.Price(shop, 21) == 42 && calls == 2;
}

int test_text_and_children()
{
	DeleteAttribute(&shop, "");
	shop.items.first.cost = "5";
	shop.items.second.cost = "6";
	aref items;
	makearef(items, shop.items);
	aref second = &items[1];
	return $(12) == "12" && items[0].cost == "5" && items[1].cost$int(0) == 6 && GetAttributeName(second) == "second";
}

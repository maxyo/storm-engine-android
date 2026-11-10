// object literals, class tables and object->Method(...)
object CounterClass = {
	"Add" = &Counter_Add,
	"Get" = &Counter_Get,
};

void Counter_Add(ref this, int amount)
{
	this.value = makeint(this.value) + amount;
}

int Counter_Get(ref this)
{
	return makeint(this.value);
}

object NewCounter()
{
	return {
		classtable = CounterClass,
		"value" = 0
	};
}

int test_literal()
{
	object point = { x = 3, "y" = "4", nested = { z = 5 } };
	return makeint(point.x) == 3 && point.y == "4" && makeint(point.nested.z) == 5;
}

int test_methods()
{
	object counter = NewCounter();
	counter->Add(2);
	counter->Add(5);
	return counter->Get() == 7;
}

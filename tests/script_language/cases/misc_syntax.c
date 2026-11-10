// backtick strings, fref defaults, attribute of a call result, lua declarations, nullptr globals
lua string ScriptCheckLuaFunction(string a);
ref noReference = nullptr;
object holder;

aref HolderRoot()
{
	aref root;
	makearef(root, holder);
	return root;
}

int Apply(int value, fref f = fref(nullptr), fref g = &Twice)
{
	if (bool(f)) return f(value);
	return g(value);
}

int Twice(int value)
{
	return value * 2;
}

int test_misc()
{
	holder.name = "box";
	string quoted = `a b`;
	return quoted == "a b" && Apply(4) == 8 && HolderRoot().name == "box" && noReference == nullptr;
}

// SetClasstable gives an object a class table at run time; GetTypeNameByTypeID
native void SetClasstable(ref object, ref table);
native string GetTypeNameByTypeID(int id);

object ShapeClass = {
	"Area" = &Shape_Area,
};
object square;

int Shape_Area(ref this)
{
	return makeint(this.side) * makeint(this.side);
}

int test_classtable()
{
	square.side = "3";
	SetClasstable(&square, &ShapeClass);
	return square->Area() == 9 && GetTypeNameByTypeID(typeid(string)) == "string";
}

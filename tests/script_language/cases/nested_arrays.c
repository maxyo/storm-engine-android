// arrays of references to new arrays (a[i][j]); text[i] is one character
ref grid[];

void InitGrid(ref rows)
{
	@rows = 3;
	for (int i = 0; i < 3; i++)
	{
		rows[i] = new string[4];
		for (int j = 0; j < 4; j++)
			rows[i][j] = "" + i + j;
	}
}

int test_nested()
{
	InitGrid(&grid);
	return grid[2][3] == "23" && grid[0][1] == "01" && grid[1][0] != nullptr;
}

int test_characters()
{
	string word = "port";
	return word[0] == "p" && word[-1] == "t" && string(42)[1] == "2";
}

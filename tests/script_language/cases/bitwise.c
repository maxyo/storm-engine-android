// & | ~ << >> and |=
int test_bitwise()
{
	int flags = 1 | 4;
	int masked = flags & ~4;
	int shifted = (3 << 4) + (256 >> 2);
	return flags == 5 && masked == 1 && shifted == 112;
}

int test_or_assign()
{
	int state = 2;
	state |= 8;
	return state == 10;
}

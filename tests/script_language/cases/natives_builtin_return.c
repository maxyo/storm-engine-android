// expect-error: native declaration of built-in function 'rand' returns string, the engine's returns integer
native string rand(int max);

int test_none()
{
	return 1;
}

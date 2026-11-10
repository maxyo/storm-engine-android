// expect-error: native declaration of built-in function 'rand' takes 2 arguments, the engine's takes 1
native int rand(int low, int high);

int test_none()
{
	return 1;
}

#include "parseContext.h"
#include "compiler/compiler.h"

// possible invocation: 3bx main.3bx
// will compile 3bx to an executable named main
// to execute that executable: ./main
// the compiler will always receive one source file, since that file imports all other files
// if no arguments are given, the program will print its arguments to the console
int main(char *argumentValues[], int argumentCount)
{
	if (argumentCount)
	{
		ParseContext context;
		// first, read all source files
		importSourceFile(argumentValues[0], context);
	}
	else
	{
		// print arguments
	}

	return 0;
}
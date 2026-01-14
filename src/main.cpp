#include "parseContext.h"
#include "compiler/compiler.h"

// possible invocation: 3bx main.3bx
// will compile 3bx to an executable named main
// to execute that executable: ./main
// the compiler will always receive one source file, since that file imports all other files
// if no arguments are given, the program will print its arguments to the console
int main(int argumentCount, char *argumentValues[])
{
	if (argumentCount)
	{
		ParseContext context{};
		compile(argumentValues[1], context);
		context.reportDiagnostics();
		
	}
	else
	{
		// print arguments
	}

	return 0;
}
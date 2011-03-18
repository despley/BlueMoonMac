/**
 * @file
 * Main program of Blue Moon.
 */

#import <Cocoa/Cocoa.h>
#include <libintl.h>

#define PACKAGE "bluemoon"

int main(int argc, char *argv[])
{
	// Change numeric format to widely portable mode 
	setlocale(LC_NUMERIC, "C");

    // Run the application
    return NSApplicationMain(argc,  (const char **)argv);
}

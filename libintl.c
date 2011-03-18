/**
 * @file
 * Carbonized versions of gettext() and ngettext() localization functions.
 * They are mostly compatible with libintl so the game engine can use them
 * instead of libintl with no changes needed.
 */

#include <Carbon/Carbon.h>

/// Global dictionary used to store localized strings.
static CFMutableDictionaryRef g_textDictionary = NULL;

char *gettext (char *__msgid)
{
    // Create global string dictionary when gettext() is called for the first time
    if (!g_textDictionary)
        g_textDictionary = CFDictionaryCreateMutable(NULL, 100, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    // Convert __msgid to CFString
    CFStringRef msgid = CFStringCreateWithCString(NULL, __msgid, kCFStringEncodingISOLatin1);

    // Try to get already loaded localized string from g_textDictionary
    CFMutableDataRef data = (CFMutableDataRef)CFDictionaryGetValue(g_textDictionary, msgid);
        
    if (!data)
    {
        // Load localized string from "Engine" string table
        CFStringRef result = CFBundleCopyLocalizedString(CFBundleGetMainBundle(), msgid, msgid, CFSTR("Engine"));
        if (result)
        {
            // Get amount of bytes needed for the localized string with
            // UTF-8 encoding, including null character.
            CFIndex numBytes;
            CFRange range = CFRangeMake(0, CFStringGetLength(result));
            CFStringGetBytes(result, range, kCFStringEncodingUTF8, 0, false, NULL, 0, &numBytes);

            // Allocate data buffer for the localized string 
            data = CFDataCreateMutable(NULL, numBytes + 1);
            CFDataIncreaseLength(data, numBytes + 1);
            
            // Store localized string to the data buffer
            CFStringGetBytes(result, range, kCFStringEncodingUTF8, 0, false, CFDataGetMutableBytePtr(data), numBytes, NULL);

            // Store the data buffer into g_textDictionary
            CFDictionaryAddValue(g_textDictionary, msgid, data);
        }
    }    
    // If localized string was found, return it. Else return the original string.
    return data ? (char *)CFDataGetBytePtr(data) : __msgid;
}

char *ngettext (char *__msgid1, char *__msgid2, unsigned long int __n)
{
    // Return correct localized string depending on value of __n
    return gettext(__n != 1 ? __msgid2 : __msgid1);
}

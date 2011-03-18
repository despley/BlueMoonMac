/**
 * @file
 * Carbonized versions of gettext() and ngettext() localization functions.
 * They are mostly compatible with libintl so the game engine can use them
 * instead of libintl with no changes needed.
 */

#pragma once

/**
 * Carbon implementation of gettext. Returns a localized version of string __msgid
 * if available. Otherwise returns __msgid unchanged.
 *
 * @param __msgid A string to be localized.
 
 * @return Localized version of the string if available, or __msgid.
 */
extern char *gettext (char *__msgid);

/**
 * Limited Carbon implementation of ngettext(), not completely compatible
 * with libintl.
 *
 * @param __msgid1  String in singular form to be localized.
 * @param __msgid2  String in plural form to be localized.
 * @param __n       Plurality. If this is 1, __msgid1 is used, else __msgid2 is used.
 *
 * @return Localized version of __msgid1 or __msgid2 if available, or original
 *         __msgid1/__msgid2 when no localization is available.
 */
extern char *ngettext (char *__msgid1, char *__msgid2, unsigned long int __n);

/* SPDX-License-Identifier: GPL-2.0 */
#pragma once

// these values are sed'ed so they may be empty

#define FILEVER_MAJOR 5
#define FILEVER_MINOR 0
#define FILEVER_REVISION 0

#define _VERSION_STRING(maj, min, rev) #maj "." #min "." #rev
#define VERSION_STRING(maj, min, rev) _VERSION_STRING(maj, min, rev)

#define FILEVER_STR VERSION_STRING(FILEVER_MAJOR, FILEVER_MINOR, FILEVER_REVISION)

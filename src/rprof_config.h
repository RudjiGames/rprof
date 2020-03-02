/*
 * Copyright (c) 2020 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef RPROF_CONFIG_H
#define RPROF_CONFIG_H

#define RPROF_SCOPES_MAX            (16*1024)
#define RPROF_TEXT_MAX			    (1024*1024)
#define RPROF_DRAW_THREADS_MAX	    (1024)

/*--------------------------------------------------------------------------
 * Define to 1 if LZ4 is already statically linked with project using rprof
 *------------------------------------------------------------------------*/
#define RPROF_LZ4_NO_DEFINE			0

#endif /* RPROF_CONFIG_H */

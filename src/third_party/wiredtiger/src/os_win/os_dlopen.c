/*-
 * Copyright (c) 2014-2017 MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/*
 * __wt_dlopen --
 *	Open a dynamic library.
 */
int
__wt_dlopen(WT_SESSION_IMPL *session, const char *path, WT_DLH **dlhp)
{
	DWORD windows_error;
	WT_DECL_RET;
	WT_DLH *dlh;

	WT_RET(__wt_calloc_one(session, &dlh));
	WT_ERR(__wt_strdup(session, path, &dlh->name));
	WT_ERR(__wt_strdup(session, path == NULL ? "local" : path, &dlh->name));

	/* NULL means load from the current binary */
	if (path == NULL) {
		if (GetModuleHandleExW(
		    0, NULL, (HMODULE *)&dlh->handle) == FALSE) {
			windows_error = __wt_getlasterror();
			__wt_errx(session,
			    "GetModuleHandleExW: %s: %s",
			    path, __wt_formatmessage(session, windows_error));
			WT_ERR(__wt_map_windows_error(windows_error));
		}
	} else {
		// TODO: load dll here
		DebugBreak();
	}

	*dlhp = dlh;
	if (0) {
err:		__wt_free(session, dlh->name);
		__wt_free(session, dlh);
	}
	return (ret);
}

/*
 * __wt_dlsym --
 *	Lookup a symbol in a dynamic library.
 */
int
__wt_dlsym(WT_SESSION_IMPL *session,
    WT_DLH *dlh, const char *name, bool fail, void *sym_ret)
{
	DWORD windows_error;
	void *sym;

	*(void **)sym_ret = NULL;

	sym = GetProcAddress(dlh->handle, name);
	if (sym == NULL && fail) {
		windows_error = __wt_getlasterror();
		__wt_errx(session,
		    "GetProcAddress: %s in %s: %s",
		    name, dlh->name,
		    __wt_formatmessage(session, windows_error));
		WT_RET(__wt_map_windows_error(windows_error));
	}

	*(void **)sym_ret = sym;
	return (0);
}

/*
 * __wt_dlclose --
 *	Close a dynamic library
 */
int
__wt_dlclose(WT_SESSION_IMPL *session, WT_DLH *dlh)
{
	DWORD windows_error;
	WT_DECL_RET;

	if (FreeLibrary(dlh->handle) == FALSE) {
		windows_error = __wt_getlasterror();
		__wt_errx(session, "FreeLibrary: %s: %s",
		    dlh->name, __wt_formatmessage(session, windows_error));
		ret = __wt_map_windows_error(windows_error);
	}

	__wt_free(session, dlh->name);
	__wt_free(session, dlh);
	return (ret);
}

/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: t; tab-width: 4 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is [Open Source Virtual Machine.].
 *
 * The Initial Developer of the Original Code is
 * Adobe System Incorporated.
 * Portions created by the Initial Developer are Copyright (C) 2004-2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Adobe AS3 Team
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */


#include "MMgc.h"

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef _DEBUG
#include <malloc.h>
#include <strsafe.h>
#endif


/*************************************************************************/
/******************************* Debugging *******************************/
/*************************************************************************/

#ifndef MB_SERVICE_NOTIFICATION
#define MB_SERVICE_NOTIFICATION     0x00200000L
#endif

namespace MMgc
{
	static const bool logToStdErr = true;

	void GCDebugMsg(bool debuggerBreak, const char *format, ...)
	{
		// [ggrossman 09.24.04]
		// Changed this to _DEBUG only because we don't link to
		// CRT in Release builds, so vsprintf is unavailable!!
#if defined (_DEBUG) && !defined(UNDER_CE)
		va_list argptr;
		va_start(argptr, format);
		int bufferSize = _vscprintf(format, argptr)+1;

		char *buffer = (char*)alloca(bufferSize+2);
		if (buffer) {
			StringCbVPrintf(buffer, bufferSize+1, format, argptr);
			GCDebugMsg(buffer, debuggerBreak);
		}
#else
		(void)debuggerBreak;
		(void)format;
#endif
	}
	
	void GCDebugMsg(const char* msg, bool debugBreak)
	{
#ifndef UNDER_CE
		// !!@ only unicode
		OutputDebugStringA(msg);
#else
		DebugBreak();
#endif
		if(logToStdErr) {
 			fprintf( stderr, "%s\n", msg );
		}

		if (debugBreak) {
			DebugBreak();
		}
	}

	void GCDebugMsg(const wchar* msg, bool debugBreak)
	{
		OutputDebugStringW((LPCWSTR)msg);
		if(logToStdErr) {
 			fprintf( stderr, "%S\n", msg );
		}

		if (debugBreak) {
			DebugBreak();
		}
	}

}

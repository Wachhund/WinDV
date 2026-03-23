// stdafx.h : Precompiled header for WinDV.
//
// This file is included first by every .cpp translation unit (enforced by the
// VC6 project setting "Use precompiled header through stdafx.h").  Place only
// headers that are stable and changed infrequently here so that the compiler
// can cache their output and speed up incremental builds.
//

#if !defined(AFX_STDAFX_H__3D738ACC_5425_47A4_BE52_EE2CAC53FC62__INCLUDED_)
#define AFX_STDAFX_H__3D738ACC_5425_47A4_BE52_EE2CAC53FC62__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// Trim the Windows header set to the subset actually needed.
// This reduces compile time and avoids name collisions with DirectShow headers.
#define VC_EXTRALEAN		// Exclude rarely-used stuff from Windows headers

#include <afxwin.h>         // MFC core: CWinApp, CDialog, CWnd, message maps, etc.
#include <afxext.h>         // MFC extensions: CPropertySheet, CPropertyPage, toolbars, etc.
#include <afxdisp.h>        // MFC OLE Automation: COleDispatchDriver, VARIANT helpers
#include <afxdtctl.h>		// MFC wrappers for IE4+ common controls (date/time picker, etc.)
#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>			// MFC wrappers for Win32 common controls: CTabCtrl, CListCtrl, etc.
#endif // _AFX_NO_AFXCMN_SUPPORT

// DirectShow BaseClasses header (from Windows SDK Samples / Platform SDK).
// Provides IGraphBuilder, ICaptureGraphBuilder2, IMediaControl, REFERENCE_TIME,
// filter base classes (CBaseFilter, CBasePin), and the CHECK_HR / ThrowDShowException
// infrastructure used throughout DShow.cpp.
#include <streams.h>

// VC6 compatibility: INVALID_FILE_ATTRIBUTES not defined in older SDKs.
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#endif

#include <afxtempl.h>		// MFC template collections: CArray, CList, CMap
#include <afxmt.h>		    // MFC synchronisation primitives: CCritSec, CEvent, CAutoLock

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STDAFX_H__3D738ACC_5425_47A4_BE52_EE2CAC53FC62__INCLUDED_)

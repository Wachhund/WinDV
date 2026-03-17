// WinDV.h : main header file for the WinDV application
//
// Declares CWinDVApp (the CWinApp subclass) and the global SelectFile() helper.
// The application is dialog-based: InitInstance() creates and runs CDVToolsDlg
// as the sole main window and returns FALSE when the dialog closes.
//

#if !defined(AFX_WINDV_H__233DAD68_8B5D_4596_8330_07B05049E9F4__INCLUDED_)
#define AFX_WINDV_H__233DAD68_8B5D_4596_8330_07B05049E9F4__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
	#error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"		// main symbols

/* SelectFile -- open a common file dialog and place the result into a control.
 *
 * open   TRUE  => open dialog with OFN_ALLOWMULTISELECT; multiple paths are
 *                 joined with " | " and written to ctrl via SetWindowText().
 *        FALSE => save dialog; a single path is written to ctrl.
 * ctrl   The CWnd whose text receives the selected file path(s).
 *
 * Used by the main dialog and the record configuration page whenever the user
 * clicks a "..." browse button next to a file path edit control.
 */
void SelectFile(BOOL open, CWnd *ctrl);

/////////////////////////////////////////////////////////////////////////////
// CWinDVApp:
// See WinDV.cpp for the implementation of this class
//
// CWinDVApp is the MFC application object for WinDV.  Its only meaningful
// work is in InitInstance(), which:
//   - elevates the process to HIGH_PRIORITY_CLASS to reduce DV frame drops,
//   - initialises COM in multi-threaded apartment mode (required by DirectShow),
//   - sets the registry key root ("Petr Mourek") used by all GetProfileXxx /
//     WriteProfileXxx calls throughout the application, and
//   - creates and runs the modal CDVToolsDlg main window.
//

class CWinDVApp : public CWinApp
{
public:
	CWinDVApp();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CWinDVApp)
	public:
	virtual BOOL InitInstance();
	//}}AFX_VIRTUAL

// Implementation

	//{{AFX_MSG(CWinDVApp)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_WINDV_H__233DAD68_8B5D_4596_8330_07B05049E9F4__INCLUDED_)

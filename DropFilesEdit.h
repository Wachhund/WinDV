#if !defined(AFX_DROPFILESEDIT_H__B0489045_2D71_4D76_B7DC_EE835A73F6F0__INCLUDED_)
#define AFX_DROPFILESEDIT_H__B0489045_2D71_4D76_B7DC_EE835A73F6F0__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// DropFilesEdit.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CDropFilesEdit
//
// A CEdit subclass that accepts file drag-and-drop (WM_DROPFILES).
//
// When the user drops one or more files onto the control:
//   - If m_separator is non-empty, all dropped paths are accepted and joined
//     with m_separator into a single string (multi-file mode).  This is used
//     for the record source file list, where " | " separates paths.
//   - If m_separator is empty (NULL was passed to the constructor), only the
//     first dropped file is accepted (single-file mode).  This is used for the
//     capture destination file, where only one output path makes sense.
//
// Each accepted path is optionally passed through the m_filter callback before
// being added to the result.  If the callback returns false the file is silently
// skipped.  If no filter is provided the built-in NoFilter() stub accepts every
// path unconditionally.
//
// Usage in DVToolsDlg.cpp:
//   m_FSRC is constructed with separator " | " and no filter   => multi-file
//   m_FDST is constructed with no separator and CaptureFilenameExtractBase
//                                                               => single-file, strips extension
//
// The parent window must have WS_EX_ACCEPTFILES set (or call DragAcceptFiles(TRUE))
// for WM_DROPFILES to be delivered.
//

class CDropFilesEdit : public CEdit
{
// Construction
public:
	/* Constructor.
	 * multidrop_separator  String inserted between paths when multiple files are
	 *                      dropped at once.  Pass NULL (or omit) to accept only
	 *                      the first dropped file.
	 * filter               Optional callback invoked for each dropped path before
	 *                      it is appended to the result.  The callback receives the
	 *                      path by reference and may modify it (e.g. strip the
	 *                      extension).  Return true to accept, false to skip.
	 *                      Defaults to an internal no-op that always returns true.
	 */
	CDropFilesEdit(LPCSTR multidrop_separator=NULL, bool (*filter)(CString &) = NULL);

// Attributes
public:
	// String inserted between multiple dropped file paths.
	// Empty when the control is in single-file mode.
	CString m_separator;

	// Per-file filter/transform callback.  Never NULL after construction
	// (falls back to the internal NoFilter stub).
	bool (*m_filter)(CString &);

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CDropFilesEdit)
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CDropFilesEdit();

	// Generated message map functions
protected:
	//{{AFX_MSG(CDropFilesEdit)
	afx_msg void OnDropFiles(HDROP hDropInfo);
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_DROPFILESEDIT_H__B0489045_2D71_4D76_B7DC_EE835A73F6F0__INCLUDED_)

// DropFilesEdit.cpp : implementation file
//

#include "stdafx.h"
#include "DropFilesEdit.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/* NoFilter -- default path filter that accepts every file unconditionally.
 *
 * Used when the caller passes NULL as the filter argument to the constructor.
 * Having an actual function pointer rather than a NULL check in OnDropFiles
 * keeps the hot path branch-free.
 */
static bool NoFilter(CString &)
{
	return true;
}

/////////////////////////////////////////////////////////////////////////////
// CDropFilesEdit

/* Constructor.
 * Stores the separator and filter callback, substituting the no-op NoFilter
 * for a NULL filter so that m_filter is always a valid callable.
 */
CDropFilesEdit::CDropFilesEdit(LPCSTR multidrop_separator, bool (*filter)(CString &))
: m_separator(multidrop_separator), m_filter(filter)
{
	if (!m_filter) m_filter = NoFilter;
}

CDropFilesEdit::~CDropFilesEdit()
{
}


BEGIN_MESSAGE_MAP(CDropFilesEdit, CEdit)
	//{{AFX_MSG_MAP(CDropFilesEdit)
	ON_WM_DROPFILES()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CDropFilesEdit message handlers

/* OnDropFiles -- handle WM_DROPFILES delivered by the shell after a drag-drop.
 *
 * Single-file mode (m_separator is empty):
 *   Only the first dropped file (index 0) is queried.  The loop runs once
 *   regardless of how many files were actually dropped.
 *
 * Multi-file mode (m_separator is non-empty, e.g. " | "):
 *   DragQueryFile(-1, ...) returns the total count.  All files are iterated,
 *   each passed through m_filter.  Accepted paths are joined with m_separator.
 *
 * In both modes the assembled path string replaces the control's text only if
 * at least one file was accepted by the filter.  This prevents an accidental
 * drop of entirely-filtered files from blanking a previously set path.
 *
 * DragFinish() must be called in all code paths to release the HDROP handle.
 */
void CDropFilesEdit::OnDropFiles(HDROP hDropInfo)
{
	TCHAR buf[256];

	// Default to 1 so that single-file mode always reads index 0.
	int n = 1;

	if (!m_separator.IsEmpty()) {
		// Multi-file mode: query the actual count of dropped files.
		n = DragQueryFile(hDropInfo, -1, NULL, 0);
	}
	CString file, files;

	int i;
	for(i=0; i<n; i++) {
		DragQueryFile(hDropInfo, i, buf, sizeof buf / sizeof (TCHAR));
		file = buf;
		// The filter may modify 'file' in place (e.g. strip the extension).
		if (m_filter(file)) {
			if (!files.IsEmpty()) {
				files += m_separator;
			}
			files += file;
		}
	}
	DragFinish(hDropInfo);

	// Only update the control if at least one file passed the filter.
	if (!files.IsEmpty()) SetWindowText(files);
}

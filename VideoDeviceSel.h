#if !defined(AFX_VIDEODEVICESEL_H__5B9B5B7A_A184_467F_B190_5447284D9A80__INCLUDED_)
#define AFX_VIDEODEVICESEL_H__5B9B5B7A_A184_467F_B190_5447284D9A80__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// VideoDeviceSel.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CVideoDeviceSel
//
// Modal list-box dialog that lets the user pick a DV device by friendly name.
//
// The caller (CDVToolsDlg::OnVsrcSel / OnVdstSel) first enumerates available
// devices into a CArray<CString> via GetVideoSrcList() or GetVideoDstList()
// (defined in DShow.cpp), then constructs this dialog with that list and the
// name of the currently selected device so it can be pre-highlighted.
//
// On OK the selected index is stored in m_selected and can be retrieved via
// GetSelection().  The caller maps this index back to the device name through
// the original array.  If the user cancels, m_selected remains -1.
//
// Double-clicking a list entry is equivalent to pressing OK (OnDblclkDevlist
// delegates to OnOK).
//

class CVideoDeviceSel : public CDialog
{
// Construction
public:
	/* Constructor.
	 * list     Reference to the caller-owned array of device friendly names.
	 *          The array must remain valid for the lifetime of this dialog.
	 * selName  Friendly name of the currently active device, used to
	 *          pre-select the matching entry in the list box on init.
	 * pParent  Optional parent window.
	 */
	CVideoDeviceSel(CArray<CString,CString&> &list, LPCSTR selName, CWnd* pParent = NULL);

// Dialog Data
	//{{AFX_DATA(CVideoDeviceSel)
	enum { IDD = IDD_VIDEODEVICESEL };
	CListBox	m_listbox;
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CVideoDeviceSel)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	CArray<CString,CString&> *m_list;   // pointer to the caller-provided device list
	CString m_selName;                  // friendly name to pre-select on init
	int m_selected;                     // index of the item selected at close (-1 = none)

	// Generated message map functions
	//{{AFX_MSG(CVideoDeviceSel)
	afx_msg void OnDblclkDevlist();
	virtual void OnOK();
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

public:
	/* GetSelection -- returns the 0-based index of the selected device.
	 * Returns -1 if no item was selected (dialog was cancelled or the list
	 * was empty).  Valid only after DoModal() returns IDOK.
	 */
	int GetSelection() {return m_selected;}
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_VIDEODEVICESEL_H__5B9B5B7A_A184_467F_B190_5447284D9A80__INCLUDED_)

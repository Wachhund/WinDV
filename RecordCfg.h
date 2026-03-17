#if !defined(AFX_RECORDCFG_H__C6EDDD3E_4F72_4E92_939C_B69F736C5612__INCLUDED_)
#define AFX_RECORDCFG_H__C6EDDD3E_4F72_4E92_939C_B69F736C5612__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// RecordCfg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CRecordCfg
//
// CPropertyPage for record-specific settings, displayed as the second page
// of the configuration CPropertySheet opened from CDVToolsDlg::OnConfig().
//
// Settings controlled by this page:
//   m_aviPrefix       Pipe-delimited list of AVI files to play before the
//                     user-selected source files (leader / slate footage).
//                     May be empty.
//   m_aviSuffix       Pipe-delimited list of AVI files to play after the
//                     user-selected source files (trailer footage).
//                     May be empty.
//   m_recordPreview   TRUE to enable a live preview window while recording.
//
// The prefix and suffix fields are CDropFilesEdit controls (multi-file,
// " | " separator) so files can be dragged onto them from Explorer, and
// their "..." browse buttons (IDC_PREFIX_SEL, IDC_SUFFIX_SEL) call
// SelectFile() from WinDV.cpp.
//
// The assembled pipeline file string passed to CDV::BuildRecording() has the
// form:  <prefix> | <source files> | <suffix>  where any of the three
// sections may be empty.
//

class CRecordCfg : public CPropertyPage
{
// Construction
public:
	CRecordCfg();   // standard constructor

// Dialog Data
	//{{AFX_DATA(CRecordCfg)
	enum { IDD = IDD_RECORD_CONFIG };
	CDropFilesEdit	m_aviSuffixCtl;     // drag-drop edit for suffix AVI files
	CDropFilesEdit	m_aviPrefixCtl;     // drag-drop edit for prefix AVI files
	BOOL	m_recordPreview;            // enable preview during recording
	CString	m_aviPrefix;               // pipe-delimited prefix file list
	CString	m_aviSuffix;               // pipe-delimited suffix file list
	//}}AFX_DATA


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CRecordCfg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CRecordCfg)
	afx_msg void OnPrefixSel();   // "..." button for IDC_AVI_PREFIX
	afx_msg void OnSuffixSel();   // "..." button for IDC_AVI_SUFFIX
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_RECORDCFG_H__C6EDDD3E_4F72_4E92_939C_B69F736C5612__INCLUDED_)

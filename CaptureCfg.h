#if !defined(AFX_CAPTURECFG_H__88AB6830_E82C_4984_9B13_1C854FA927CE__INCLUDED_)
#define AFX_CAPTURECFG_H__88AB6830_E82C_4984_9B13_1C854FA927CE__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// CaptureCfg.h : header file
//

/////////////////////////////////////////////////////////////////////////////
// CCaptureCfg
//
// CPropertyPage for capture-specific settings, displayed as the first page
// of the configuration CPropertySheet opened from CDVToolsDlg::OnConfig().
//
// Settings controlled by this page:
//   m_type12              AVI output format: 0 = Type-1 (interleaved DV stream),
//                         1 = Type-2 (separate audio/video streams).
//   m_discontinuityTreshold  Maximum gap (in frames) in the incoming DV stream
//                         before a new output AVI file is started automatically.
//                         0 disables automatic splitting.
//   m_maxAVIFrames        Maximum number of DV frames per output AVI file.
//                         When the limit is reached a new file is opened.
//   m_everyNth            Capture only every Nth frame (1 = all frames).
//   m_dtformat            strftime-compatible format string used when building
//                         timestamped output filenames.
//   m_dtformathistory     Newline-delimited list of recently used format strings,
//                         shown as drop-down history in the format combo box.
//   m_ndigits             Number of zero-padded digits appended to the filename
//                         base as a sequence counter (0 = no counter).
//
// A 500 ms WM_TIMER (id=1) started in OnInitDialog drives the live filename
// preview label (IDC_FEXAMPLE) so the user can see the effect of format and
// digit-count changes in real time.
//

class CCaptureCfg : public CPropertyPage
{
// Construction
public:
	CCaptureCfg();   // standard constructor

// Dialog Data
	//{{AFX_DATA(CCaptureCfg)
	enum { IDD = IDD_CAPTURE_CONFIG };
	CStatic		m_fexample;         // live filename preview label
	CComboBox	m_dtformatctl;      // date-time format string combo (editable + history)
	CComboBox	m_ndigitsctl;       // sequence-counter digit count combo (0-4)
	UINT		m_discontinuityTreshold;
	UINT		m_everyNth;
	UINT		m_maxAVIFrames;
	int			m_type12;           // 0 = Type-1 AVI, 1 = Type-2 AVI
	CString		m_dtformat;         // current format string (from/to combo edit field)
	int			m_ndigits;          // current digit count selection index
	//}}AFX_DATA

	// History list is not in the AFX_DATA block because it is not bound to a
	// control directly; it is loaded into the combo as separate items.
	CString m_dtformathistory;


// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CCaptureCfg)
	public:
	virtual void OnOK();
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:

	// Generated message map functions
	//{{AFX_MSG(CCaptureCfg)
	afx_msg void OnTimer(UINT nIDEvent);
	virtual BOOL OnInitDialog();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_CAPTURECFG_H__88AB6830_E82C_4984_9B13_1C854FA927CE__INCLUDED_)

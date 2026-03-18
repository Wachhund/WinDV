// DVToolsDlg.h : header file
//

#if !defined(AFX_DVTOOLSDLG_H__0633EC27_A5A4_4B4B_8547_D2DE0ADC9AC9__INCLUDED_)
#define AFX_DVTOOLSDLG_H__0633EC27_A5A4_4B4B_8547_D2DE0ADC9AC9__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

/////////////////////////////////////////////////////////////////////////////
// CDVToolsDlg
//
// Main application dialog for WinDV.  Owns and orchestrates the entire
// capture/record session lifecycle through the embedded CDV pipeline object
// (m_video, bound to IDC_VIDEO).
//
// --- Layout and tab switching ---
// The dialog contains an owner-draw CToolTab (m_toolTab) with two pages:
//   Tab 0 — "Video Capture":  source DV device -> AVI file
//   Tab 1 — "Video Recording": AVI file(s) -> destination DV device
// Controls that belong to only one tab are shown/hidden via the tabMask field
// in ctrlProperties[] (see DVToolsDlg.cpp for details).
//
// --- Proportional resize ---
// The dialog is resizable.  Each control's position and size are recalculated
// on WM_SIZE using percentage-based anchor values stored in ctrlProperties[].
// The original dialog client rectangle and each control's original rect are
// captured in OnInitDialog() as the resize reference baseline.
//
// --- Status display ---
// Three status labels and a counter are updated every 200 ms by a WM_TIMER:
//   m_status   — human-readable state description (e.g. "Capturing...")
//   m_status2  — DV tape recording timestamp decoded from subcode data
//   m_status3  — ring-buffer queue load ("Q:N")
//   m_counter  — elapsed/remaining time in H:MM:SS.t format
//
// --- Registry persistence ---
// On close (OnClose) all settings are written to:
//   HKCU\Software\Petr Mourek\WinDV\MainWindow  — position, size, active tab
//   HKCU\Software\Petr Mourek\WinDV\Capture     — device name, AVI options
//   HKCU\Software\Petr Mourek\WinDV\Record      — device name, prefix/suffix
// They are restored in OnInitDialog() via the symmetric GetProfileXxx calls.
//
// --- Command-line automation ---
// When WinDV is started with a subcommand argument (see WinDV.cpp / CLAUDE.md),
// OnInitDialog() parses the command line and immediately starts the operation:
//   WinDV capture [-exit] [HH:]MM:SS[.us] <filename>
//   WinDV record  [-exit] <file1> [file2 ...]
// The -exit flag sets m_exitOnFinish so OnTimer() calls OnClose() automatically
// when the pipeline reaches CDV::Finished state.
//

class CDVToolsDlg : public CDialog
{
// Construction
public:
	CDVToolsDlg(CWnd* pParent = NULL);	// standard constructor
	virtual  ~CDVToolsDlg();

// Dialog Data
	//{{AFX_DATA(CDVToolsDlg)
	enum { IDD = IDD_DVTOOLS_DIALOG };
	CToolTab		m_toolTab;    // owner-draw tab control (Capture / Record)
	CButton			m_DVCtrl;     // checkbox to enable DV transport remote control
	CStatic			m_counter;    // elapsed/remaining time label (H:MM:SS.t)
	CStatic			m_status3;    // queue-load label ("Q:N")
	CStatic			m_status2;    // tape timestamp label (decoded from DV subcode)
	CDV				m_video;      // DirectShow pipeline object + preview window
	CStatic			m_VDST;       // record: destination DV device name display
	CStatic			m_VSRC;       // capture: source DV device name display
	CDropFilesEdit	m_FSRC;       // record: source AVI file(s) edit (multi-file, " | ")
	CDropFilesEdit	m_FDST;       // capture: destination AVI file edit (single-file)
	CStatic			m_status;     // primary status label
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CDVToolsDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	/* Exception2Status -- format a caught CException message and display it
	 * in m_status.  Called from CATCH_ALL blocks throughout this file. */
	void Exception2Status(CException *e);

	/* InitVideo -- reset the pipeline to the idle/ready state for the
	 * currently active tab.  For the Capture tab this builds a new capture
	 * graph and starts the 200 ms status timer.  For the Record tab it
	 * destroys any existing pipeline and prompts the user to select a file. */
	void InitVideo();

	/* SetToolTabItemSize -- resize all tab items proportionally so they fill
	 * the tab control width evenly, recalculated after every WM_SIZE. */
	void SetToolTabItemSize();

	HICON m_hIcon, m_hIconSmall;    // large and small application icons

	/* m_originalRects -- array of NControls RECT values capturing each
	 * control's position at the time the dialog was first displayed.
	 * Used as the baseline for all subsequent proportional resize calculations.
	 * Allocated in OnInitDialog(), freed in the destructor. */
	RECT *m_originalRects;

	/* m_originalRect -- client-area size of the dialog at first display.
	 * OnSize() compares the current size against this to compute the delta. */
	RECT m_originalRect;

	/* m_lastRect -- most recent non-iconic, non-maximised window rect.
	 * Updated in OnMove() and OnSize().  Written to the registry in OnClose()
	 * so the window is restored to the same position/size on next launch. */
	RECT m_lastRect;

	int m_minWidth, m_minHeight;    // minimum window size enforced by OnGetMinMaxInfo()

	/* tabChangeBtns -- invisible CButton array (one per tab) created in
	 * OnInitDialog().  Their IDs fall in the IDC_TAB_CHANGE range so that
	 * accelerator-key tab switching can be routed through OnCmdTabChange(). */
	CButton *tabChangeBtns;

	CString m_VSRCname;   // friendly name of the selected capture source DV device
	CString m_VDSTname;   // friendly name of the selected record destination DV device

	// AVI prefix/suffix file lists used as leader/trailer footage in record mode.
	// Stored as pipe-delimited strings matching the CDropFilesEdit format.
	CString m_AVIPrefix, m_AVISuffix;

	CString m_DTFormat;         // active strftime format string for capture filenames
	CString m_DTFormatHistory;  // newline-delimited history of recently used format strings
	int m_nSuffixDigits;        // zero-padded sequence counter digit count (0 = none)

	// State enumeration mirrors CDV::State.  Iddle (sic) is the original spelling.
	enum {Iddle, CapturePaused, Capturing, RecordPaused, Recording};

	/* m_exitOnFinish -- when true the dialog calls OnClose() automatically
	 * once the pipeline reaches CDV::Finished.  Set by the -exit command-line
	 * flag to support unattended capture/record automation. */
	bool m_exitOnFinish;

	/* OnCmdTabChange -- handles WM_COMMAND messages in the IDC_TAB_CHANGE
	 * range generated by the invisible tabChangeBtns.  Switches the active
	 * tab programmatically if the tab control is enabled. */
	afx_msg void OnCmdTabChange(UINT nID);

	/* OnDVTimeChange -- handles the custom WM_DV_TIMECHANGE message posted by
	 * the CDV pipeline when a new DV subcode timestamp is decoded.  The LPARAM
	 * carries the time_t timestamp which is formatted and written to m_status2. */
	afx_msg LRESULT OnDVTimeChange(WPARAM, LPARAM);

	/* OnDVLowDiskSpace -- handles WM_DV_LOWDISKSPACE posted by CapturingThread
	 * when free disk space drops below 500 MB. Shows warning in status bar. */
	afx_msg LRESULT OnDVLowDiskSpace(WPARAM, LPARAM);

	// Generated message map functions
	//{{AFX_MSG(CDVToolsDlg)
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI);
	afx_msg void OnSelchangeToolTab(NMHDR* pNMHDR, LRESULT* pResult);
	virtual void OnCancel();
	afx_msg void OnClose();
	virtual void OnOK();
	afx_msg void OnVsrcSel();
	afx_msg void OnVdstSel();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnMove(int x, int y);
	afx_msg void OnFdstSel();
	afx_msg void OnFsrcSel();
	afx_msg void OnConfig();
	afx_msg void OnCapture();
	afx_msg void OnRecord();
	afx_msg void OnTimer(UINT nIDEvent);
	afx_msg void OnPicture();
	afx_msg void OnDvctrl();
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_DVTOOLSDLG_H__0633EC27_A5A4_4B4B_8547_D2DE0ADC9AC9__INCLUDED_)

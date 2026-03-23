// CaptureCfg.cpp : implementation file
//

#include "stdafx.h"
#include "WinDV.h"
#include "CaptureCfg.h"
#include "DShow.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CCaptureCfg dialog

/* Constructor -- initialises all data members to safe sentinel values.
 * The real values are injected by CDVToolsDlg::OnConfig() before DoModal()
 * is called, and read back after DoModal() returns IDOK.
 */
CCaptureCfg::CCaptureCfg()
	: CPropertyPage(CCaptureCfg::IDD)
{
	//{{AFX_DATA_INIT(CCaptureCfg)
	m_discontinuityTreshold = 0;
	m_everyNth = 0;
	m_maxAVIFrames = 0;
	m_type12 = -1;
	m_dtformat = _T("");
	m_ndigits = -1;
	//}}AFX_DATA_INIT
	m_autoStopEnabled = TRUE;
	m_autoStopSeconds = 5;
}


void CCaptureCfg::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CCaptureCfg)
	DDX_Control(pDX, IDC_FEXAMPLE, m_fexample);
	DDX_Control(pDX, IDC_DTFORMAT, m_dtformatctl);
	DDX_Control(pDX, IDC_NDIGITS, m_ndigitsctl);
	DDX_Text(pDX, IDC_DISCONTINUITY_TRESHOLD, m_discontinuityTreshold);
	DDV_MinMaxUInt(pDX, m_discontinuityTreshold, 0, 1000000);
	DDX_Text(pDX, IDC_EVERY_NTH, m_everyNth);
	DDV_MinMaxUInt(pDX, m_everyNth, 1, 1000000);
	DDX_Text(pDX, IDC_MAX_FRAMES, m_maxAVIFrames);
	DDV_MinMaxUInt(pDX, m_maxAVIFrames, 10, UINT_MAX);
	DDX_Radio(pDX, IDC_TYPE_1, m_type12);
	DDX_CBString(pDX, IDC_DTFORMAT, m_dtformat);
	DDX_CBIndex(pDX, IDC_NDIGITS, m_ndigits);
	//}}AFX_DATA_MAP
	DDX_Check(pDX, IDC_CHK_AUTOSTOP, m_autoStopEnabled);
	DDX_Text(pDX, IDC_EDIT_AUTOSTOP, m_autoStopSeconds);
	DDV_MinMaxUInt(pDX, m_autoStopSeconds, 1, 60);
}


BEGIN_MESSAGE_MAP(CCaptureCfg, CPropertyPage)
	//{{AFX_MSG_MAP(CCaptureCfg)
	ON_WM_TIMER()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CCaptureCfg message handlers

/* OnTimer -- update the live filename preview once every 500 ms (timer id=1).
 *
 * Reads the current text of the format combo (which may differ from m_dtformat
 * if the user is mid-edit) and calls FormatTime() (DShow.cpp) with the current
 * wall-clock time to produce the timestamp portion.  The digit-count suffix is
 * appended as ".000...0" with as many digits as the combo selection.  The
 * complete example filename ("...example.<timestamp>.<counter>.avi") is written
 * to the m_fexample static label so the user gets immediate visual feedback.
 *
 * Timer id=1 is the only expected id; any other id is forwarded to the base class.
 */
void CCaptureCfg::OnTimer(UINT nIDEvent)
{
	if (nIDEvent == 1) {
		CString tmp;
		m_dtformatctl.GetWindowText(tmp);
		// FormatTime() applies strftime formatting using the provided timestamp.
		CString tmp2 = FormatTime(tmp, time(NULL));
		tmp = "...example";
		if (!tmp2.IsEmpty()) {
			tmp += ".";
			tmp += tmp2;
		}
		// Append the zero-padded sequence counter suffix (e.g. ".00" for 2 digits).
		int n = m_ndigitsctl.GetCurSel();
		tmp2.Empty();
		if (n) tmp2.Format(".%0*d", n, 0);
		m_fexample.SetWindowText(tmp+tmp2+".avi");
	}
	else {
		CPropertyPage::OnTimer(nIDEvent);
	}
}

/* OnInitDialog -- populate controls and start the live preview timer.
 *
 * Digit-count combo: items "0" through "4" are added in order so that the
 * combo index directly equals the digit count (index 0 = no counter, etc.).
 *
 * Format history combo: m_dtformat is added first so it appears as the
 * selected item, then m_dtformathistory (newline-separated) is split and
 * each entry is appended.  An empty string is appended last as a convenient
 * way to clear the field.
 *
 * Timer 1 is started at 500 ms to drive OnTimer() for live preview updates.
 * OnTimer(1) is called immediately before starting the timer so the preview
 * is populated at once rather than after the first 500 ms delay.
 */
BOOL CCaptureCfg::OnInitDialog()
{
	CPropertyPage::OnInitDialog();

	// Populate digit-count combo: index == digit count (0 to 4).
	CString tmp;
	int i;
	for(i = 0; i<=4; i++) {
		tmp.Format("%d", i);
		m_ndigitsctl.AddString(tmp);
	}

	// Add the current format string as the first (selected) combo entry.
	m_dtformatctl.AddString(m_dtformat);

	// Parse the newline-delimited history and add each non-empty entry.
	while (!m_dtformathistory.IsEmpty()) {
		tmp = m_dtformathistory.SpanExcluding("\n");
		int l = tmp.GetLength();
		if (l < m_dtformathistory.GetLength())
			m_dtformathistory = m_dtformathistory.Mid(l+1);
		else
			m_dtformathistory.Empty();

		m_dtformatctl.AddString(tmp);
	}
	// Append a blank entry so the user can easily clear the format field.
	if (!m_dtformat.IsEmpty()) m_dtformatctl.AddString("");

	m_ndigitsctl.SetCurSel(m_ndigits);

	// Populate the preview immediately, then arm the 500 ms refresh timer.
	OnTimer(1);
	SetTimer(1, 500, NULL);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

/* OnOK -- save the format history before the property page is dismissed.
 *
 * The current m_dtformat entry is removed from the combo list (if present)
 * to avoid duplication, then up to 10 non-empty combo items are serialised
 * into m_dtformathistory as a newline-delimited string.  The caller
 * (CDVToolsDlg::OnConfig) reads m_dtformathistory back and persists it to
 * the registry.
 */
void CCaptureCfg::OnOK()
{
	CPropertyPage::OnOK();

	// Remove the current format from the list to avoid it appearing twice.
	int i = m_dtformatctl.FindStringExact(-1, m_dtformat);
	if (i>=0) m_dtformatctl.DeleteString(i);

	m_dtformathistory.Empty();
	int n = m_dtformatctl.GetCount();
	if (n > 10) n = 10;   // cap history at 10 entries

	for (i=0; i<n; i++) {
		CString tmp;
		m_dtformatctl.GetLBText(i, tmp);
		if (!tmp.IsEmpty()) {
			if (!m_dtformathistory.IsEmpty()) m_dtformathistory += "\n";
			m_dtformathistory += tmp;
		}
	}
}

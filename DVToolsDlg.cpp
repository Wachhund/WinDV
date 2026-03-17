// DVToolsDlg.cpp : implementation file
//
// Implements CDVToolsDlg, the main application dialog, and the local
// CAboutDlg helper dialog.
//

#include "stdafx.h"
#include "process.h"
#include "WinDV.h"
#include "DropFilesEdit.h"
#include "DShow.h"
#include "ToolTab.h"
#include "DVToolsDlg.h"
#include "VideoDeviceSel.h"
#include "CaptureCfg.h"
#include "RecordCfg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About
//
// Simple About dialog showing author contact information.  The email and URL
// controls are drawn in blue (OnCtlColor) and use the hand cursor (OnSetCursor)
// to look like hyperlinks.  Activation opens the system default mail client or
// browser on a background thread so the UI is not blocked.
//

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	afx_msg void OnEmail();
	afx_msg void OnUrl();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT

}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
	ON_COMMAND(IDC_EMAIL, OnEmail)
	ON_COMMAND(IDC_URL, OnUrl)
	ON_WM_CTLCOLOR()
	ON_WM_SETCURSOR()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/* UrlThread -- fire-and-forget thread that opens a URL or mailto: URI via the
 * shell default handler.  Called from _beginthread() so the UI remains responsive
 * while the browser or mail client launches.
 * ptr  Pointer to a string literal; no ownership transfer required.
 */
void UrlThread(void *ptr)
{
	ShellExecute(NULL, "open", (char *)ptr, NULL, NULL, SW_SHOWNORMAL);
}

/* OnEmail -- launch the default mail client addressed to the author. */
void CAboutDlg::OnEmail()
{
	_beginthread(UrlThread, 0, "mailto:petr@mourek.cz?subject=WinDV");
}

/* OnUrl -- open the WinDV project website in the default browser. */
void CAboutDlg::OnUrl()
{
	_beginthread(UrlThread, 0, "http://windv.mourek.cz/");
}

/* OnCtlColor -- render the email and URL controls in blue to mimic hyperlinks. */
HBRUSH CAboutDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CDialog::OnCtlColor(pDC, pWnd, nCtlColor);

	switch (pWnd->GetDlgCtrlID()) {
	case IDC_EMAIL:
	case IDC_URL:
		pDC->SetTextColor(RGB(0,0,192));
	}

	return hbr;
}

/* OnSetCursor -- show the hand cursor (system cursor 32649) over the OK
 * button and both hyperlink controls to reinforce the clickable appearance.
 */
BOOL CAboutDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	switch (pWnd->GetDlgCtrlID()) {
	case IDOK:
	case IDC_EMAIL:
	case IDC_URL:
		SetCursor(LoadCursor(NULL, MAKEINTRESOURCE(32649)));
		return TRUE;
	}


	return CDialog::OnSetCursor(pWnd, nHitTest, message);
}

/////////////////////////////////////////////////////////////////////////////
// CDVToolsDlg dialog

// Base command ID for the invisible tab-change button array.
// Buttons are created at IDs IDC_TAB_CHANGE+0, IDC_TAB_CHANGE+1, ...
// and the ON_COMMAND_RANGE macro routes them to OnCmdTabChange().
#define IDC_TAB_CHANGE 0x100

// Tab index constants used with ctrlProperties[].tabMask.
// TAB_ALL  (-1 / all bits set) means the control is always visible.
// TAB_NONE (0)  means never visible (reserved, not currently used).
// TAB_CAPTURE (1) and TAB_RECORD (2) correspond to tab indices 0 and 1;
// the mask is tested as (1 << tabIndex) & tabMask.
enum {TAB_ALL=-1, TAB_NONE=0, TAB_CAPTURE=1, TAB_RECORD=2};

// Percentage anchor constants used in ctrlProperties[].
// XL = 25%: controls whose left/right edge tracks one-quarter of the dialog width change.
// XR = 75%: controls whose left/right edge tracks three-quarters.
// dy/dh values of 100 mean the control's vertical edges track 100% of height change,
// which keeps them pinned to the bottom of the dialog.
#define XL 25
#define XR 75

/* ctrlProperties[] -- proportional layout table for all resizable controls.
 *
 * Each entry describes one dialog control and how its position and size should
 * change as the dialog is resized.  The five integer fields are percentages:
 *
 *   dx   How much of the horizontal size delta is added to the control's left edge.
 *        0  = left edge is fixed (anchored to the left side of the dialog).
 *        100 = left edge moves with the right side (anchored to the right).
 *        XL/XR = partial anchor — used for controls that straddle the centre.
 *
 *   dw   How much of the horizontal size delta is added to the control's right edge.
 *        When dw > dx the control grows wider as the dialog widens.
 *        When dw == dx the control's width stays constant but it may translate.
 *
 *   dy   Same as dx but for the vertical direction (top edge).
 *   dh   Same as dw but for the vertical direction (bottom edge).
 *
 * The new position is computed in OnSize() as:
 *   new_left   = originalLeft   + (dx * deltaX) / 100
 *   new_top    = originalTop    + (dy * deltaY) / 100
 *   new_width  = originalWidth  + ((dw - dx) * deltaX) / 100
 *   new_height = originalHeight + ((dh - dy) * deltaY) / 100
 *
 * tabMask controls visibility on tab switches (see the TAB_* constants above).
 */
static struct CtrlProperties {int id; int dx, dw, dy, dh; int tabMask;} ctrlProperties[] =
{
	// Video preview fills the entire client area (0%..100% in both axes).
	{IDC_VIDEO,		  0,100,  0,100,	TAB_ALL},
	// About picture: pinned to bottom-right corner.
	{IDC_PICTURE,	 XR, XR,100,100,	TAB_ALL},
	// Tab control: stretches between the 25% and 75% horizontal anchors.
	{IDC_TOOL_TAB,	 XL, XR,100,100,	TAB_ALL},

	// Capture tab: source device row (label fixed-width at 25%, display stretches, button at right).
	{IDC_VSRC_L,	 XL, XL,100,100,	TAB_CAPTURE},
	{IDC_VSRC,		 XL, XR,100,100,	TAB_CAPTURE},
	{IDC_VSRC_SEL,	 XR, XR,100,100,	TAB_CAPTURE},

	// Record tab: source file row.
	{IDC_FSRC_L,	 XL, XL,100,100,	TAB_RECORD},
	{IDC_FSRC,		 XL, XR,100,100,	TAB_RECORD},
	{IDC_FSRC_SEL,	 XR, XR,100,100,	TAB_RECORD},

	// Capture tab: destination file row.
	{IDC_FDST_L,	 XL, XL,100,100,	TAB_CAPTURE},
	{IDC_FDST,		 XL, XR,100,100,	TAB_CAPTURE},
	{IDC_FDST_SEL,	 XR, XR,100,100,	TAB_CAPTURE},

	// Record tab: destination device row.
	{IDC_VDST_L,	 XL, XL,100,100,	TAB_RECORD},
	{IDC_VDST,		 XL, XR,100,100,	TAB_RECORD},
	{IDC_VDST_SEL,	 XR, XR,100,100,	TAB_RECORD},

	// Config button: visible on both tabs, pinned to the right.
	{IDC_CONFIG,	 XR, XR,100,100,	TAB_CAPTURE | TAB_RECORD},
	// DV transport control checkbox: always visible.
	{IDC_DVCTRL,	 XR, XR,100,100,	TAB_ALL},
	// Action buttons: each visible on its own tab only.
	{IDC_CAPTURE,	 XR, XR,100,100,	TAB_CAPTURE},
	{IDC_RECORD,	 XR, XR,100,100,	TAB_RECORD},
	// Cancel button: visible on both active tabs.
	{IDCANCEL,		 XR, XR,100,100,	TAB_CAPTURE | TAB_RECORD},
	// Status / counter row: always visible.
	{IDC_COUNTER,	 XR, XR,100,100,	TAB_ALL},
	{IDC_STATUS,	 XL, XR,100,100,	TAB_ALL},
	{IDC_STATUS2,	 XR, XR,100,100,	TAB_ALL},
	{IDC_STATUS3,	 XR, XR,100,100,	TAB_ALL}
};

// Number of entries in ctrlProperties[].
#define NControls (sizeof ctrlProperties/sizeof (struct CtrlProperties))

/* CaptureFilenameExtractBase -- strip the file extension from a capture filename.
 *
 * Used as the m_filter callback for m_FDST (the capture destination edit).
 * When a file is dropped or browsed the extension is removed so the user sees
 * the base name; CDV::StartCapturing() appends its own timestamped extension.
 *
 * file     Input path; modified in place to remove everything from the first
 *          '.' character onward.
 * Returns  true if the resulting base name is non-empty; false otherwise
 *          (which causes CDropFilesEdit to ignore the file).
 */
bool CaptureFilenameExtractBase(CString &file)
{
	int pos = file.Find(".");
	if (pos>=0) file = file.Mid(0, pos);
	return ! file.IsEmpty();
}


/* Constructor -- initialises the two CDropFilesEdit controls with their
 * respective modes before the dialog is created:
 *   m_FSRC: multi-file with " | " separator (record source files).
 *   m_FDST: single-file with CaptureFilenameExtractBase filter (capture destination).
 * Also loads both application icons and zeroes resize bookkeeping state.
 */
CDVToolsDlg::CDVToolsDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CDVToolsDlg::IDD, pParent), m_FSRC(" | "), m_FDST(NULL, CaptureFilenameExtractBase)
{
	//{{AFX_DATA_INIT(CDVToolsDlg)
	//}}AFX_DATA_INIT

	// Load both icon sizes explicitly; the framework only loads one automatically
	// when the main window is not a dialog.
	m_hIcon      = (HICON)LoadImage(AfxGetResourceHandle(),
	                         MAKEINTRESOURCE(IDR_MAINFRAME),
							 IMAGE_ICON,
							 0, 0, LR_DEFAULTSIZE);
	m_hIconSmall = (HICON)LoadImage(AfxGetResourceHandle(),
	                         MAKEINTRESOURCE(IDR_MAINFRAME),
							 IMAGE_ICON,
							 16, 16, 0);

	tabChangeBtns = NULL;
	m_originalRects = NULL;
	m_originalRect.right = 0;      // sentinel: 0 means resize data not yet initialised
	m_minWidth = m_minHeight = 1;
	m_exitOnFinish = 0;
}

CDVToolsDlg::~CDVToolsDlg()
{
	delete[] tabChangeBtns;
	delete[] m_originalRects;
}

void CDVToolsDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CDVToolsDlg)
	DDX_Control(pDX, IDC_TOOL_TAB, m_toolTab);
	DDX_Control(pDX, IDC_DVCTRL, m_DVCtrl);
	DDX_Control(pDX, IDC_COUNTER, m_counter);
	DDX_Control(pDX, IDC_STATUS3, m_status3);
	DDX_Control(pDX, IDC_STATUS2, m_status2);
	DDX_Control(pDX, IDC_VIDEO, m_video);
	DDX_Control(pDX, IDC_VDST, m_VDST);
	DDX_Control(pDX, IDC_VSRC, m_VSRC);
	DDX_Control(pDX, IDC_FSRC, m_FSRC);
	DDX_Control(pDX, IDC_FDST, m_FDST);
	DDX_Control(pDX, IDC_STATUS, m_status);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CDVToolsDlg, CDialog)
	//{{AFX_MSG_MAP(CDVToolsDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_SIZE()
	ON_WM_GETMINMAXINFO()
	ON_NOTIFY(TCN_SELCHANGE, IDC_TOOL_TAB, OnSelchangeToolTab)
	ON_WM_CLOSE()
	ON_BN_CLICKED(IDC_VSRC_SEL, OnVsrcSel)
	ON_BN_CLICKED(IDC_VDST_SEL, OnVdstSel)
	ON_WM_CTLCOLOR()
	ON_WM_MOVE()
	ON_BN_CLICKED(IDC_FDST_SEL, OnFdstSel)
	ON_BN_CLICKED(IDC_FSRC_SEL, OnFsrcSel)
	ON_BN_CLICKED(IDC_CONFIG, OnConfig)
	ON_BN_CLICKED(IDC_CAPTURE, OnCapture)
	ON_BN_CLICKED(IDC_RECORD, OnRecord)
	ON_WM_TIMER()
	ON_BN_CLICKED(IDC_PICTURE, OnPicture)
	ON_BN_CLICKED(IDC_DVCTRL, OnDvctrl)
	//}}AFX_MSG_MAP
	// Route IDC_TAB_CHANGE+0 .. IDC_TAB_CHANGE+99 to OnCmdTabChange.
	ON_COMMAND_RANGE(IDC_TAB_CHANGE, IDC_TAB_CHANGE+99, OnCmdTabChange)
	// Custom message posted by the CDV pipeline when a DV subcode timestamp changes.
	ON_MESSAGE(WM_DV_TIMECHANGE, OnDVTimeChange)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CDVToolsDlg message handlers

/* OnInitDialog -- one-time dialog initialisation.
 *
 * Performs the following in order:
 *
 * 1. System menu: appends an "About..." entry so the About dialog is accessible
 *    from the title bar context menu.
 *
 * 2. Icons: assigns both large and small application icons.
 *
 * 3. Resize baseline: records the initial client rect (m_originalRect) and the
 *    initial window rect (used to derive minimum size).  Allocates m_originalRects[]
 *    and captures each control's current client-relative rect as the resize origin.
 *
 * 4. Tab items: inserts "Video Capture" and "Video Recording" tabs and creates
 *    corresponding invisible CButton objects in tabChangeBtns[] at IDs
 *    IDC_TAB_CHANGE+0 and IDC_TAB_CHANGE+1.  These buttons are never shown; their
 *    sole purpose is to give MFC accelerator routing a WM_COMMAND target for
 *    programmatic tab switching.
 *
 * 5. Registry restore: reads window position/size, DV control state, working
 *    directory, device names, file paths, and all capture/record settings from
 *    HKCU\Software\Petr Mourek\WinDV.  If no saved size is found the About
 *    dialog is shown via PostMessage on first launch.
 *
 * 6. Command-line parsing: if m_lpCmdLine is non-empty the first token selects
 *    a subcommand ("capture" or "record").  The optional "-exit" flag sets
 *    m_exitOnFinish.  Parsing errors display the IDS_USAGE string and exit.
 *
 *    Capture subcommand duration format:
 *      The duration is a variable-field time string: [HH:]MM:SS[.us]
 *      where HH, MM, SS are integers and us is up to 6 fractional digits
 *      (microseconds, where 1000000 us = 1 s).  A bare integer is treated as
 *      seconds.  The parser uses a goto-based state machine: it accumulates
 *      the first numeric run into hh, then branches on the following delimiter
 *      character (':' vs end/'.') to decide whether it is HH:MM:SS or MM:SS or
 *      just SS.  The timusec label handles the optional '.us' tail.
 *      REFERENCE_TIME is a 100-nanosecond unit, so the final conversion is:
 *        t = ((hh*60 + mi)*60 + ss) * 10,000,000  +  us_fractional * 10
 *
 *    Record subcommand: all remaining tokens become the pipe-delimited file list
 *    passed to CDV::BuildRecording(), prepended/appended with m_AVIPrefix and
 *    m_AVISuffix.
 *
 * 7. If no command-line subcommand was given, OnSelchangeToolTab() is called
 *    to initialise the pipeline for whichever tab was last active.
 */
BOOL CDVToolsDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIconSmall, FALSE);		// Set small icon

	// Capture the initial client size as the reference for all resize calculations.
	GetClientRect(&m_originalRect);
	GetWindowRect(&m_lastRect);

	// Derive minimum window size from the initial window dimensions.
	m_minWidth = m_lastRect.right - m_lastRect.left;
	m_minHeight = m_lastRect.bottom - m_lastRect.top;

	m_originalRects = new RECT[NControls];
	int pgNo = 0;
	CString tmp;
	RECT rc = {0,0,0,0};
	tabChangeBtns = new CButton[2];

	// Create tab pages and their associated invisible command buttons.
	tmp.LoadString(IDS_TAB_VIDEO_CAPTURE);
	tabChangeBtns[pgNo].Create(tmp, WS_CHILD, rc, this, pgNo + IDC_TAB_CHANGE);
	m_toolTab.InsertItem(pgNo++, tmp);
	tmp.LoadString(IDS_TAB_VIDEO_RECORDING);
	tabChangeBtns[pgNo].Create(tmp, WS_CHILD, rc, this, pgNo + IDC_TAB_CHANGE);
	m_toolTab.InsertItem(pgNo++, tmp);

	// Record each control's initial screen-to-client rect as the resize baseline.
	{
		int i;
		for(i=0; i<NControls; i++)
		{
			GetDlgItem(ctrlProperties[i].id)->GetWindowRect(&m_originalRects[i]);
			ScreenToClient(&m_originalRects[i]);
		}
	}

	SetToolTabItemSize();

	// --- Registry restore ---
	// Window geometry.
	int wx, wy, ww, wh;
	wx = AfxGetApp()->GetProfileInt("MainWindow", "X", 0);
	wy = AfxGetApp()->GetProfileInt("MainWindow", "Y", 0);
	ww = AfxGetApp()->GetProfileInt("MainWindow", "W", 0);
	wh = AfxGetApp()->GetProfileInt("MainWindow", "H", 0);

	// DV transport control state.
	m_video.m_DVctrl = AfxGetApp()->GetProfileInt("MainWindow", "DVControlEnabled", m_video.m_DVctrl) > 0;
	m_DVCtrl.SetCheck(m_video.m_DVctrl);

	// Restore working directory so relative file paths resolve correctly.
	CString wdir = AfxGetApp()->GetProfileString("MainWindow", "WorkingDirectory", ".");
	SetCurrentDirectory(wdir);

	if (ww && wh)
		SetWindowPos(NULL, wx, wy, ww, wh, SWP_NOZORDER);
	else
		// First run: show the About dialog as a welcome/help hint.
		PostMessage(WM_SYSCOMMAND, IDM_ABOUTBOX, 0);

	// Device names.
	m_VSRCname = AfxGetApp()->GetProfileString("Capture", "DVDevice", "Microsoft DV Camera and VCR");
	m_VDSTname = AfxGetApp()->GetProfileString("Record", "DVDevice", "Microsoft DV Camera and VCR");
	m_VSRC.SetWindowText(m_VSRCname);
	m_VDST.SetWindowText(m_VDSTname);

	// File paths.
	m_FSRC.SetWindowText(AfxGetApp()->GetProfileString("Record", "File", ""));
	m_FDST.SetWindowText(AfxGetApp()->GetProfileString("Capture", "File", ""));

	// Active tab.
	m_toolTab.SetCurSel(AfxGetApp()->GetProfileInt("MainWindow", "SelectedTool", 0));

	// Record prefix/suffix and preview flag.
	m_AVIPrefix = AfxGetApp()->GetProfileString("Record", "AVIPrefix", "");
	m_AVISuffix = AfxGetApp()->GetProfileString("Record", "AVISuffix", "");
	m_video.m_recordPreview = AfxGetApp()->GetProfileInt("Record", "Preview", m_video.m_recordPreview) > 0;

	// Capture options.
	m_video.m_type2AVI = AfxGetApp()->GetProfileInt("Capture", "Type2AVI", m_video.m_type2AVI) > 0;
	m_video.m_discontinuityTreshold = AfxGetApp()->GetProfileInt("Capture", "DiscontinuityTreshold", m_video.m_discontinuityTreshold);
	m_video.m_maxAVIFrames = AfxGetApp()->GetProfileInt("Capture", "MaxAVIFrames", m_video.m_maxAVIFrames);
	m_video.m_everyNth = AfxGetApp()->GetProfileInt("Capture", "EveryNth", m_video.m_everyNth);

	// Filename date-time format and sequence counter settings.
	m_DTFormat = AfxGetApp()->GetProfileString("Capture", "DateTimeFormat", "%y-%m-%d_%H-%M");
	m_DTFormatHistory = AfxGetApp()->GetProfileString("Capture", "DateTimeFormatHistory", "%y-%m-%d_%H-%M-%S\n%Y-%m-%d_%H-%M\n%Y-%m-%d_%H-%M-%S\n%Y%m%d-%H%M%S\n%a_%H-%M-%S");
	m_nSuffixDigits = AfxGetApp()->GetProfileInt("Capture", "SuffixDigits", 2);

	// --- Command-line parsing ---
	// Whitespace delimiters for strtok-based tokenisation.
	char *delim = " \t\n";
	CString err;
	LPSTR arg = strtok(AfxGetApp()->m_lpCmdLine, delim);
	if (arg) {
		if (strcmp(arg,"capture")==0) {
			// Switch to the Capture tab and apply it before starting.
			m_toolTab.SetCurSel(0);
			LRESULT result;
			OnSelchangeToolTab(NULL, &result);
			arg = strtok(NULL, delim);

			// Optional -exit flag: close the dialog when the pipeline finishes.
			if (arg && strcmp(arg,"-exit")==0) {
				m_exitOnFinish = 1;
				arg = strtok(NULL, delim);
			}
			if (!arg) {
				CString tmp; tmp.LoadString(IDS_USAGE);
				err += tmp;
			}
			else {
				// --- Duration parser ---
				// Accumulate the first run of digits into hh (could be HH, MM, or SS).
				int hh = 0, mi = 0, ss = 0, us = 0;
				for(;isdigit(*arg);arg++) {
					hh = hh * 10 + (*arg - '0');
				}
				if (*arg == 0 || *arg == '.') {
					// Single numeric field: treat as plain seconds.
					ss = hh; hh = 0;
					goto timusec;
				}
				if (*arg == ':') arg++;
				else goto timerr;
				// Second numeric run: could be MM (in HH:MM:SS) or SS (in MM:SS).
				for(;isdigit(*arg);arg++) {
					mi = mi * 10 + (*arg - '0');
				}
				if (*arg == 0 || *arg == '.') {
					// Two fields only: MM:SS — reassign.
					ss = mi; mi = hh; hh = 0;
					goto timusec;
				}
				if (*arg == ':') arg++;
				else goto timerr;
				// Third numeric run: definitely SS in HH:MM:SS.
				for(;isdigit(*arg);arg++) {
					ss = ss * 10 + (*arg - '0');
				}
timusec:
				// Optional microsecond fraction after '.'.
				// Digits are weighted from 100000 downward (6 significant digits).
				if (*arg) {
					if (*arg == '.') arg++;
					else goto timerr;

					int i = 1000000;
					for(;isdigit(*arg);arg++) {
						us += i*(*arg - '0');
						i /= 10;
					}
					if (*arg) goto timerr;
				}

				arg = strtok(NULL, delim);
				if (!arg) {
timerr:
					CString tmp; tmp.LoadString(IDS_USAGE);
					err += tmp;
				}
				else {
					// Convert hh:mi:ss.us to REFERENCE_TIME (100-nanosecond units).
					REFERENCE_TIME t = (hh*60+mi)*60+ss;
					t = t * 10000000 + us;

					CString file = arg;
					arg = strtok(NULL, delim);
					if (arg) {
						// Extra argument: usage error.
						CString tmp; tmp.LoadString(IDS_USAGE);
						err += tmp;
					}
					else {
						TRY {
							m_FDST.SetWindowText(file);
							m_video.BuildCapturing(m_VSRCname);
							m_video.StartCapturing(file, m_DTFormat, m_nSuffixDigits, t);
							SetTimer(1, 200, NULL);
						}
						CATCH_ALL(e) {
							InitVideo();
							Exception2Status(e);
						}
						END_CATCH_ALL;
					}
				}
			}
		}
		else if (strcmp(arg,"record")==0) {
			// Switch to the Record tab and apply it before starting.
			m_toolTab.SetCurSel(1);
			LRESULT result;
			OnSelchangeToolTab(NULL, &result);
			arg = strtok(NULL, delim);

			// Optional -exit flag.
			if (arg && strcmp(arg,"-exit")==0) {
				m_exitOnFinish = 1;
				arg = strtok(NULL, delim);
			}
			if (!arg) {
				CString tmp; tmp.LoadString(IDS_USAGE);
				err += tmp;
			}
			else {
				// Collect all remaining tokens as pipe-delimited source files.
				CString files;
				while (arg) {
					if (!files.IsEmpty()) files += " | ";
					files += arg;
					arg = strtok(NULL, delim);
				}
				TRY {
					m_FSRC.SetWindowText(files);
					m_video.BuildRecording(m_AVIPrefix + '|' + files + '|' + m_AVISuffix,  m_VDSTname);
					m_video.StartRecording();
					SetTimer(1, 200, NULL);
				}
				CATCH_ALL(e) {
					InitVideo();
					Exception2Status(e);
				}
				END_CATCH_ALL;
			}
		}
		else {
			CString tmp; tmp.LoadString(IDS_USAGE);
			err += tmp;
		}
		if (!err.IsEmpty()) {
			// Usage error: display message and exit without showing the dialog.
			MessageBox(err,NULL,MB_OK | MB_ICONEXCLAMATION);
			m_video.Destroy();
			CDialog::OnCancel();
		}
	}
	else {
		// No command-line arguments: initialise the pipeline for the restored tab.
		LRESULT result;
		OnSelchangeToolTab(NULL, &result);
	}

	return TRUE;
}

/* OnSysCommand -- intercept the IDM_ABOUTBOX system menu command to show the
 * About dialog; forward all other system commands to the base class.
 */
void CDVToolsDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CDVToolsDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CDVToolsDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

/* OnGetMinMaxInfo -- enforce the minimum window size.
 * m_minWidth/m_minHeight are captured from the dialog's initial size in
 * OnInitDialog() so the layout never collapses below the design-time dimensions.
 */
void CDVToolsDlg::OnGetMinMaxInfo(MINMAXINFO FAR* lpMMI)
{
	CDialog::OnGetMinMaxInfo(lpMMI);
	lpMMI->ptMinTrackSize.x = m_minWidth;
	lpMMI->ptMinTrackSize.y = m_minHeight;
}

/* OnSize -- reposition and resize all controls proportionally.
 *
 * When the dialog is restored from minimised state the window rect is saved to
 * m_lastRect.  The resize is only performed once the baseline has been
 * established (m_originalRect.right != 0).
 *
 * For each control in ctrlProperties[] the new position is:
 *   left   = originalLeft   + dx  * (newCX - origCX) / 100
 *   top    = originalTop    + dy  * (newCY - origCY) / 100
 *   width  = originalWidth  + (dw-dx) * (newCX - origCX) / 100
 *   height = originalHeight + (dh-dy) * (newCY - origCY) / 100
 *
 * After repositioning all controls the tab item widths are recalculated and
 * the client area is fully invalidated and repainted.
 */
void CDVToolsDlg::OnSize(UINT nType, int cx, int cy)
{
	if (nType == SIZE_RESTORED) GetWindowRect(&m_lastRect);
	if (m_originalRect.right)
	{
		int dx = cx - m_originalRect.right;
		int dy = cy - m_originalRect.bottom;

		int i;

		for(i=0; i<NControls; i++)
		{
			GetDlgItem(ctrlProperties[i].id)->MoveWindow(
				(ctrlProperties[i].dx*dx)/100+m_originalRects[i].left,
				(ctrlProperties[i].dy*dy)/100+m_originalRects[i].top,
				((ctrlProperties[i].dw-ctrlProperties[i].dx)*dx)/100+m_originalRects[i].right-m_originalRects[i].left,
				((ctrlProperties[i].dh-ctrlProperties[i].dy)*dy)/100+m_originalRects[i].bottom-m_originalRects[i].top,
				false);
		}

		SetToolTabItemSize();

		InvalidateRect(NULL);
		UpdateWindow();
	}
}


/* OnMove -- track the window position so m_lastRect stays current.
 * Only updates when the window is in the normal (non-iconic, non-maximised)
 * state so that the saved position always reflects the restored window rect.
 */
void CDVToolsDlg::OnMove(int x, int y)
{
	CDialog::OnMove(x, y);

	if (!IsIconic() && !IsZoomed())	GetWindowRect(&m_lastRect);
}

/* OnSelchangeToolTab -- show/hide controls for the newly selected tab and
 * reinitialise the DirectShow pipeline.
 *
 * Each control's visibility is determined by testing (1 << sel) against its
 * tabMask: TAB_CAPTURE (mask bit 0) is visible when sel==0, TAB_RECORD (bit 1)
 * when sel==1, and TAB_ALL (-1, all bits set) is always visible.
 *
 * After updating visibility, InitVideo() builds the capture graph (for tab 0)
 * or destroys any existing pipeline and shows a prompt (for tab 1).
 */
void CDVToolsDlg::OnSelchangeToolTab(NMHDR* pNMHDR, LRESULT* pResult)
{
	int sel = m_toolTab.GetCurSel();

	int i;

	for(i=0; i<NControls; i++)
	{
		GetDlgItem(ctrlProperties[i].id)->ShowWindow(((1<<sel) & ctrlProperties[i].tabMask ) ? SW_SHOW : SW_HIDE);
	}

	UpdateWindow();

	TRY {
		InitVideo();
	}
	CATCH_ALL(e) {
		TCHAR buf[1024];
		e->GetErrorMessage(buf, sizeof buf);
		MessageBox(buf, NULL, MB_OK | MB_ICONERROR);
	}
	END_CATCH_ALL;

	*pResult = 0;
}


/* SetToolTabItemSize -- resize all tab items to fill the tab control evenly.
 *
 * The item height is preserved from the first item's current rect.  The width
 * is computed so that all items together (with a half-item gutter on each side)
 * span the full tab control width:
 *   itemWidth = controlWidth * 2 / (itemCount * 2 + 1)
 *
 * Called from OnInitDialog() and after every resize (OnSize()).
 */
void CDVToolsDlg::SetToolTabItemSize()
{
	RECT rc;
	CSize sz;
	m_toolTab.GetItemRect(0, &rc);
	sz.cy = rc.bottom - rc.top;
	m_toolTab.GetWindowRect(&rc);
	sz.cx = (rc.right - rc.left) * 2 / (m_toolTab.GetItemCount()*2 + 1);
	m_toolTab.SetItemSize(sz);
}

/* OnCmdTabChange -- programmatically switch to the tab whose index is encoded
 * in the WM_COMMAND ID (nID - IDC_TAB_CHANGE).  The switch is only performed
 * when the tab control is enabled and the target tab differs from the current one.
 */
void CDVToolsDlg::OnCmdTabChange(UINT nID)
{
	int newTab = nID - IDC_TAB_CHANGE;
	if (m_toolTab.IsWindowEnabled() && m_toolTab.GetCurSel() != newTab) {
		m_toolTab.SetCurSel(newTab);
		LRESULT result;
		OnSelchangeToolTab(NULL, &result);
	}
}

/* OnCtlColor -- paint the video preview (IDC_VIDEO) with a black background.
 * All other controls use the default dialog colour returned by the base class.
 */
HBRUSH CDVToolsDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CDialog::OnCtlColor(pDC, pWnd, nCtlColor);

	if (pWnd->GetDlgCtrlID() == IDC_VIDEO) {
		hbr = (HBRUSH)GetStockObject(BLACK_BRUSH);
	}

	return hbr;
}

/* OnCancel -- suppress the default Escape/Cancel behaviour that would close the
 * dialog.  Instead, reinitialise the pipeline to the idle state for the active tab.
 */
void CDVToolsDlg::OnCancel()
{
	InitVideo();
}

/* OnClose -- persist all settings to the registry and close the dialog.
 *
 * Registry keys written (all under HKCU\Software\Petr Mourek\WinDV):
 *
 *   MainWindow\X, Y, W, H        — last non-iconic window rect
 *   MainWindow\DVControlEnabled  — DV transport control checkbox state
 *   MainWindow\SelectedTool      — active tab index (0=Capture, 1=Record)
 *   MainWindow\WorkingDirectory  — current working directory for relative paths
 *
 *   Capture\DVDevice             — friendly name of the capture source device
 *   Capture\File                 — last capture destination filename base
 *   Capture\Type2AVI             — AVI type (0=Type-1, 1=Type-2)
 *   Capture\DiscontinuityTreshold— max frame gap before auto-split
 *   Capture\MaxAVIFrames         — max frames per output AVI file
 *   Capture\EveryNth             — frame sub-sampling factor
 *   Capture\DateTimeFormat       — strftime format for timestamped filenames
 *   Capture\DateTimeFormatHistory— newline-delimited recently used formats
 *   Capture\SuffixDigits         — sequence counter digit count
 *
 *   Record\DVDevice              — friendly name of the record destination device
 *   Record\File                  — last record source file list
 *   Record\AVIPrefix             — pipe-delimited leader AVI files
 *   Record\AVISuffix             — pipe-delimited trailer AVI files
 *   Record\Preview               — enable live preview during recording
 */
void CDVToolsDlg::OnClose()
{
	m_video.Destroy();

	CString tmp;

	AfxGetApp()->WriteProfileInt("MainWindow", "X", m_lastRect.left);
	AfxGetApp()->WriteProfileInt("MainWindow", "Y", m_lastRect.top);
	AfxGetApp()->WriteProfileInt("MainWindow", "W", m_lastRect.right - m_lastRect.left);
	AfxGetApp()->WriteProfileInt("MainWindow", "H", m_lastRect.bottom - m_lastRect.top);
	AfxGetApp()->WriteProfileInt("MainWindow", "DVControlEnabled", m_video.m_DVctrl);
	AfxGetApp()->WriteProfileInt("MainWindow", "SelectedTool", m_toolTab.GetCurSel());
	char wdir[1024];
	GetCurrentDirectory(sizeof wdir, wdir);
	AfxGetApp()->WriteProfileString("MainWindow", "WorkingDirectory", wdir);
	AfxGetApp()->WriteProfileString("Capture", "DVDevice", m_VSRCname);
	AfxGetApp()->WriteProfileString("Record", "DVDevice", m_VDSTname);
	m_FSRC.GetWindowText(tmp);
	AfxGetApp()->WriteProfileString("Record", "File", tmp);
	m_FDST.GetWindowText(tmp);
	AfxGetApp()->WriteProfileString("Capture", "File", tmp);

	AfxGetApp()->WriteProfileInt("Capture", "Type2AVI", m_video.m_type2AVI);
	AfxGetApp()->WriteProfileInt("Capture", "DiscontinuityTreshold", m_video.m_discontinuityTreshold);
	AfxGetApp()->WriteProfileInt("Capture", "MaxAVIFrames", m_video.m_maxAVIFrames);
	AfxGetApp()->WriteProfileInt("Capture", "EveryNth", m_video.m_everyNth);

	AfxGetApp()->WriteProfileString("Capture", "DateTimeFormat", m_DTFormat);
	AfxGetApp()->WriteProfileString("Capture", "DateTimeFormatHistory", m_DTFormatHistory);
	AfxGetApp()->WriteProfileInt("Capture", "SuffixDigits", m_nSuffixDigits);

	AfxGetApp()->WriteProfileString("Record", "AVIPrefix", m_AVIPrefix);
	AfxGetApp()->WriteProfileString("Record", "AVISuffix", m_AVISuffix);
	AfxGetApp()->WriteProfileInt("Record", "Preview", m_video.m_recordPreview);

	CDialog::OnCancel();
}

/* OnOK -- suppress the default Enter key behaviour (which would close the dialog).
 * Intentionally empty.
 */
void CDVToolsDlg::OnOK()
{
}

/* Exception2Status -- render a caught MFC exception as an error string in m_status.
 * e  Exception whose GetErrorMessage() is called; the caller retains ownership.
 */
void CDVToolsDlg::Exception2Status(CException *e)
{
	TCHAR buf[1024];
	e->GetErrorMessage(buf, sizeof buf);
	CString tmp;
	tmp.Format("Error: %s", buf);
	m_status.SetWindowText(tmp);
}

/* OnVsrcSel -- enumerate available DV capture source devices and let the user
 * pick one from CVideoDeviceSel.  On OK, updates m_VSRCname, the display label,
 * and calls InitVideo() to rebuild the capture pipeline with the new device.
 */
void CDVToolsDlg::OnVsrcSel()
{
	CArray<CString,CString&> list;
	GetVideoSrcList(list);

	BOOL doInit;

	CVideoDeviceSel devSel(list, m_VSRCname);
	if ((doInit = (devSel.DoModal() == IDOK))) {
		m_VSRCname = list[devSel.GetSelection()];
		m_VSRC.SetWindowText(m_VSRCname);
	}

	if (doInit) InitVideo();
}

/* OnVdstSel -- enumerate available DV record destination devices and let the user
 * pick one from CVideoDeviceSel.  On OK, updates m_VDSTname and the display label.
 * InitVideo() is called to reflect the change, though for the Record tab it simply
 * resets the status prompt.
 */
void CDVToolsDlg::OnVdstSel()
{
	CArray<CString,CString&> list;
	GetVideoDstList(list);

	BOOL doInit;

	CVideoDeviceSel devSel(list, m_VDSTname);
	if ((doInit = (devSel.DoModal() == IDOK))) {
		m_VDSTname = list[devSel.GetSelection()];
		m_VDST.SetWindowText(m_VDSTname);
	}

	if (doInit) InitVideo();
}

/* InitVideo -- reset the pipeline to the ready/idle state for the active tab.
 *
 * Always clears m_exitOnFinish, stops the status timer, and blanks all status
 * labels.
 *
 * Tab 0 (Capture): calls CDV::BuildCapturing() to connect the DV device to
 *   the preview and arm the ready-to-capture state, then starts the 200 ms
 *   status timer.  Any DirectShow exception is displayed in m_status.
 *
 * Tab 1 (Record): destroys any existing pipeline (CDV::Destroy()) and sets
 *   m_status to a prompt so the user knows to select a file and press Record.
 *
 * Called from: OnSelchangeToolTab(), OnCancel(), OnVsrcSel(), OnVdstSel(),
 * and all CATCH_ALL blocks after a failed start attempt.
 */
void CDVToolsDlg::InitVideo()
{
	m_exitOnFinish = 0;

	KillTimer(1);
	int sel = m_toolTab.GetCurSel();
	m_status.SetWindowText("Initializing...");
	m_status2.SetWindowText("");
	m_status3.SetWindowText("");
	m_counter.SetWindowText("");

	switch(sel) {
	case 0:
		TRY {
			CString filename;
			m_video.BuildCapturing(m_VSRCname);
			SetTimer(1, 200, NULL);
		}
		CATCH_ALL(e) {
			Exception2Status(e);
		}
		END_CATCH_ALL;
		break;
	case 1:
		m_video.Destroy();
		m_status.SetWindowText("Select file and press <Record>");
		break;
	}
}


/* OnFsrcSel -- open a multi-file browse dialog for the record source file list
 * and populate m_FSRC.  Delegates to the global SelectFile(TRUE, ...) helper.
 */
void CDVToolsDlg::OnFsrcSel()
{
	SelectFile(TRUE, &m_FSRC);
}


/* OnFdstSel -- open a single-file browse dialog for the capture destination,
 * populate m_FDST, then strip the extension via CaptureFilenameExtractBase so
 * the field shows only the base name that CDV::StartCapturing() will use.
 */
void CDVToolsDlg::OnFdstSel()
{
	SelectFile(FALSE, &m_FDST);
	CString filename;
	m_FDST.GetWindowText(filename);
	CaptureFilenameExtractBase(filename);
	m_FDST.SetWindowText(filename);
}

/* OnCapture -- toggle button handler for the Capture tab.
 *
 * State transitions:
 *   CapturePaused  -> StartCapturing()  (pipeline is built; begin writing frames)
 *   Capturing      -> StopCapturing()   (pause; output file is flushed/closed)
 *   any other      -> InitVideo()       (error recovery / reset)
 *
 * Exceptions from the DirectShow layer are caught, the pipeline is reset to
 * idle, and the error text is displayed in m_status.
 */
void CDVToolsDlg::OnCapture()
{
	if (m_video.GetState() == CDV::CapturePaused) {
		TRY {
			CString filename;
			m_FDST.GetWindowText(filename);
			m_video.StartCapturing(filename, m_DTFormat, m_nSuffixDigits);
		}
		CATCH_ALL(e) {
			InitVideo();
			Exception2Status(e);
		}
		END_CATCH_ALL;
	}
	else if (m_video.GetState() == CDV::Capturing) {
		TRY {
			m_video.StopCapturing();
		}
		CATCH_ALL(e) {
			InitVideo();
			Exception2Status(e);
		}
		END_CATCH_ALL;
	}
	else
		InitVideo();
}

/* OnRecord -- toggle button handler for the Record tab.
 *
 * State transitions:
 *   RecordPaused  -> StartRecording()   (pipeline is built; begin sending frames)
 *   Recording     -> StopRecording()    (pause; DV device transport stops)
 *   any other     -> BuildRecording() + StartRecording()
 *                                       (first press: build graph from file list,
 *                                        then start playing to the DV device)
 *
 * The file list passed to BuildRecording() combines the optional prefix and
 * suffix (from configuration) with the user-provided files:
 *   m_AVIPrefix | <m_FSRC text> | m_AVISuffix
 * Any of the three sections may be empty.
 */
void CDVToolsDlg::OnRecord()
{
	if (m_video.GetState() == CDV::RecordPaused) {
		TRY {
			m_video.StartRecording();
		}
		CATCH_ALL(e) {
			InitVideo();
			Exception2Status(e);
		}
		END_CATCH_ALL;
	}
	else if (m_video.GetState() == CDV::Recording) {
		TRY {
			m_video.StopRecording();
		}
		CATCH_ALL(e) {
			InitVideo();
			Exception2Status(e);
		}
		END_CATCH_ALL;
	}
	else {
		TRY {
			CString filename;
			m_FSRC.GetWindowText(filename);
			m_video.BuildRecording(m_AVIPrefix + '|' +filename + '|' + m_AVISuffix,  m_VDSTname);
			SetTimer(1, 200, NULL);
		}
		CATCH_ALL(e) {
			InitVideo();
			Exception2Status(e);
		}
		END_CATCH_ALL;
	}
}

/* OnConfig -- open the two-page configuration property sheet.
 *
 * Copies current settings into temporary CCaptureCfg and CRecordCfg objects,
 * builds a CPropertySheet with both pages (pre-selecting the page that matches
 * the active tab), and if the user confirms copies the values back.
 *
 * The "Apply Now" button is suppressed (PSH_NOAPPLYNOW) because settings take
 * effect only when a new pipeline is built; live apply would require a graph
 * rebuild which is not implemented.
 */
void CDVToolsDlg::OnConfig()
{
	int sel = m_toolTab.GetCurSel();

	CCaptureCfg captureCfg;
	CRecordCfg recordCfg;

	// Copy current values into the page data objects before displaying.
	captureCfg.m_type12 = m_video.m_type2AVI ? 1 : 0;
	captureCfg.m_discontinuityTreshold = m_video.m_discontinuityTreshold;
	captureCfg.m_maxAVIFrames = m_video.m_maxAVIFrames;
	captureCfg.m_everyNth = m_video.m_everyNth;
	captureCfg.m_dtformat = m_DTFormat;
	captureCfg.m_dtformathistory = m_DTFormatHistory;
	captureCfg.m_ndigits = m_nSuffixDigits;

	recordCfg.m_aviPrefix = m_AVIPrefix;
	recordCfg.m_aviSuffix = m_AVISuffix;
	recordCfg.m_recordPreview = m_video.m_recordPreview;

	CPropertySheet cfgDlg(IDS_CONFIG_DLG);

	cfgDlg.m_psh.dwFlags |= PSH_NOAPPLYNOW;

	cfgDlg.AddPage(&captureCfg);
	cfgDlg.AddPage(&recordCfg);
	cfgDlg.SetActivePage(m_toolTab.GetCurSel());

	if (cfgDlg.DoModal() == IDOK) {
		// Copy updated values back from the pages.
		m_video.m_type2AVI  = captureCfg.m_type12 == 1;
		m_video.m_discontinuityTreshold = captureCfg.m_discontinuityTreshold;
		m_video.m_maxAVIFrames = captureCfg.m_maxAVIFrames;
		m_video.m_everyNth = captureCfg.m_everyNth;
		m_DTFormat = captureCfg.m_dtformat;
		m_DTFormatHistory = captureCfg.m_dtformathistory;
		m_nSuffixDigits = captureCfg.m_ndigits;

		m_AVIPrefix = recordCfg.m_aviPrefix;
		m_AVISuffix = recordCfg.m_aviSuffix;
		m_video.m_recordPreview = recordCfg.m_recordPreview > 0;
//		InitVideo();
	}
}


/* OnTimer -- 200 ms status refresh tick (timer id=1).
 *
 * Called every 200 ms while the pipeline is active.  Performs two duties:
 *
 * 1. Auto-close check: if m_exitOnFinish is set and the pipeline has reached
 *    CDV::Finished, calls OnClose() to persist settings and exit.
 *
 * 2. Status label updates: constructs three status strings and writes them to
 *    the corresponding labels only if the text has changed (avoids unnecessary
 *    repaints):
 *
 *    m_status (txt):
 *      Human-readable pipeline state with dropped-frame count appended during
 *      active capture.
 *
 *    m_counter (txt2):
 *      Elapsed time from CDV::GetTime() (100 ns units) formatted as H:MM:SS.t
 *      (tenths of a second).  Displayed when the pipeline is in any non-idle state.
 *
 *    m_status3 (txt3):
 *      Ring-buffer queue load ("Q:N") during capture and recording states.
 *      Not shown when paused or finished since the queue is not moving.
 *
 * Also calls SetThreadExecutionState(ES_DISPLAY_REQUIRED) to prevent the
 * monitor from blanking while a capture or record session is in progress.
 */
void CDVToolsDlg::OnTimer(UINT nIDEvent)
{
	// Prevent screen blanking while the pipeline is running.
	SetThreadExecutionState(ES_DISPLAY_REQUIRED);

	// Auto-exit when the pipeline completes and -exit was specified.
	if (m_video.GetState() == CDV::Finished && m_exitOnFinish) {
		OnClose();
	}

	CString txt, txt2, txt3;

	// Build primary status string.
	switch (m_video.GetState()) {
	case CDV::Capturing: {
		txt += "Capturing...  Press <Capture> for pause.";
		CString dropped;
		dropped.Format(" (%d frames dropped)", m_video.GetDropped());
		txt += dropped;
		break;
	}
	case CDV::CapturePaused: txt += "Paused... Press <Capture> for Capturing.";
		break;
	case CDV::Recording: txt += "Recording...  Press <Record> for pause.";
		break;
	case CDV::RecordPaused: txt += "Paused... Press <Record> for recording.";
		break;
	case CDV::Finished: txt += "Finished.";
		break;
	}

	// Build elapsed-time string (H:MM:SS.t) from 100 ns REFERENCE_TIME units.
	switch (m_video.GetState()) {
	case CDV::Capturing:
	case CDV::CapturePaused:
	case CDV::Recording:
	case CDV::RecordPaused:
	case CDV::Finished:
		REFERENCE_TIME t = m_video.GetTime();
		if (t >= 0) {
			t /= 1000000;           // convert 100 ns -> tenths of a second
			int ss = (int)(t % 10); t /= 10;
			int s = (int)(t % 60); t /= 60;
			int m = (int)(t % 60); t /= 60;
			txt2.Format("%d:%02d:%02d.%01d", (int)t, m, s, ss);
		}
	}

	// Build queue-load string for states where the queue is actively draining.
	switch (m_video.GetState()) {
	case CDV::Capturing:
	case CDV::Recording:
	case CDV::RecordPaused:
		txt3.Format(" Q:%i", m_video.GetQueueLoad());
	}

	// Only call SetWindowText() when the value has actually changed to avoid
	// unnecessary WM_PAINT messages on every tick.
	CString tmp;
	m_status.GetWindowText(tmp);
	if (tmp != txt) m_status.SetWindowText(txt);
	m_counter.GetWindowText(tmp);
	if (tmp != txt2) m_counter.SetWindowText(txt2);
	m_status3.GetWindowText(tmp);
	if (tmp != txt3) m_status3.SetWindowText(txt3);

	CDialog::OnTimer(nIDEvent);
}

/* OnDVTimeChange -- handle the WM_DV_TIMECHANGE custom message.
 *
 * Posted by the CDV pipeline (DShow.cpp) whenever a new recording timestamp is
 * decoded from the DV frame SSYB subcode packs (pack type 0x63).  The LPARAM
 * carries the decoded time as a time_t value (UTC seconds since epoch).
 *
 * A positive value is formatted as "DD.MM.'YY HH:MM:SS" and displayed in
 * m_status2.  A zero or negative value (no valid timestamp in the stream)
 * clears the label.
 */
LRESULT CDVToolsDlg::OnDVTimeChange(WPARAM, LPARAM lParam)
{
	char buf[100] = "";
	if (lParam > 0) {
		strftime(buf, sizeof buf, "%d.%m.'%y %H:%M:%S", localtime(&lParam));
	}
	m_status2.SetWindowText(buf);
	return 0;
}

/* OnPicture -- clicking the About logo image opens the About dialog.
 * Delegates to OnSysCommand with IDM_ABOUTBOX.
 */
void CDVToolsDlg::OnPicture()
{
	OnSysCommand(IDM_ABOUTBOX, 0);
}

/* OnDvctrl -- sync the m_DVctrl flag in the CDV pipeline with the checkbox state.
 * When enabled, CDV uses IAMExtTransport to issue Play/Stop/Record commands to
 * the DV device's built-in transport mechanism.
 */
void CDVToolsDlg::OnDvctrl()
{
	m_video.m_DVctrl = m_DVCtrl.GetCheck() > 0;
}

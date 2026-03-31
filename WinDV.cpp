// WinDV.cpp : Defines the class behaviors for the application.
//
// Entry point and global helpers for the WinDV DV capture/record application.
// The application is dialog-based: there is no document/view architecture.
// CDVToolsDlg (DVToolsDlg.cpp) is the sole main window.
//

#include "stdafx.h"
#include "Version.h"
#include "WinDV.h"
#include "DropFilesEdit.h"
#include "DShow.h"
#include "ToolTab.h"
#include "DVToolsDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CWinDVApp

BEGIN_MESSAGE_MAP(CWinDVApp, CWinApp)
	//{{AFX_MSG_MAP(CWinDVApp)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG
//	ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CWinDVApp construction

CWinDVApp::CWinDVApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CWinDVApp object

CWinDVApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CWinDVApp initialization

/* InitInstance -- application entry point called by MFC after WinMain().
 *
 * Performs one-time setup before the main dialog is created:
 *   1. Enables ActiveX control hosting in child windows (AfxEnableControlContainer).
 *   2. Raises the process priority to HIGH_PRIORITY_CLASS.  DV capture is
 *      time-critical: missing the 200 Hz frame delivery deadline causes dropped
 *      frames that cannot be recovered.
 *   3. Enables 3D-look controls appropriate for the shared/static MFC build.
 *   4. Initialises COM with COINIT_MULTITHREADED.  DirectShow filter graphs
 *      and device enumeration require COM; multi-threaded mode is necessary
 *      because capture worker threads call COM interfaces directly.
 *   5. Sets "Petr Mourek" as the registry key root so that all subsequent
 *      GetProfileXxx/WriteProfileXxx calls store settings under
 *      HKCU\Software\Petr Mourek\WinDV.
 *   6. Creates and runs CDVToolsDlg modally.  Returns FALSE in all cases so
 *      that MFC exits rather than entering the message pump.
 */
BOOL CWinDVApp::InitInstance()
{
	AfxEnableControlContainer();

	/* Enable DPI awareness on Vista+ for sharp rendering on high-DPI displays.
	 * On Windows XP, GetProcAddress returns NULL and nothing happens. */
	{
		typedef BOOL (WINAPI *PFN_SetProcessDPIAware)(void);
		HMODULE hUser32 = GetModuleHandle("user32.dll");
		if (hUser32) {
			PFN_SetProcessDPIAware pfn = (PFN_SetProcessDPIAware)
				GetProcAddress(hUser32, "SetProcessDPIAware");
			if (pfn) pfn();
		}
	}

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	//  of your final executable, you should remove from the following
	//  the specific initialization routines you do not need.

#ifdef _AFXDLL
	Enable3dControls();			// Call this when using MFC in a shared DLL
#else
	Enable3dControlsStatic();	// Call this when linking to MFC statically
#endif

	CoInitializeEx(NULL, COINIT_MULTITHREADED);

	SetRegistryKey("Petr Mourek");

	/* Portable mode: if a WinDV.ini file exists next to the EXE,
	 * redirect all GetProfile/WriteProfile calls to that INI file
	 * instead of the registry.  MFC handles this automatically when
	 * m_pszProfileName contains a file path (with backslash). */
	{
		char szIniPath[MAX_PATH];
		GetModuleFileName(NULL, szIniPath, MAX_PATH);
		/* Replace the EXE filename with "WinDV.ini". */
		char *pSlash = strrchr(szIniPath, '\\');
		if (pSlash) {
			strcpy(pSlash + 1, "WinDV.ini");
			if (GetFileAttributes(szIniPath) != INVALID_FILE_ATTRIBUTES) {
				/* Verify the INI is writable before committing to portable mode. */
				HANDLE hFile = CreateFile(szIniPath, GENERIC_WRITE, 0, NULL,
				                          OPEN_EXISTING, 0, NULL);
				if (hFile != INVALID_HANDLE_VALUE) {
					CloseHandle(hFile);
					free((void*)m_pszProfileName);
					m_pszProfileName = _strdup(szIniPath);
				}
			}
		}
	}

	/* Handle --version before creating any windows.
	 * Try the inherited stdout first (works with cmd.exe redirection).
	 * Only attach to parent console if no stdout is available. */
	{
		LPCSTR cmd = m_lpCmdLine;
		while (*cmd == ' ' || *cmd == '\t') cmd++;
		if (strncmp(cmd, "--version", 9) == 0 &&
		    (cmd[9] == '\0' || cmd[9] == ' ' || cmd[9] == '\t')) {
			HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
			if (hOut == NULL || hOut == INVALID_HANDLE_VALUE) {
				SafeAttachConsole();
				hOut = GetStdHandle(STD_OUTPUT_HANDLE);
			}
			if (hOut != NULL && hOut != INVALID_HANDLE_VALUE) {
				char buf[64];
				int len = wsprintfA(buf, "WinDV %s\r\n", VER_STRING);
				DWORD written;
				WriteFile(hOut, buf, len, &written, NULL);
			}
			CoUninitialize();
			ExitProcess(0);
		}
	}

	CDVToolsDlg dlg;
	m_pMainWnd = &dlg;
	int nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with OK
	}
	else if (nResponse == IDCANCEL)
	{
		// TODO: Place code here to handle when the dialog is
		//  dismissed with Cancel
	}

	CoUninitialize();
	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}

/////////////////////////////////////////////////////////////////////////////

/* SelectFile -- display a common file dialog and populate a control with the result.
 *
 * open   TRUE  => "Open" dialog with OFN_ALLOWMULTISELECT.  Multiple selected
 *                 paths are concatenated with " | " as separator, matching the
 *                 pipe-delimited format that CDropFilesEdit and the CDV pipeline
 *                 expect for multi-file record sources.
 *        FALSE => "Save" dialog (single file); the path is set verbatim.
 * ctrl   Target CWnd (typically a CDropFilesEdit) whose text is updated on OK.
 *
 * The function uses a 16 KB path buffer to support long multi-file selections.
 * If the user cancels the dialog the control text is left unchanged.
 */
void SelectFile(BOOL open, CWnd *ctrl)
{
	CFileDialog dlg(open, NULL, NULL, (open ? OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY : 0),
		"*.avi|*.avi||");

	dlg.m_ofn.lpstrInitialDir=".";
	char fbuf[16384] = "";
	dlg.m_ofn.lpstrFile = fbuf;
	dlg.m_ofn.nMaxFile = sizeof fbuf;
	if (dlg.DoModal() == IDOK) {
		CString txt;
		if (open) {
			// Iterate through all selected filenames and join them with " | ".
			POSITION p = dlg.GetStartPosition();
			while (p) {
				txt += dlg.GetNextPathName(p);
				if (p) txt += " | ";
			}

		}
		else
			txt = dlg.GetPathName();

		ctrl->SetWindowText(txt);
	}

}

/////////////////////////////////////////////////////////////////////////////

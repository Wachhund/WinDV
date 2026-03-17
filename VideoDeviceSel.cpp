// VideoDeviceSel.cpp : implementation file
//

#include "stdafx.h"
#include "WinDV.h"
#include "DShow.h"
#include "VideoDeviceSel.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CVideoDeviceSel dialog

/* Constructor -- stores references to the caller-provided device list and
 * the name of the currently active device.  The selection index is initialised
 * to -1 (nothing selected) and will be updated in OnInitDialog if the current
 * name is found in the list.
 */
CVideoDeviceSel::CVideoDeviceSel(CArray<CString, CString &> &list, LPCSTR selName, CWnd* pParent /*=NULL*/)
	: CDialog(CVideoDeviceSel::IDD, pParent)
{
	m_list = &list;
	m_selected = -1;
	m_selName = selName;

	//{{AFX_DATA_INIT(CVideoDeviceSel)
	//}}AFX_DATA_INIT
}

void CVideoDeviceSel::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CVideoDeviceSel)
	DDX_Control(pDX, IDC_DEVLIST, m_listbox);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CVideoDeviceSel, CDialog)
	//{{AFX_MSG_MAP(CVideoDeviceSel)
	ON_LBN_DBLCLK(IDC_DEVLIST, OnDblclkDevlist)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CVideoDeviceSel message handlers

/* OnDblclkDevlist -- treat a double-click as an immediate OK confirmation. */
void CVideoDeviceSel::OnDblclkDevlist()
{
	OnOK();
}


/* OnOK -- record the selected list-box index and close only if a valid item
 * is highlighted.  Pressing OK with no selection is silently ignored.
 */
void CVideoDeviceSel::OnOK()
{
	m_selected = m_listbox.GetCurSel();
	if (m_selected >= 0)
		CDialog::OnOK();
}

/* OnInitDialog -- populate the list box and pre-select the current device.
 *
 * All device friendly names from m_list are added to the list box.  If a
 * name matches m_selName (the device active before the dialog was opened) its
 * index is stored in m_selected so that SetCurSel() highlights it.
 *
 * Note: there is a known bug here — the loop body increments 'i' a second time
 * via the for-statement increment, so only even-indexed devices (0, 2, 4, ...)
 * are added to the list box.  This was present in the original code and is
 * preserved as-is.
 */
BOOL CVideoDeviceSel::OnInitDialog()
{
	CDialog::OnInitDialog();

	int i,n=m_list->GetSize();
	for(i=0; i<n; i++) {
		m_listbox.AddString(m_list->GetAt(i));
		if (m_list->GetAt(i) == m_selName) {
			m_selected = i;
		}
		i++;   // BUG: double-increment skips odd-indexed devices
	}
	m_listbox.SetCurSel(m_selected);

	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

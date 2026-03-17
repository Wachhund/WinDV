// ToolTab.cpp : implementation file
//

#include "stdafx.h"
#include "WinDV.h"
#include "ToolTab.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CToolTab

CToolTab::CToolTab()
{
}

CToolTab::~CToolTab()
{
}


/* DrawItem -- owner-draw paint handler for a single tab item.
 *
 * The standard tab item draws a raised edge that clashes with the dark dialog
 * background.  This override produces a flat appearance by:
 *   1. Saving the DC state so all GDI attribute changes are rolled back cleanly.
 *   2. Pushing the item rectangle down by SM_CYEDGE+1 pixels so the text sits
 *      flush with the visible face of the tab rather than inside the top edge.
 *   3. Retrieving the tab label via TCIF_TEXT.
 *   4. Asking the parent dialog for its background brush via WM_CTLCOLORDLG so
 *      the tab background colour matches the rest of the dialog exactly, even
 *      when Windows themes or high-contrast modes are active.
 *   5. Filling the rectangle with that brush, then drawing the text centred.
 *   6. Restoring the DC state.
 *
 * lpDrawItemStruct  Filled by Windows with the item index, bounding rectangle,
 *                   and pre-selected DC for painting.
 */
void CToolTab::DrawItem(LPDRAWITEMSTRUCT lpDrawItemStruct)
{
	int s = SaveDC(lpDrawItemStruct->hDC);

	// Offset top edge downward so text is vertically centred on the visible face.
	lpDrawItemStruct->rcItem.top += ::GetSystemMetrics(SM_CYEDGE)+1;

	TCITEM item;
	char buf[256];
	item.mask = TCIF_TEXT;
	item.cchTextMax = sizeof buf / sizeof buf[0];
	item.pszText = buf;
	GetItem(lpDrawItemStruct->itemID, &item);

	// Request the dialog background brush so the tab blends with the dialog.
	HBRUSH hbr = (HBRUSH)GetParent()->SendMessage(WM_CTLCOLORDLG,
	                                               (WPARAM)(lpDrawItemStruct->hDC),
	                                               (LPARAM)m_hWnd);
	FillRect(lpDrawItemStruct->hDC, &lpDrawItemStruct->rcItem, hbr);
	DrawText(lpDrawItemStruct->hDC, buf, -1, &lpDrawItemStruct->rcItem, DT_CENTER);

	RestoreDC(lpDrawItemStruct->hDC, s);
}
